// Backward liveness over a typed structured task. A suspension has two
// successors: its normal continuation and the defers that cancellation
// would run there. The operand is evaluated before either successor.
#pragma once

namespace klang {

class TaskLiveness {
    using Vars = set<VarDef *>;
    using Point = size_t;
    struct Step {
        Vars reads, written, live;
        vector<Point> next, predecessors;
    };
    struct Jump {
        Point on_break, on_continue;
        size_t defers;
        bool loop;
    };
    struct OperandLoan {
        const Roots *roots;
        const char *description;
    };

    TypeCheck &tc;
    FnSpec *spec;
    vector<Step> graph { Step {} };  // Point zero exits the task.
    vector<Jump> jumps;
    vector<const Vars *> defers;
    map<Defer *, Vars> deferred_reads;
    vector<pair<Call *, Point>> suspensions;
    vector<VarDef *> sequence_loans;
    vector<pair<Node *, TypeExpr *>> operands;
    unordered_map<Node *, bool> awaits;
    unordered_map<Node *, OperandLoan> operand_loans;
    Vars local_declarations;

    bool Suspends(Node *n) {
        if (!n) return false;
        if (auto it = awaits.find(n); it != awaits.end()) return it->second;
        auto c = Is<Call>(n);
        auto found = c && c->task_await != TaskAwait::None;
        RunChildren(n, [&](Node *child) { if (!found) found = Suspends(child); });
        awaits.emplace(n, found);
        return found;
    }

    void Read(Vars &vars, VarDef *v) {
        if (v && v->ownerspec == spec && !v->isglobal) vars.insert(v);
    }

    void ImplicitReads(Node *n, Vars &vars) {
        set<TypeExpr *> seen;
        NodeNamedArrays(n, seen, [&](VarDef *v, bool length) {
            if (length) Read(vars, v);
        });
        auto c = Is<Call>(n);
        if (!c) return;
        auto captures = [&](FnSpec *callee) {
            if (!callee) return;
            for (const auto &r : callee->envreads) Read(vars, c->FreeVarArg(r.var));
        };
        captures(c->spec);
        for (auto callee : c->dispatch) captures(callee);
        for (auto callee : c->scope_tasks) captures(callee);
        for (auto &[t, callee] : c->fmtspecs) captures(callee);
    }

    void ReadTree(Node *n, Vars &vars) {
        if (!n) return;
        if (auto component = tc.TaskRootComponent(n)) {
            Read(vars, component);
            ImplicitReads(n, vars);
            return;
        }
        if (auto id = Is<Ident>(n)) Read(vars, id->vdef);
        ImplicitReads(n, vars);
        RunChildren(n, [&](Node *child) { ReadTree(child, vars); });
    }

    Point Add(vector<Point> next, Vars reads = {}, Vars written = {}) {
        auto point = graph.size();
        graph.push_back({ std::move(reads), std::move(written), {}, std::move(next), {} });
        return point;
    }

    Point Transfer(Point after, Vars reads, Vars written = {}) {
        if (reads.empty() && written.empty()) return after;
        return Add({ after }, std::move(reads), std::move(written));
    }

    Point Cleanup(Point after, size_t keep) {
        Vars reads;
        for (size_t i = keep; i < defers.size(); ++i)
            reads.insert(defers[i]->begin(), defers[i]->end());
        return Transfer(after, std::move(reads));
    }

    Point BlockEntry(Block *b, Point after) {
        auto first = defers.size();
        for (auto stmt : b->stmts) {
            auto d = Is<Defer>(stmt);
            if (!d) continue;
            auto [it, fresh] = deferred_reads.try_emplace(d);
            if (fresh) ReadTree(d->body, it->second);
            defers.push_back(&it->second);
        }
        auto point = Entry(b->tail, Cleanup(after, first));
        for (auto it = b->stmts.rbegin(); it != b->stmts.rend(); ++it) {
            if (Is<Defer>(*it)) defers.pop_back();
            else point = Entry(*it, point);
        }
        assert(defers.size() == first);
        return point;
    }

    Point LoopEntry(Block *body, Node *condition, Point after, bool can_skip, Vars binders = {}) {
        auto head = Add({});
        jumps.push_back({ after, head, defers.size(), true });
        auto iteration = Entry(body, head);
        jumps.pop_back();
        iteration = Transfer(iteration, {}, std::move(binders));
        auto test = can_skip ? Add({ iteration, after }) : iteration;
        auto condition_entry = Entry(condition, test);
        graph[head].next = { condition_entry };
        return head;
    }

    Point JumpEntry(bool again, Node *value) {
        auto target = find_if(jumps.rbegin(), jumps.rend(), [&](const auto &j) { return !again || j.loop; });
        assert(target != jumps.rend());
        return Entry(value, Cleanup(again ? target->on_continue : target->on_break, target->defers));
    }

    // The value of each operand preceding a suspension has already been
    // evaluated. It stays live until the operation consumes that value.
    Point OperandsEntry(const vector<Node *> &nodes, const vector<TypeExpr *> &types, Point after,
                        Node *resolved = nullptr) {
        auto later = find_if(nodes.rbegin(), nodes.rend(), [&](Node *n) { return Suspends(n); });
        auto saved = later == nodes.rend() ? size_t(0) : (size_t)(later.base() - nodes.begin() - 1);
        for (size_t i = 0; i < saved; ++i) {
            auto t = types[i];
            if (!t || tc.ClassOf(t) != SC_FIXED || (!tc.IsFlat(t) && !operand_loans.count(nodes[i])))
                tc.Error(nodes[i], "an operand saved across await needs a fixed-size type without references");
            operands.emplace_back(nodes[i], t);
        }
        for (size_t i = nodes.size(); i-- > 0;) {
            if (i < saved) operands.pop_back();
            if (nodes[i] != resolved) after = Entry(nodes[i], after);
        }
        return after;
    }

    bool FixedPlace(Node *n, VarDef *&root, vector<Index *> &indices) {
        auto t = n->exprtype;
        if (!t || tc.ClassOf(t) != SC_FIXED || !tc.IsFlat(t)) return false;
        if (auto component = tc.TaskRootComponent(n)) {
            root = component;
            return true;
        }
        if (auto id = Is<Ident>(n)) {
            auto v = id->vdef;
            if (!v || tc.ClassOf(v->type) != SC_FIXED || !tc.IsFlat(v->type)) return false;
            root = v;
            return true;
        }
        if (auto d = Is<Dot>(n); d && d->IsField()) return FixedPlace(d->obj, root, indices);
        if (auto i = Is<Index>(n)) {
            auto a = i->obj->exprtype;
            if (!a) return false;
            auto slice = a->kind == TY_SLICE && i->task_slice_owner;
            auto array = a->kind == TY_ARRAY && a->arr->akind == A_FIXED && FixedPlace(i->obj, root, indices);
            if (!slice && !array) return false;
            indices.push_back(i);
            return true;
        }
        return false;
    }

    Point PlaceEntry(Node *place, Node *rhs, bool compound, Point after) {
        vector<Index *> indices;
        VarDef *root = nullptr;
        if (!FixedPlace(place, root, indices))
            tc.Error(place, "an indexed or field location across await needs named fixed storage or a stable slice");
        vector<Node *> nodes;
        vector<TypeExpr *> types;
        for (auto i : indices) {
            i->task_place = true;
            if (i->obj->exprtype->kind == TY_SLICE) {
                operand_loans.emplace(i->obj, OperandLoan { i->task_slice_owner.get(), "indexed slice" });
                nodes.push_back(i->obj);
                types.push_back(i->obj->exprtype);
            }
            nodes.push_back(i->idx);
            types.push_back(tc.ast.inttypes[IS_I64]);
        }
        if (compound) {
            nodes.push_back(place);
            types.push_back(place->exprtype);
        }
        if (rhs) nodes.push_back(rhs);
        // The location is reconstructed against the same storage. The
        // root remains live even when only a field or element is replaced.
        Vars reads;
        Read(reads, root);
        return OperandsEntry(nodes, types, Transfer(after, std::move(reads)), place);
    }

    Point LiteralEntry(Node *n, Point after) {
        auto t = n->exprtype;
        if (!tc.IsFlat(t) || (t->kind == TY_STRUCT && t->struc->st->isroot))
            tc.Error(n, "a literal containing await needs a type without references or root ownership");
        auto inputs = LiteralInputs(n);
        auto last = find_if(inputs.rbegin(), inputs.rend(), [&](const auto &input) { return Suspends(input.first); });
        assert(last != inputs.rend());
        if (tc.ClassOf(last->second) != SC_FIXED || !tc.IsFlat(last->second))
            tc.Error(last->first, "a suspended literal initializer needs a fixed-size type without references");
        vector<Node *> nodes;
        vector<TypeExpr *> types;
        for (auto [value, type] : inputs) {
            nodes.push_back(value);
            types.push_back(type);
        }
        return OperandsEntry(nodes, types, after);
    }

    Point CallEntry(Call *c, Point after) {
        if (c->task_await != TaskAwait::None) {
            // Treat the cancellation reads as part of the suspension node,
            // so they do not leak backwards past an intervening assignment.
            Vars reads;
            ImplicitReads(c, reads);
            auto point = Add({ after, Cleanup(0, 0) }, std::move(reads));
            c->task_operands = operands;
            for (auto owner : sequence_loans) tc.RecordTaskLoan(c->task_shared_owners, owner);
            suspensions.push_back({ c, point });
            auto operand = c->task_await == TaskAwait::Child || !c->args.empty() ? c->FirstArg() : nullptr;
            return Entry(operand, point);
        }
        auto args = c->ArgNodes();
        if (Suspends(c)) {
            Vars reads;
            ImplicitReads(c, reads);
            after = Transfer(after, std::move(reads));
            if (c->builtin == B_ASSERT || c->builtin == B_EXIT || (c->builtin == B_PRINT && args.size() == 1))
                return Entry(args[0], after);
            if (!c->spec || c->builtin >= 0 || c->fvbody || c->native_task || c->task_scope ||
                c->scope_controller || !c->dispatch.empty() || Suspends(c->defaultinit) ||
                (!Is<Dot>(c->callee) && Suspends(c->callee)))
                tc.Error(c, "await in an argument needs a direct function call");
            for (const auto &[arg, type, roots, field] : c->task_arg_roots)
                operand_loans.emplace(arg, OperandLoan { &roots, "argument loan" });
            args.resize(c->spec->argtypes.size());
            return OperandsEntry(args, c->spec->argtypes, after);
        }
        if (c->fvbody) {
            after = Entry(c->fvbody, after);
            Vars params;
            for (auto v : c->fvparams) Read(params, v);
            after = Transfer(after, {}, std::move(params));
        }
        if (c->defaultinit) after = Entry(c->defaultinit, after);
        Vars reads;
        ImplicitReads(c, reads);
        after = Transfer(after, std::move(reads));
        for (auto it = args.rbegin(); it != args.rend(); ++it) after = Entry(*it, after);
        if (auto callee = Is<Ident>(c->callee); callee && callee->vdef) return Entry(callee, after);
        return after;
    }

    Point Entry(Node *n, Point after) {
        if (!n) return after;
        if (auto b = Is<Block>(n)) return BlockEntry(b, after);
        if (auto c = Is<Call>(n)) return CallEntry(c, after);
        if (auto d = Is<VarDecl>(n)) {
            Vars written;
            for (auto v : d->defs) {
                Read(written, v);
                if (!d->inits.empty() || tc.ClassOf(v->type) == SC_FIXED) Read(local_declarations, v);
                for (auto component : v->components) {
                    Read(written, component);
                    Read(local_declarations, component);
                }
            }
            auto entry = Transfer(after, {}, std::move(written));
            for (auto it = d->inits.rbegin(); it != d->inits.rend(); ++it) entry = Entry(*it, entry);
            return entry;
        }
        if (auto a = Is<Assign>(n); a && !a->pointee) {
            auto id = Is<Ident>(a->lval);
            auto v = id ? id->vdef : nullptr;
            auto t = v ? v->type : nullptr;
            if (t && ((a->op == T_DOTASSIGN && t->kind == TY_REF && !IsRelRef(t)) ||
                      (a->op == T_ASSIGN && t->kind == TY_SLICE))) {
                // The descriptor slot is named directly. Its address needs
                // no temporary that would be lost while evaluating the RHS.
                a->task_place = Suspends(a->rhs);
                Vars written;
                Read(written, v);
                return Entry(a->rhs, Transfer(after, {}, std::move(written)));
            }
            // Stores to aggregates and through views can require the old storage.
            if (t && (t->kind == TY_BOOL || t->kind == TY_FLT ||
                      (t->kind == TY_INT && t->intstorage != IS_VARINT && !IsIndexT(t)))) {
                if (a->op == T_ASSIGN || Suspends(a->rhs)) {
                    Vars written;
                    Read(written, v);
                    auto store = Transfer(after, {}, std::move(written));
                    if (a->op == T_ASSIGN) return Entry(a->rhs, store);
                    return OperandsEntry({ a->lval, a->rhs }, { t }, store);
                }
            }
        }
        if (auto branch = Is<IfExpr>(n)) {
            auto yes = Entry(branch->thenb, after);
            auto no = Entry(branch->elseb, after);
            return Entry(branch->cond, Add({ yes, no }));
        }
        if (auto match = Is<MatchExpr>(n)) {
            vector<Point> arms;
            for (auto &arm : match->arms) {
                Vars binder;
                Read(binder, arm.binder);
                arms.push_back(Transfer(Entry(arm.body, after), {}, std::move(binder)));
            }
            return Entry(match->scrutinee, Add(std::move(arms)));
        }
        if (auto loop = Is<While>(n)) return LoopEntry(loop->body, loop->cond, after, true);
        if (auto loop = Is<LoopExpr>(n)) return LoopEntry(loop->body, nullptr, after, false);
        if (auto loop = Is<ForLoop>(n)) {
            Vars binders;
            Read(binders, loop->vdef);
            Read(binders, loop->idxdef);
            if (loop->task_shared_owner) sequence_loans.push_back(loop->task_shared_owner);
            auto head = LoopEntry(loop->body, nullptr, after, true, std::move(binders));
            if (loop->task_shared_owner) sequence_loans.pop_back();
            // A sequence is revisited on each iteration. Its roots can be
            // needed again even when the rest of the body never names them.
            if (loop->iterkind == IK_ARRAY || loop->iterkind == IK_SLICE) {
                auto range = loop->task_resumable ? Is<SliceExpr>(loop->iter) : nullptr;
                // A range over a borrowed array reference is sampled as
                // a slice. Its owner stays live through the input loan.
                if (!range || range->obj->exprtype->kind != TY_REF)
                    ReadTree(range ? range->obj : loop->iter, graph[head].reads);
            }
            return Entry(loop->iter, head);
        }
        if (auto block = Is<EarlyBlock>(n)) {
            jumps.push_back({ after, 0, defers.size(), false });
            auto entry = Entry(block->body, after);
            jumps.pop_back();
            return entry;
        }
        if (auto r = Is<Return>(n)) {
            auto entry = Cleanup(0, 0);
            for (auto it = r->vals.rbegin(); it != r->vals.rend(); ++it) entry = Entry(*it, entry);
            return entry;
        }
        if (auto b = Is<Break>(n)) return JumpEntry(false, b->val);
        if (Is<Continue>(n)) return JumpEntry(true, nullptr);
        if (auto b = Is<Binary>(n); b && (b->op == T_ANDAND || b->op == T_OROR)) {
            auto rhs = Entry(b->right, after);
            return Entry(b->left, Add({ rhs, after }));
        }
        if (Suspends(n)) {
            if (auto a = Is<Assign>(n); a && a->op != T_DOTASSIGN && !a->pointee) {
                a->task_place = true;
                return PlaceEntry(a->lval, a->rhs, a->op != T_ASSIGN, after);
            }
            if (auto x = Is<IncDec>(n)) return PlaceEntry(x->lval, nullptr, false, after);
            if (auto i = Is<Index>(n)) {
                if (Suspends(i->idx)) return PlaceEntry(i, nullptr, false, after);
                return Entry(i->obj, Entry(i->idx, after));
            }
            if (Is<StructLit>(n) || Is<ArrayLit>(n)) return LiteralEntry(n, after);
            if (auto b = Is<Binary>(n)) {
                auto t = b->left->exprtype;
                auto identity = b->op == T_DOTEQ || b->op == T_DOTNEQ;
                if (!identity && IsPlainRef(t)) t = t->ref->sub;
                // A syntactic null test only evaluates the non-null operand.
                if (!identity && Is<NullLit>(b->left)) return Entry(b->right, after);
                Vars reads;
                ImplicitReads(b, reads);
                return OperandsEntry({ b->left, b->right }, { t }, Transfer(after, std::move(reads)));
            }
            if (auto r = Is<RangeExpr>(n))
                return OperandsEntry({ r->lo, r->hi }, { r->lo->exprtype }, after);
            if (auto u = Is<Unary>(n); u && u->op != T_BITAND) return Entry(u->child, after);
            if (auto a = Is<AsCast>(n)) {
                Vars reads;
                ImplicitReads(a, reads);
                return Entry(a->child, Transfer(after, std::move(reads)));
            }
            if (auto d = Is<Dot>(n)) return d->obj ? Entry(d->obj, after) : after;
            tc.Error(n, "await is not supported in this expression position");
        }
        // Conservatively retain the inputs of ordinary expressions.
        Vars reads;
        ReadTree(n, reads);
        return Transfer(after, std::move(reads));
    }

    void Solve() {
        deque<Point> queue;
        vector<bool> queued(graph.size(), true);
        for (Point i = 0; i < graph.size(); ++i) {
            for (auto next : graph[i].next) graph[next].predecessors.push_back(i);
            queue.push_back(i);
        }
        while (!queue.empty()) {
            auto i = queue.front();
            queue.pop_front();
            queued[i] = false;
            auto &step = graph[i];
            Vars live = step.reads;
            for (auto next : step.next)
                for (auto v : graph[next].live)
                    if (!step.written.count(v)) live.insert(v);
            if (live == step.live) continue;
            step.live.swap(live);
            for (auto pred : step.predecessors)
                if (!queued[pred]) {
                    queue.push_back(pred);
                    queued[pred] = true;
                }
        }
    }

    static void KeepLocal(VarDef *v, Vars &live, vector<VarDef *> &work) {
        if (live.insert(v).second) work.push_back(v);
    }

    void KeepBorrowOwner(Call *call, const Roots &roots, TypeExpr *type, string_view loan,
                         Vars &live, vector<VarDef *> &work, vector<VarDef *> &task_loans) {
        auto message = cat(loan, " across await needs a stable storage owner");
        if (roots.unknown || (roots.None() && IsRefOrSlice(type) && !IsOptional(type))) tc.Error(call, message);
        auto holder = tc.TaskInputHolderRef(type) ? type->ref->sub : nullptr;
        auto holder_contents = tc.TaskHolderStorage(type);
        for (const auto &origin : roots.alts) {
            if (tc.TaskInputLifetime(origin.root, spec)) continue;
            auto owner = tc.TaskBorrowOwner(call, origin, message, holder, holder_contents);
            if (!owner) continue;
            auto seal = holder || (holder_contents && tc.TaskHolderStorage(owner->type));
            if (owner->isglobal) {
                if (seal) tc.RecordGlobalTaskLoans(call, owner, task_loans);
                else if (tc.TaskSharedStorage(owner->type))
                    tc.RecordTaskLoan(task_loans, owner);
                continue;
            }
            if (!local_declarations.count(owner) ||
                find(call->task_values.begin(), call->task_values.end(), owner) == call->task_values.end())
                tc.Error(call, message);
            KeepLocal(owner, live, work);
            if (tc.ClassOf(owner->type) == SC_FIXED) owner->task_pinned = true;
            if (seal) tc.RecordTaskLoan(task_loans, owner);
        }
    }

    // A whole-owner use passes the descriptor to ordinary K code. Its
    // pointers must continue to name the same component slots after a poll.
    void KeepRoot(Call *call, VarDef *owner, Vars &live, vector<VarDef *> &work) {
        owner->task_pinned = true;
        for (auto component : owner->components) {
            if (!component) continue;
            auto t = component->type;
            if (!tc.TaskResidentStorage(t) && !(IsRefOrSlice(t) && !IsRelRef(t)) && !tc.TaskHolderStorage(t))
                tc.Error(call, cat("root component ", component->name, " needs task-resident storage"));
            KeepLocal(component, live, work);
            if (tc.ClassOf(t) == SC_FIXED) component->task_pinned = true;
        }
    }

public:
    TaskLiveness(TypeCheck &tc, FnSpec *spec) : tc(tc), spec(spec) {}

    void Check() {
        Entry(spec->body, 0);
        Solve();
        for (auto [call, point] : suspensions) {
            auto &live = graph[point].live;
            auto retained = live;
            vector<VarDef *> work(live.begin(), live.end());
            auto child = call->task_await == TaskAwait::Child ? Is<Call>(call->FirstArg()) : nullptr;
            const auto whole_loans = call->task_shared_owners;
            vector<TypeCheck::SavedTaskBorrow> saved;
            for (size_t i = 0; i < call->task_operands.size(); ++i) {
                const auto &[operand, type] = call->task_operands[i];
                auto loan = operand_loans.find(operand);
                if (loan == operand_loans.end()) continue;
                KeepBorrowOwner(call, *loan->second.roots, type, loan->second.description,
                                live, work, call->task_shared_owners);
                saved.push_back({ type, loan->second.roots, { .operand = i } });
            }
            if (child && child->task_borrow_consumed)
                for (const auto &[arg, type, roots, field] : child->task_arg_roots)
                    KeepBorrowOwner(call, roots, type, "child task argument", live, work,
                                    call->task_shared_owners);
            unordered_map<VarDef *, const Roots *> loans;
            for (const auto &[v, roots] : call->task_loan_roots) loans.emplace(v, &roots);
            for (size_t i = 0; i < work.size(); ++i) {
                auto v = work[i];
                auto t = v->type;
                if (!t) continue;
                if (tc.ClassOf(t) == SC_ROOT && local_declarations.count(v)) {
                    KeepRoot(call, v, live, work);
                    for (auto component : v->components)
                        if (component) retained.insert(component);
                }
                auto loan = loans.find(v);
                if (loan == loans.end()) continue;
                auto kind = t->kind == TY_SLICE ? "slice " : t->kind == TY_REF ? "reference " : "borrowed fields of ";
                KeepBorrowOwner(call, *loan->second, t, cat(kind, v->name), live, work,
                                call->task_shared_owners);
                if (t->kind == TY_REF) v->task_saved_ref = true;
            }
            for (const auto &[v, roots] : call->task_loan_roots)
                if (retained.count(v)) saved.push_back({ v->type, &roots, { .local = v } });
            call->task_slot_loans = tc.PreciseTaskSlots(child, call->task_shared_owners, whole_loans, call, saved);
            erase_if(call->task_values, [&](VarDef *v) {
                if (!live.count(v)) {
                    call->task_dead.push_back(v);
                    return true;
                }
                auto t = v->type;
                if (v->task_pinned) return false;
                if (tc.ClassOf(t) != SC_FIXED && !IsRefOrSlice(t) && local_declarations.count(v) &&
                    (tc.TaskResidentStorage(t) || (loans.count(v) && !v->reusable))) {
                    v->task_storage = true;
                    return false;
                }
                if (loans.count(v) && tc.ClassOf(t) == SC_FIXED) return false;
                auto flat_handle = t->kind == TY_STRUCT &&
                    (t->struc->st->task_frame || (t->owns && tc.GetStructInst(t)->flat));
                if (tc.ClassOf(t) != SC_FIXED || (!tc.IsFlat(t) && !flat_handle))
                    tc.Error(call, cat("task frame value ", v->name, " needs a fixed-size type without references"));
                return false;
            });
        }
    }
};

}  // namespace klang
