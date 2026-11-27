// Metadata decorators use ordinary nominal record types and constant values.
#pragma once

namespace klang {

inline void TypeCheck::CheckDecoratorApplications() {
    set<SStruct *> invalid;
    for (auto st : ast.structs) {
        if (st->decorator_target == DecoratorTarget::None) continue;
        if (!CheckRoot([&] {
            for (const auto &field : st->fields) {
                auto t = Subst(field.type);
                if ((t->kind == TY_INT && !IsVarintT(t) && !t->ix) ||
                    t->kind == TY_BOOL || t->kind == TY_FLT ||
                    (t->kind == TY_SLICE && t->cq && IsU8(t->sub)))
                    continue;
                Error(field.line, "a decorator parameter needs a scalar or const u8[:] type");
            }
            if (auto signature = st->decorator_signature) {
                for (auto t : signature->fn->args) ValidateType(Subst(t), t->line, VT_PARAM);
                for (auto t : signature->fn->rets) ValidateType(Subst(t), t->line, VT_RET);
            }
        })) invalid.insert(st);
    }

    auto check = [&](vector<DecoratorApplication> &applications, DecoratorTarget target,
                     SFunction *sf = nullptr) {
        set<SStruct *> applied;
        for (auto &application : applications) CheckRoot([&] {
            auto type = application.type;
            if (type->kind == TY_STRUCT && type->struc->st->unknown) return;
            if (type->kind != TY_STRUCT || type->struc->st->decorator_target == DecoratorTarget::None)
                Error(application.at, cat("unknown decorator: ", TypeStr(type)));
            auto st = type->struc->st;
            if (invalid.count(st)) return;
            if (!applied.insert(st).second)
                Error(application.at, cat("decorator ", st->qname, " is applied more than once"));
            if (target != st->decorator_target || (sf && (sf->isextern || sf->isthread))) {
                auto required = DecoratorTargetName(st->decorator_target);
                if (st->decorator_target == DecoratorTarget::AsyncFunction) required = "an async fn";
                else if (st->decorator_target == DecoratorTarget::Function) required = "a synchronous K fn";
                Error(application.at, cat("decorator ", st->qname, " requires ", required));
            }
            if (auto signature = st->decorator_signature; signature && signature->fn->has_sig) {
                const auto &results = sf->async_body ? sf->async_rets : sf->rets;
                const auto &sig = *signature->fn;
                auto equal_type = [&](TypeExpr *a, TypeExpr *b) {
                    return TypeEq(Subst(a), Subst(b));
                };
                if (sf->IsGeneric() || sf->params.size() != sig.args.size() ||
                    !equal(sf->params.begin(), sf->params.end(), sig.args.begin(),
                           [&](const Param &p, TypeExpr *t) { return equal_type(p.type, t); }) ||
                    results.size() != sig.rets.size() ||
                    !equal(results.begin(), results.end(), sig.rets.begin(), equal_type))
                    Error(application.at, cat("decorator ", st->qname, " requires signature ",
                                              TypeStr(signature)));
            }
            if (application.args.size() != st->fields.size())
                Error(application.at, cat("decorator ", st->qname, " expects ", st->fields.size(),
                                          " constant arguments"));

            auto value = ast.New<StructLit>(application.at, type);
            set<VarDecl *> visiting;
            for (auto arg : application.args)
                value->inits.push_back({ {}, ExpansionConstant(arg, visiting, "decorator argument") });
            CheckV(value, nullptr);
            application.value = value;
        });
    };

    for (auto sf : ast.functions)
        if (!sf->brokensig && !sf->decorators.empty())
            check(sf->decorators, sf->async_body ? DecoratorTarget::AsyncFunction : DecoratorTarget::Function, sf);
    for (auto st : ast.structs) {
        if (!st->decorators.empty()) check(st->decorators, DecoratorTarget::Struct);
        for (auto &field : st->fields)
            if (!field.decorators.empty()) check(field.decorators, DecoratorTarget::Field);
    }
    for (auto en : ast.enums) {
        if (!en->decorators.empty()) check(en->decorators, DecoratorTarget::Enum);
        for (auto &variant : en->variants)
            for (auto &field : variant.fields)
                if (!field.decorators.empty()) check(field.decorators, DecoratorTarget::Field);
    }
}

inline void TypeCheck::LowerDecoratorWrappers() {
    const auto count = ast.functions.size();
    for (size_t i = 0; i < count; ++i) {
        auto source = ast.functions[i];
        if (!source->body || source->broken || source->brokensig ||
            none_of(source->decorators.begin(), source->decorators.end(), [](const auto &a) {
                return a.value && a.type->struc->st->decorator_wrapper;
            })) continue;
        CheckRoot([&] { LowerDecoratedFunction(source); });
    }
}

inline void TypeCheck::LowerDecoratedFunction(SFunction *source) {
    auto original_join = source->async_body ? source->rets[0]->struc->st->task_join : nullptr;
    SFunction *original = nullptr;
    SFunction *next = nullptr;
    auto private_function = [&](Line line, Line end) {
        auto fn = new SFunction(*source);
        ast.functions.push_back(fn);
        fn->qname = ast.Intern(cat(source->qname, "#decorator", ast.functions.size()));
        fn->line = line;
        fn->end = end;
        fn->decorators.clear();
        fn->decorator_public = source;
        fn->isexport = fn->isdeprecated = false;
        fn->cname.clear();
        for (auto &param : fn->params) param.defaultval = nullptr;
        return fn;
    };
    auto clone_body = [&](SFunction *from, SFunction *to) {
        ExpansionClone clone;
        clone.functions.emplace(from, to);
        Saved<ExpansionClone *> cloning(ast.expansionclone, &clone);
        return Is<Block>(from->body->Clone(ast));
    };
    for (auto it = source->decorators.rbegin(); it != source->decorators.rend(); ++it) {
        if (!it->value) continue;
        auto decorator = it->type->struc->st;
        auto wrapper = decorator->decorator_wrapper;
        if (!wrapper || wrapper->broken) continue;
        if (!original) {
            original = private_function(source->line, source->end);
            original->body = original_join ? source->body : clone_body(source, original);
            next = original;
        }
        auto layer = private_function(wrapper->line, wrapper->end);
        layer->ns = wrapper->ns;
        layer->name = wrapper->name;
        layer->decorator_layer = decorator;
        layer->decorator_next = next;
        layer->body = clone_body(wrapper, layer);

        vector<Node *> bindings;
        for (size_t f = 0; f < decorator->fields.size(); ++f) {
            const auto &field = decorator->fields[f];
            auto decl = ast.New<VarDecl>(field.line, false);
            decl->names.push_back(field.name);
            decl->type = field.type;
            decl->inits.push_back(it->value->InitFor((int)f)->Clone(ast));
            bindings.push_back(decl);
        }
        layer->body->stmts.insert(layer->body->stmts.begin(), bindings.begin(), bindings.end());
        if (original_join) {
            layer->async_body = nullptr;
            layer->rets = source->async_rets;
            layer->decorator_result_source = original_join;
            LowerAsyncFunction(ast, layer);
        } else {
            layer->decorator_original = original;
            for (size_t p = 0; p < layer->params.size(); ++p)
                layer->params[p].name = ast.Intern(cat("#arg", p));
        }
        next = layer;
    }
    if (!original) return;
    if (original_join) {
        source->body = next->body;
        source->rets = next->rets;
        return;
    }
    source->decorator_written = source->body;
    source->decorator_original = original;
    source->decorator_next = next;
    auto at = source->line;
    source->body = ast.New<Block>(at);
    auto call = ast.New<Call>(at, ast.New<Ident>(at, "next", source->ns));
    call->decorator_forward = true;
    source->body->tail = call;
}

inline FnSpec *TypeCheck::DecoratorFrame() {
    for (auto spec = frames.back().lexspec; spec; spec = spec->lexparent)
        if (spec->sf && spec->sf->decorator_next) return spec;
    return nullptr;
}

inline Val TypeCheck::CheckDecoratorContinuation(Call *c, FnSpec *frame, SFunction *probe) {
    if (!frame) Error(c, "next() needs an enclosing decorator wrapper");
    auto sf = frame->sf;
    c->args.clear();
    for (const auto &param : sf->params)
        c->args.push_back(ast.New<Ident>(c->line, param.name, sf->ns));
    vector<SFunction *> targets { probe ? probe : sf->decorator_next };
    Node *receiver = nullptr;
    return ResolveCall(c, targets, nullptr, "next", nullptr, receiver, reachable, nullptr, nullptr,
                       frame, probe != nullptr);
}

// A declaration without a result annotation can infer results. A decorator
// constrained to a void signature checks what the body inferred.
inline void TypeCheck::CheckDecoratorResults() {
    for (auto sf : ast.functions) {
        if (sf->decorators.empty() || (sf->async_body ? !sf->async_rets.empty() : sf->has_rets)) continue;
        auto target = sf->async_body ? sf->rets[0]->struc->st->task_join : sf;
        auto unavailable = target->specs.empty();
        auto inferred_result = any_of(target->specs.begin(), target->specs.end(), [](FnSpec *spec) {
            return spec->retsknown && !spec->failed && !spec->rets.empty();
        });
        if (!unavailable && !inferred_result) continue;
        for (const auto &application : sf->decorators) {
            if (!application.value) continue;
            auto st = application.type->struc->st;
            if (st->decorator_signature->fn->has_sig) CheckItem([&] {
                Error(application.at, cat("decorator ", st->qname, unavailable
                                          ? " needs a checkable function body for signature "
                                          : " requires signature ",
                                          TypeStr(st->decorator_signature)));
            });
        }
    }
}

// Read checked records from the explicit lexical scope. This is also used
// by the non-diagnostic walk that inspects a match's potential result arms.
inline vector<TypeCheck::DecoratedDeclaration> TypeCheck::DecoratedDeclarations(
    const ConstFor *query, TypeExpr *decorator, TypeExpr *owner) {
    vector<DecoratedDeclaration> declarations;
    if (!decorator || decorator->kind != TY_STRUCT) return declarations;
    auto tag = decorator->struc->st;
    auto metadata = [&](const vector<DecoratorApplication> &applications) -> StructLit * {
        for (const auto &application : applications)
            if (application.value && application.type->struc->st == tag) return application.value;
        return nullptr;
    };
    if (!query->decorator_ns) {
        if (tag->decorator_target != DecoratorTarget::Field || !owner) return declarations;
        const auto fields = owner->kind == TY_STRUCT ? &owner->struc->st->fields
                          : owner->kind == TY_VARIANT && owner->var->variant
                              ? &owner->var->variant->fields : nullptr;
        if (fields) for (const auto &field : *fields)
            if (auto value = metadata(field.decorators))
                declarations.push_back({ field.name, value, nullptr, nullptr, &field });
        return declarations;
    }

    auto ns = ast.FindNS(*query->decorator_ns);
    if (!ns) return declarations;
    switch (tag->decorator_target) {
        case DecoratorTarget::Function:
        case DecoratorTarget::AsyncFunction:
            for (const auto &[name, overloads] : ns->functionmap)
                for (auto fn : overloads)
                    if (auto value = metadata(fn->decorators))
                        declarations.push_back({ fn->qname, value, fn });
            break;
        case DecoratorTarget::Struct:
            for (const auto &[name, st] : ns->structmap)
                if (auto value = metadata(st->decorators))
                    declarations.push_back({ st->qname, value, nullptr, ast.StructOf(st, {}, query->line) });
            break;
        case DecoratorTarget::Enum:
            for (const auto &[name, en] : ns->enummap)
                if (auto value = metadata(en->decorators))
                    declarations.push_back({ en->qname, value, nullptr, ast.EnumOf(en, {}, false, query->line) });
            break;
        default: break;
    }
    sort(declarations.begin(), declarations.end(), [&](const auto &a, const auto &b) {
        if (a.name != b.name) return a.name < b.name;
        if (!a.function || !b.function) return false; // Only overloads share a qualified name.
        auto x = a.function->line, y = b.function->line;
        return tie(ast.sources[x.fileidx].first, x.line, x.col) <
               tie(ast.sources[y.fileidx].first, y.line, y.col);
    });
    return declarations;
}

inline void TypeCheck::ExpandDecorated(ConstFor *query, vector<vector<ExpansionArg>> &entries,
                                       vector<string> &descriptions) {
    auto resolve = [&](TypeExpr *type) {
        ConstNamesIn(type);
        return Subst(type);
    };
    auto decorator = resolve(query->types[0]);
    if (decorator->kind == TY_STRUCT && decorator->struc->st->unknown) throw Abandoned {};
    if (decorator->kind != TY_STRUCT || decorator->struc->st->decorator_target == DecoratorTarget::None)
        Error(query, "decorated requires a declared decorator type");
    ValidateType(decorator, query->line, VT_LOCAL);
    auto field = decorator->struc->st->decorator_target == DecoratorTarget::Field;
    if (field == query->decorator_ns.has_value())
        Error(query, field ? "a field decorator needs a struct or enum variant scope"
                           : "a function or type decorator needs an explicit namespace scope");
    TypeExpr *owner = nullptr;
    if (field) {
        owner = ast.PlainOf(resolve(query->types[1]));
        if (owner->kind == TY_STRUCT && owner->struc->st->unknown) throw Abandoned {};
        if (HasGenerics(owner) || (owner->kind != TY_STRUCT && owner->kind != TY_VARIANT))
            Error(query, "a field decorator needs a concrete struct or enum variant scope");
        ValidateType(owner, query->line, VT_POINTEE);
    }
    for (const auto &decl : DecoratedDeclarations(query, decorator, owner)) {
        ExpansionArg symbol { query->bindings[1] };
        if (decl.function) {
            auto id = ast.New<Ident>(query->line, decl.name, decl.function->ns);
            id->boundfn = decl.function;
            symbol.value = id;
        } else if (decl.field) {
            symbol.owner = owner;
            symbol.field = decl.field->name;
        } else {
            symbol.type = decl.type;
            symbol.nominal = true;
        }
        entries.push_back({ { query->bindings[0], ast.New<StrLit>(query->line, string(decl.name)) },
                            symbol, { query->bindings[2], decl.metadata } });
        descriptions.push_back(cat("decorated ", owner ? cat(TypeStr(owner), ".") : string(), decl.name));
    }
}

inline const vector<DecoratorApplication> &TypeCheck::DecoratorTypeApplications(Call *c, SStruct *decorator) {
    auto owner = Subst(c->tyargs[1]);
    if (owner->kind == TY_STRUCT && owner->struc->st->unknown) throw Abandoned {};
    auto target = decorator->decorator_target;
    if (target == DecoratorTarget::Struct || target == DecoratorTarget::Enum) {
        if (!c->args.empty())
            Error(c, "a type metadata lookup takes only the decorator and type arguments");
        if (target == DecoratorTarget::Struct && owner->kind == TY_STRUCT) {
            if (!owner->struc->args.empty()) GetStructInst(owner);
            return owner->struc->st->decorators;
        }
        if (target == DecoratorTarget::Enum && owner->kind == TY_ENUM) {
            if (!owner->enu->args.empty()) GetEnumInst(owner);
            return owner->enu->en->decorators;
        }
        Error(c, cat("decorator ", decorator->qname, " requires ", DecoratorTargetName(target)));
    }

    auto argument = c->args.size() == 1 ? Is<Ident>(c->args[0]) : nullptr;
    if (!argument || argument->name.find("::") != string_view::npos)
        Error(c, "a field metadata lookup needs one unqualified field name");
    const vector<Field> *fields;
    bool broken;
    if (owner->kind == TY_STRUCT) {
        auto st = owner->struc->st;
        if (!owner->struc->args.empty()) GetStructInst(owner);
        fields = &st->fields;
        broken = st->broken;
    } else if (owner->kind == TY_VARIANT && owner->var->variant) {
        auto adt = owner->var->adt;
        if (!adt->enu->args.empty()) GetEnumInst(adt);
        fields = &owner->var->variant->fields;
        broken = adt->enu->en->broken;
    } else {
        Error(c, "a field metadata lookup needs a struct or enum variant type");
    }
    auto field = find_if(fields->begin(), fields->end(), [&](const Field &f) {
        return !f.ispad && f.name == argument->name;
    });
    if (field == fields->end()) {
        if (broken) throw Abandoned {};
        Error(argument, cat("type ", TypeStr(owner), " has no field ", argument->name));
    }
    argument->fieldref = &*field;
    return field->decorators;
}

inline Val TypeCheck::CheckDecoratorMetadata(Call *c) {
    if (c->tyargs.empty() || c->tyargs.size() > 2 || c->trailing)
        Error(c, "decorator_metadata needs a decorator type and a named function, type or field");
    auto type = Subst(c->tyargs[0]);
    if (type->kind != TY_STRUCT || type->struc->st->decorator_target == DecoratorTarget::None)
        Error(c, "decorator_metadata needs a declared decorator type");
    auto decorator = type->struc->st;
    StructLit *value = nullptr;
    auto select = [&](const vector<DecoratorApplication> &applications, Node *at) {
        for (const auto &application : applications) {
            if (application.type->kind != TY_STRUCT || application.type->struc->st != decorator) continue;
            if (!application.value) throw Abandoned {};
            if (value) Error(at, cat("decorator ", decorator->qname, " matches multiple overloads"));
            value = application.value;
        }
    };
    if (decorator->decorator_signature) {
        auto argument = c->args.size() == 1 ? Is<Ident>(c->args[0]) : nullptr;
        if (c->tyargs.size() != 1 || !argument)
            Error(c, "decorator_metadata needs a named function");
        auto fn = CheckV(argument, nullptr);
        if (fn.type != fntype || !fn.fnv.named || fn.fnv.env)
            Error(argument, "decorator_metadata needs a named top-level function");
        SFunction *selected = nullptr;
        auto select_function = [&](SFunction *sf) {
            select(sf->decorators, argument);
            if (!selected && value) selected = sf;
        };
        if (fn.fnv.set)
            for (auto sf : *fn.fnv.set) select_function(sf);
        else select_function(fn.fnv.named);
        if (!value) Error(argument, cat("function ", argument->name, " has no @", decorator->qname, " decorator"));
        argument->fnref = selected;
    } else {
        if (c->tyargs.size() != 2)
            Error(c, "type and field metadata lookups need a second type argument naming the declaration");
        const auto &applications = DecoratorTypeApplications(c, decorator);
        auto at = c->args.empty() ? static_cast<Node *>(c) : c->args[0];
        select(applications, at);
        if (!value) Error(at, cat("declaration has no @", decorator->qname, " decorator"));
    }

    c->decorator_value = Is<StructLit>(value->Clone(ast));
    auto output = CheckV(c->decorator_value, type);
    c->decorator_value->exprtype = output.type;
    c->rettypes = { output.type };
    lastcallrets = { output };
    return output;
}

}  // namespace klang
