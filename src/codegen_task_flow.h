// Structured task poll bodies. Resumption restores lexical locals and the
// evaluated operands of enclosing expressions from a per-site union;
// the normal C scopes still describe which cleanup to run at each label.
#pragma once

namespace klang {

inline void CodeGen::EmitTaskFlowJoin(FnSpec *owner, FnSpec *stage, TaskCode &task) {
    TaskFlowCode flow;
    vector<const ForLoop *> loops;
    vector<const VarDef *> storage;
    vector<const VarDef *> pinned;
    size_t loan_slots = 0;
    function<void(Node *)> collect = [&](Node *n) {
        if (auto loop = Is<ForLoop>(n); loop && loop->task_resumable) {
            flow.loops.emplace(loop, loops.size());
            loops.push_back(loop);
        }
        auto call = Is<Call>(n);
        if (call) for (auto scoped : call->scope_tasks) EmitTaskJoin(scoped);
        if (call && call->task_await != TaskAwait::None) {
            auto index = flow.sites.size();
            flow.indices.emplace(call, index);
            flow.sites.push_back({ call, cat("k_task_resume_", index) });
            loan_slots = max(loan_slots, TaskLoanCount(call->task_shared_owners, call->task_slot_loans));
            for (auto v : call->task_values) {
                if (TaskOwnsStack(v)) {
                    if (flow.storage.emplace(v, storage.size()).second) {
                        storage.push_back(v);
                        if (v->reusable) {
                            // Both of a pool's regions have a k_rhdr.
                            flow.freelists.emplace(v, storage.size());
                            storage.push_back(v);
                        }
                    }
                } else if (v->task_pinned && flow.pinned.emplace(v, pinned.size()).second) pinned.push_back(v);
            }
            for (auto [operand, type] : call->task_operands) flow.operand_types.emplace(operand, type);
            if (call->task_await == TaskAwait::Child) {
                EmitTaskJoin(call->spec);
                task.has_child = true;
            }
        }
        RunChildren(n, collect);
    };
    collect(stage->body);
    const auto &info = sinfo[owner];
    if (sinfo[stage].hasrf || !sinfo[stage].freevars.empty() || fromids.count(stage))
        Fail(stage->sf->line, "an async poll cannot return through or capture a separate function call");
    auto initial = owner->argtypes[0];
    auto result = owner->rets.empty() ? nullptr : owner->rets[0];
    if (IsLargeFixed(initial) || (result && IsLargeFixed(result)))
        Fail(owner->sf->line, "task frame value exceeds native poll storage limit");
    auto name = info.cname;
    task.suspends = true;
    task.has_storage = !storage.empty();
    task.pending_input_cleanup = flow.pending_input_cleanup =
        stage->sf->decorator_next && owner->task_cleanup;
    task.frame = flow.frame = Unique(cat(name, "_frame"));
    task.ops = Unique(cat(name, "_ops"));
    flow.poll_body = Unique(cat(name, "_body"));
    Append(code, "typedef struct {\n    ", CT(initial), " initial;\n    int suspended;\n");
    if (task.has_child) Append(code, "    k_task_child child;\n");
    if (task.pending_input_cleanup) Append(code, "    bool forwarded;\n");
    if (loan_slots) Append(code, "    k_task_loan loans[", loan_slots, "];\n");
    if (result) Append(code, "    ", CT(result), " result;\n");
    if (info.needssp) Append(code, "    int64_t sp;\n");
    if (info.located) Append(code, "    const k_loc *at;\n");
    for (size_t i = 0; i < pinned.size(); ++i) Append(code, "    ", CT(pinned[i]->type), " pinned", i, ";\n");
    if (task.has_storage) {
        Append(code, "    k_stack *stacks[", storage.size(), "];\n");
        for (size_t i = 0; i < storage.size(); ++i) {
            auto owner = storage[i];
            auto t = owner->type;
            if (IsResz(t)) EmitCoreTypes();
            auto ct = IsResz(t) ? RzCT(t) : owner->task_pinned ? cat(CT(t), " *") : "uint8_t *";
            Append(code, "    ", ct, " local", i, ";\n");
        }
    }
    for (size_t i = 0; i < loops.size(); ++i) {
        const auto loop = loops[i];
        if (loop->iterkind == IK_ARRAY || loop->iterkind == IK_SLICE) {
            Append(code, "    struct { int64_t counter");
            auto slice = Is<SliceExpr>(loop->iter);
            auto sampled_ref = slice && slice->obj->exprtype->kind == TY_REF;
            if (slice && !sampled_ref) Append(code, loop->task_sequential ? ", count" : ", first, count");
            if (loop->task_sequential) Append(code, ", offset");
            if (sampled_ref) Append(code, "; ", CT(loop->iter->exprtype), " view");
            if (loop->task_sequential && (loop->iterkind == IK_SLICE || loop->iter->exprtype->kind == TY_REF))
                if (auto id = Is<Ident>(loop->iter); id && id->vdef->isvar) Append(code, "; uintptr_t base");
            Append(code, "; } loop", i, ";\n");
            continue;
        }
        auto type = CT(loop->iter->exprtype);
        Append(code, "    struct { ", type, " counter, end;");
        if (loop->idxdef) Append(code, " int64_t index;");
        Append(code, " } loop", i, ";\n");
    }
    if (!flow.sites.empty()) Append(code, "    union {\n");
    for (size_t i = 0; i < flow.sites.size(); ++i) {
        auto call = flow.sites[i].call;
        Append(code, "        struct {\n");
        int64_t bytes = 0;
        auto saved = false;
        for (size_t j = 0; j < call->task_values.size(); ++j) {
            auto v = call->task_values[j];
            if (v->task_storage || v->task_pinned) continue;
            auto type = v->type;
            saved = true;
            // k_pref carries a header and stack pair for each region.
            bytes += (PrefVar(v) ? 2 : 1) * FixedSize(type);
            if (bytes > NATIVE_VALUE_LIMIT)
                Fail(call->line, "task frame values exceed native poll storage limit");
            Append(code, "            ", VarCT(v), " v", j, ";\n");
        }
        for (size_t j = 0; j < call->task_operands.size(); ++j) {
            auto type = call->task_operands[j].second;
            bytes += FixedSize(type);
            if (bytes > NATIVE_VALUE_LIMIT)
                Fail(call->line, "task frame values exceed native poll storage limit");
            Append(code, "            ", CT(type), " operand", j, ";\n");
        }
        if (call->task_await == TaskAwait::Child)
            Append(code, "            ", taskcode.at(call->spec).frame, " child;\n");
        else if (!call->rettypes.empty()) {
            auto type = call->rettypes[0];
            if (FixedSize(type) > NATIVE_VALUE_LIMIT - bytes)
                Fail(call->line, "task frame values exceed native poll storage limit");
            Append(code, "            ", CT(type), " value;\n");
        } else if (!saved && call->task_operands.empty())
            Append(code, "            uint8_t unused;\n");
        Append(code, "        } s", i, ";\n");
    }
    if (!flow.sites.empty()) Append(code, "    } wait;\n");
    Append(code, "} ", task.frame, ";\n");

    task_flow = &flow;
    EmitSpec(stage);
    task_flow = nullptr;
    ResetFnState();
    curspec = owner;
    curinfo = &sinfo[owner];
    auto params = SigParams(owner, true);
    auto poll = Unique(cat(name, "_poll"));
    auto cancel = task.has_child ? Unique(cat(name, "_cancel")) : "NULL";
    auto drain = task.has_child ? Unique(cat(name, "_drain")) : "k_task_drain_local";
    auto arm = task.has_child ? Unique(cat(name, "_arm")) : "NULL";
    auto finish = Unique(cat(name, "_finish"));
    SpecText entry { owner, false, code.size(), 0, { name } };
    Append(code, "static k_task_poll ", poll, "(void *context) {\n    return ", flow.poll_body,
           "(context, false);\n}\n");
    if (task.has_child) {
        Append(code, "static void ", cancel, "(void *context) {\n    ",
               task.frame, " *f = context;\n    k_task_child_cancel(&f->child);\n}\n",
               "static bool ", drain, "(void *context) {\n    ", task.frame, " *f = context;\n",
               "    switch (f->suspended) {\n");
        for (size_t i = 0; i < flow.sites.size(); ++i) {
            auto call = flow.sites[i].call;
            if (call->task_await != TaskAwait::Child) continue;
            Append(code, "    case ", i + 1, ":\n");
            if (sinfo[call->spec].needssp)
                Append(code, "        f->wait.s", i, ".child.sp = f->sp;\n");
            Append(code, "        return k_task_child_drain(&f->child);\n");
        }
        Append(code, "    default: return true;\n    }\n}\n",
               "static bool ", arm, "(void *context, const k_task_waker *waker) {\n    ",
               task.frame, " *f = context;\n    return k_task_child_arm(&f->child, waker);\n}\n");
    }
    Append(code, "static void ", finish, "(void *context, k_task_state state, bool started) {\n    ",
           task.frame, " *f = context;\n");
    if (task.pending_input_cleanup)
        Append(code, "    if (state != K_TASK_SUCCEEDED && started && f->suspended) (void)",
               flow.poll_body, "(f, true);\n");
    else
        Append(code, "    if (state == K_TASK_SUCCEEDED) return;\n");
    if (auto cleanup = owner->task_cleanup) {
        const auto &ci = sinfo[cleanup];
        if (ci.hasrf || !ci.freevars.empty()) Fail(stage->sf->line, "invalid async input cleanup");
        Append(code, "    if (", task.pending_input_cleanup ? "!f->forwarded" : "!started",
               ") {\n        ", ci.cname, "(",
               PtrAbi(cleanup->argtypes[0]) ? "&" : "", "f->initial",
               ci.needssp ? ", f->sp" : "", ci.located ? ", f->at" : "", ");\n",
               task.pending_input_cleanup ? "" : "        return;\n", "    }\n");
        NoteStackCall(cleanup, owner->sf->line);
    }
    if (task.pending_input_cleanup) Append(code, "    if (state == K_TASK_SUCCEEDED) return;\n");
    else Append(code, "    if (started && f->suspended) (void)", flow.poll_body, "(f, true);\n");
    if (task.has_storage)
        Append(code, "    for (size_t i = 0; i < ", storage.size(), "; ++i) k_task_stack_close(&f->stacks[i]);\n");
    Append(code, "}\n",
           "static const k_task_ops ", task.ops, " = { ", poll, ", ", cancel, ", ", drain, ", ",
           finish, ", ", arm, " };\nstatic ", SigRet(owner), " ", name, "(", params, ") {\n",
           "    ", task.frame, " f;\n    f.initial = ", PtrAbi(initial) ? "*" : "",
           LocalName(owner->params[0]), ";\n    f.suspended = 0;\n");
    if (task.has_child) Append(code, "    f.child = (k_task_child){0};\n");
    if (task.pending_input_cleanup) Append(code, "    f.forwarded = false;\n");
    if (task.has_storage) Append(code, "    K_MEMZERO(f.stacks, sizeof(f.stacks));\n");
    if (info.needssp) Append(code, "    f.sp = k_sp;\n");
    if (info.located) Append(code, "    f.at = k_at;\n");
    Append(code, "    if (!k_task_join_local(&f, &", task.ops, ")) k_abort(K_E_TAG, ",
           LocArgs(owner->sf->line), ");\n");
    if (result) Append(code, info.cret >= 0 ? "    return f.result;\n" : "    *k_r0 = f.result;\n");
    Append(code, "}\n\n");
    NoteStackCall(stage, owner->sf->line);
    entry.end = code.size();
    spectexts.push_back(std::move(entry));
    curspec = nullptr;
    curinfo = nullptr;
}

inline string CodeGen::BindPinnedTaskLocal(VarDef *v, bool forlocal) {
    if (!TaskOwnsStack(v)) return vnames[v] = cat("f->pinned", task_flow->pinned.at(v));
    auto slot = task_flow->storage.at(v);
    auto stk = cat("f->stacks[", slot, "]");
    auto ctype = CT(v->type);
    auto pointer = cat("f->local", slot);
    L("k_task_stack_open(&", stk, ");");
    SaveBase(forlocal, stk, "", v, true);
    L(pointer, " = (", ctype, " *)", Top(stk), ";");
    Bump(stk, cat("sizeof(", ctype, ")"));
    return vnames[v] = cat("(*", pointer, ")");
}

inline TypeExpr *CodeGen::TaskOperandType(Node *n) {
    if (!task_flow) return nullptr;
    auto it = task_flow->operand_types.find(n);
    return it == task_flow->operand_types.end() ? nullptr : it->second;
}

inline string CodeGen::GenTaskOperand(Node *n, const function<string()> &evaluate) {
    auto name = T();
    FixedLocal(TaskOperandType(n), name);
    PushSc(SC_STMT);
    auto value = evaluate();
    L(name, " = ", value, ";");
    PopSc();
    task_flow->operand_names.emplace(n, name);
    return name;
}

inline CodeGen::TaskLiteralScope::TaskLiteralScope(CodeGen &cg, Node *n) : cg(cg) {
    if (!cg.task_flow || (!Is<ArrayLit>(n) && !Is<StructLit>(n)) || cg.fillvalues.count(n) ||
        cg.task_flow->prepared_literals.count(n) || !ContainsTaskAwait(n)) return;
    literal = n;
    cg.task_flow->prepared_literals.insert(n);
    auto inputs = LiteralInputs(n);
    auto last = find_if(inputs.rbegin(), inputs.rend(), [](const auto &input) {
        return ContainsTaskAwait(input.first);
    });
    // Complete the prefix through the last await before writing any of
    // the aggregate's storage. This includes tags and unused slots, and
    // permits a large destination to be allocated only after resumption.
    for (auto i = inputs.begin(); i != last.base(); ++i) {
        auto [value, type] = *i;
        auto evaluate = [&] { return cg.GenXD(value, type); };
        auto name = cg.TaskOperandType(value) ? cg.GenTaskOperand(value, evaluate)
                                             : cg.Snapshot(type, evaluate());
        Loc loc;
        loc.t = type;
        loc.val = true;
        loc.s = name;
        cg.fillvalues.emplace(value, std::move(loc));
        prepared.push_back(value);
    }
}

inline CodeGen::TaskLiteralScope::~TaskLiteralScope() {
    if (!literal) return;
    for (auto value : prepared) cg.fillvalues.erase(value);
    cg.task_flow->prepared_literals.erase(literal);
}

inline bool CodeGen::HasInlineTaskArray(TypeExpr *t) {
    return HoldsLimitedArray(t, [&](TypeExpr *ft) { return FieldRuns(ft); });
}

inline size_t CodeGen::TaskLoanCount(const vector<VarDef *> &owners, span<const TaskSlotLoan> precise) {
    auto count = owners.size() + count_if(owners.begin(), owners.end(), [&](VarDef *v) {
        return IsFrameObj(v->type) && (HasInlineTaskArray(v->type) || TaskHolderWrite(v->type));
    });
    const TaskSlotLoan *previous = nullptr;
    for (const auto &slot : precise) {
        if (!previous || slot.source != previous->source) ++count;
        previous = &slot;
    }
    return count;
}

inline void CodeGen::EmitTaskLoans(const vector<VarDef *> &owners, const string &slots, const string &task,
                                   optional<Line> resize, size_t index, span<const TaskSlotLoan> precise) {
    auto open = [&](const string &stack, const string &base, const string &bytes) {
        if (resize)
            L("k_task_check_resize_loan(", stack, ", ", base, ", ", bytes, ", ", LocArgs(*resize), ");");
        L("k_task_loan_open(&", slots, "[", index++, "], ", stack, ", ", base, ", ", bytes,
          ", ", task, ", ", resize ? "true" : "false", ");");
    };
    for (auto owner : owners) {
        auto lv = VarLoc(owner);
        auto t = lv.t;
        if (!IsResz(t)) {
            auto base = lv.val ? cat("&(", lv.s, ")") : lv.s;
            // Shrink and whole-array replacement check the header. Writes
            // to elements are covered by the selected slots below.
            auto exact = any_of(precise.begin(), precise.end(), [&](const auto &slot) { return slot.owner == owner; });
            auto extent = exact ? string("1") : SizeX(t, base);
            // A global holder's possible targets include stores made after
            // initialization, including to regions not yet allocated here.
            if (owner->isglobal && !lv.val && !IsFix(t)) extent = cat("(", base, " ? ", extent, " : 0)");
            open("NULL", base, extent);
            continue;
        }
        if (!HasInlineTaskArray(t) && !TaskHolderWrite(t)) {
            open(lv.stk, "NULL", "0");
            continue;
        }
        // The tail's elements are on the data stack. Fixed fields of a
        // frame object live in its descriptor, which has its own address.
        auto base = lv.s;
        if (IsFrameObj(t)) {
            open("NULL", cat("&(", lv.s, ")"), cat("sizeof(", lv.s, ")"));
            base = cat(FoTailHdr(t, lv.s), ".base");
        }
        open(lv.stk, base, cat("(uintptr_t)", Top(lv.stk), " - (uintptr_t)(", base, ")"));
    }
}

inline string CodeGen::TaskHolderVisitor(TypeExpr *type) {
    auto ct = CT(type);
    auto [entry, fresh] = task_holder_visitors.try_emplace(ct);
    if (!fresh) return entry->second;
    auto &name = entry->second;
    name = Unique("k_task_visit_holder");
    Append(protos, "static void ", name, "(k_task_holder_graph *, const void *);\n");
    auto body = cat("static void ", name, "(k_task_holder_graph *graph, const void *address) {\n",
                    "    const ", ct, " *value = (const ", ct, " *)address;\n");
    int next_index = 0;
    auto visit = [&](auto &self, TypeExpr *part, const string &expr, const string &indent) -> void {
        if (!TaskHolderWrite(part)) return;
        switch (part->kind) {
            case TY_REF: {
                auto target = part->ref->sub;
                if (!IsFix(target)) return;
                auto visitor = part->cq && TaskHolderWrite(target) ? TaskHolderVisitor(target) : string("NULL");
                Append(body, indent, "k_task_holder_add(graph, ", expr, ", ", FixedSize(target), ", ", visitor, ");\n");
                return;
            }
            case TY_SLICE:
                if (IsFix(part->sub))
                    Append(body, indent, "k_task_holder_add(graph, ", expr, ".data, (size_t)", expr, ".len * ",
                           FixedSize(part->sub), ", NULL);\n");
                return;
            case TY_ARRAY: {
                auto index = cat("i", next_index++);
                auto count = part->arr->akind == A_LIMITED ? cat(expr, ".len") : cat(ArrSize(part->arr));
                Append(body, indent, "for (int64_t ", index, " = 0; ", index, " < ", count, "; ++", index, ") {\n");
                self(self, part->arr->sub, cat(expr, ".e[", index, "]"), indent + "    ");
                Append(body, indent, "}\n");
                return;
            }
            case TY_ENUM: {
                auto ei = EIOf(part);
                for (size_t i = 0; i < ei->vftypes.size(); ++i) {
                    auto variant = VariantType(part, (int)i);
                    if (!TaskHolderWrite(variant)) continue;
                    Append(body, indent, "if (", expr, ".tag == ", TagConst(ei, (int)i), ") {\n");
                    self(self, variant, cat(expr, ".u.v_", Sanitize(ei->en->variants[i].name)), indent + "    ");
                    Append(body, indent, "}\n");
                }
                return;
            }
            default:
                for (const auto &run : FieldRuns(part))
                    for (size_t i = 0; i < run.ftypes->size(); ++i)
                        if (auto field = (*run.ftypes)[i])
                            self(self, field, cat(expr, ".", Sanitize((*run.fields)[i].name)), indent);
        }
    };
    visit(visit, type, "(*value)", "    ");
    Append(task_holder_code, body, "}\n\n");
    return name;
}

inline void CodeGen::EmitSharedTaskLoans(const vector<VarDef *> &owners, span<const TaskSlotLoan> precise,
                                         const string &loans, const string &task, TypeExpr *initial_type,
                                         const string &initial, const Call *suspension) {
    EmitTaskLoans(owners, loans, task, {}, 0, precise);
    if (precise.empty()) return;
    Loc frame;
    frame.t = initial_type;
    frame.s = initial;
    frame.val = true;
    auto index = TaskLoanCount(owners);
    const TaskSlotLoan *previous = nullptr;
    for (const auto &slot : precise) {
        assert(find(owners.begin(), owners.end(), slot.owner) != owners.end());
        // All alternative owners for one source share its evaluated value.
        if (previous && slot.source == previous->source) continue;
        previous = &slot;
        Loc ref;
        if (auto operand = slot.source.operand) {
            assert(suspension && task_flow);
            ref.t = suspension->task_operands.at(*operand).second;
            ref.s = cat("f->wait.s", task_flow->indices.at(suspension), ".operand", *operand);
            ref.val = true;
        } else {
            ref = slot.source.local ? VarLoc(slot.source.local) : FieldLocAt(frame, slot.source.field);
        }
        if (slot.graph) {
            auto byref = ref.t->kind == TY_REF;
            auto holder = byref ? ref.t->ref->sub : ref.t;
            auto root = byref ? ref.s : cat("&(", ref.s, ")");
            L("k_task_holder_open(&", loans, "[", index++, "], ", task, ", ", root, ", ",
              byref ? FixedSize(holder) : 0, ", ", TaskHolderVisitor(holder), ");");
            continue;
        }
        assert(ref.t->kind == TY_REF && !IsRelRef(ref.t) && IsFix(ref.t->ref->sub));
        L("k_task_loan_open(&", loans, "[", index++, "], NULL, ", ref.s,
          ", (", ref.s, " ? ", FixedSize(ref.t->ref->sub), " : 0), ", task, ", false);");
    }
}

inline void CodeGen::CheckTaskArrayShrink(const Loc &lv, Line line, const string &when) {
    if (!ast.has_task_loans) return;
    auto prefix = when.empty() ? string() : cat("if (", when, ") ");
    if (IsResz(lv.t)) {
        L(prefix, "k_task_check_shrink(", lv.stk, ", NULL, 0, ", LocArgs(line), ");");
    } else if (IsArrayKind(lv.t, A_LIMITED)) {
        auto base = lv.val ? cat("&(", lv.s, ")") : lv.s;
        L(prefix, "k_task_check_shrink(NULL, ", base, ", 1, ", LocArgs(line), ");");
    }
}

inline void CodeGen::CheckTaskFixedReplacement(const Loc &lv, Line line) {
    L("k_task_check_shrink(NULL, &(", lv.s, "), ", FixedSize(lv.t), ", ", LocArgs(line), ");");
}

inline bool CodeGen::TaskHolderWrite(TypeExpr *t) {
    return ast.has_task_holder_loans && HoldsPlainRefOf(t, [&](TypeExpr *ft) { return FieldRuns(ft); });
}

inline void CodeGen::CheckTaskHolderWrite(const Loc &lv, Line line) {
    CheckTaskHolderWrite(cat("&(", lv.s, ")"), cat(FixedSize(lv.t)), line);
}

inline void CodeGen::CheckTaskHolderWrite(const string &address, const string &bytes, Line line) {
    L("k_task_check_holder_write(", address, ", ", bytes, ", ", LocArgs(line), ");");
}

inline vector<string> CodeGen::EmitTaskFlowAwait(Call *c) {
    assert(task_flow);
    const auto i = task_flow->indices.at(c);
    const auto state = cat("f->wait.s", i, ".");
    const auto &resume = task_flow->sites[i].resume;
    auto child = c->task_await == TaskAwait::Child ? c->spec : nullptr;
    if (child || !c->args.empty()) {
        // Finish evaluating the operand, releasing its expression scratch,
        // before the site can return to the driver.
        PushSc(SC_STMT);
        if (child) GenAny(Is<Dot>(c->callee)->obj, FreshLv(state + "child.initial", child->argtypes[0]));
        else GenAny(c->args[0], FreshLv(state + "value", c->rettypes[0]));
        PopSc();
    }
    vector<StackSave> released;
    for (auto scope = cscopes.rbegin(); scope != cscopes.rend(); ++scope) {
        if (scope->task_controller)
            Fail(c->line, "a task scope cannot stay live across a structured await");
        for (auto save = scope->saves.rbegin(); save != scope->saves.rend(); ++save) {
            auto owner = save->owner;
            auto dead = owner && find(c->task_dead.begin(), c->task_dead.end(), owner) != c->task_dead.end();
            if (save->task_owned) {
                if (dead) RestoreStack(*save);
                continue;
            }
            if (!dead)
                Fail(c->line, "data stack storage cannot stay live across a structured await");
            released.push_back(*save);
        }
    }
    vector<pair<size_t, string>> locals;
    for (size_t j = 0; j < c->task_values.size(); ++j) {
        auto v = c->task_values[j];
        if (v->task_storage || v->task_pinned) continue;
        auto local = VarLoc(v);
        assert(local.val);
        locals.emplace_back(j, local.s);
        L(state, "v", j, " = ", local.s, ";");
    }
    for (size_t j = 0; j < c->task_operands.size(); ++j)
        L(state, "operand", j, " = ", task_flow->operand_names.at(c->task_operands[j].first), ";");
    for (const auto &save : released) RestoreStack(save);
    EmitSharedTaskLoans(c->task_shared_owners, c->task_slot_loans, "f->loans", "NULL",
                        child ? child->argtypes[0] : nullptr, state + "child.initial", c);
    L("f->suspended = ", i + 1, ";");
    if (child) {
        const auto &emitted = taskcode.at(child);
        if (emitted.suspends) L(state, "child.suspended = 0;");
        if (emitted.has_child) L(state, "child.child = (k_task_child){0};");
        if (emitted.pending_input_cleanup) L(state, "child.forwarded = false;");
        if (emitted.has_storage) L("K_MEMZERO(", state, "child.stacks, sizeof(", state, "child.stacks));");
        if (sinfo[child].located) {
            auto at = CallerLoc(c->line);
            L(state, "child.at = ", at, ";");
        }
        L("if (!k_task_child_init(&f->child, &", state, "child, &", emitted.ops,
          ")) return K_TASK_POLL_ERROR;");
        if (c->decorator_await && task_flow->pending_input_cleanup) L("f->forwarded = true;");
    } else {
        L("return K_TASK_POLL_PENDING;");
    }
    L(resume, ":;");
    for (const auto &save : released) L(save.base, " = ", Top(save.stack), ";");
    for (const auto &[j, name] : locals) L(name, " = ", state, "v", j, ";");
    L("if (cancelling) {");
    ind++;
    EmitExitRestores(0);
    auto loan_count = TaskLoanCount(c->task_shared_owners, c->task_slot_loans);
    if (loan_count) L("k_task_loans_close(f->loans, ", loan_count, ");");
    MarkFlush();
    L("return K_TASK_POLL_ERROR;");
    ind--;
    L("}");
    if (child) {
        if (sinfo[child].needssp) L(state, "child.sp = k_sp;");
        auto step = T();
        L("k_task_poll ", step, " = k_task_child_poll(&f->child);");
        L("if (", step, " != K_TASK_POLL_READY) return ", step, ";");
        stackcalls[curspec].push_back({ child, 0, c->line });  // Local stacks were released.
    }
    if (loan_count) L("k_task_loans_close(f->loans, ", loan_count, ");");
    for (size_t j = 0; j < c->task_operands.size(); ++j)
        L(task_flow->operand_names.at(c->task_operands[j].first), " = ", state, "operand", j, ";");
    L("f->suspended = 0;");
    return c->rettypes.empty() ? vector<string>() : vector<string>{ state + (child ? "child.result" : "value") };
}

}  // namespace klang
