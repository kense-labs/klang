// Check resource transfers on typed specializations, before optimization.
// Calls and their arguments already have a single resolved evaluation order.
#pragma once

namespace klang {

class ResourceFlow {
    using TaskSlot = pair<VarDef *, size_t>;
    struct Owner {
        int scope = 0;
        int cleanup = 0;
        bool moved = false;
        bool operator==(const Owner &) const = default;
    };
    struct Flow {
        bool live = true;
        map<VarDef *, Owner> owners;
        set<TaskSlot> consumed_tasks {};
        set<TaskSlot> active_resize_tasks {};
        bool continuation_used = false;
    };
    struct Jump {
        int scope;
        bool loop;
        bool moves_result;
        Flow entry;
        Flow exits { false, {} };
    };

    TypeCheck &tc;
    Flow flow;
    vector<Jump> jumps;
    vector<set<VarDef *>> defer_reads;
    vector<Defer *> active_defers;
    map<VarDef *, int> task_scope_levels;
    int scope = 1;
    int pending = 0;   // Moved operands whose receiving call or literal is still being assembled.

    bool Owns(TypeExpr *t) { return tc.HasOwnership(t); }
    bool Owns(Node *n) {
        if (auto id = Is<Ident>(n); id && id->vdef) return Owns(id->vdef->type);
        return n && Owns(n->exprtype);
    }

    Owner &RequireHeld(Ident *id) {
        auto it = flow.owners.find(id->vdef);
        if (it == flow.owners.end())
            tc.Error(id, cat("owned resource ", id->name, " must be used in its owning scope"));
        if (it->second.moved) tc.Error(id, cat("owned resource ", id->name, " has already moved"));
        return it->second;
    }

    void Compare(const Flow &a, const Flow &b, Node *at) {
        if (!a.live || !b.live || a.owners == b.owners) return;
        for (auto &[v, state] : a.owners) {
            auto other = b.owners.find(v);
            if (other == b.owners.end() || other->second != state)
                tc.Error(at, cat("ownership of ", v->name, " differs between control-flow paths"));
        }
        tc.Error(at, "ownership differs between control-flow paths");
    }

    Flow Join(Flow a, Flow b, Node *at) {
        Compare(a, b, at);
        if (a.live && b.live) {
            a.consumed_tasks.insert(b.consumed_tasks.begin(), b.consumed_tasks.end());
            a.active_resize_tasks.insert(b.active_resize_tasks.begin(), b.active_resize_tasks.end());
            a.continuation_used |= b.continuation_used;
        }
        return a.live ? std::move(a) : std::move(b);
    }

    void Backedge(const Flow &entry, const Flow &exit, Node *at) {
        Compare(entry, exit, at);
        if (entry.live && exit.live && entry.consumed_tasks != exit.consumed_tasks)
            tc.Error(at, "a loop can consume the same scoped task more than once");
        if (entry.live && exit.live && entry.continuation_used != exit.continuation_used)
            tc.Error(at, "a loop can consume the same async decorator continuation more than once");
    }

    void CheckPending(Node *at) {
        if (pending) tc.Error(at, "control flow can leave an owned argument or capture before transfer");
    }

    void Leave(int remaining, Node *at) {
        if (!flow.live) return;
        auto leaves = [&](const TaskSlot &slot) {
            return task_scope_levels.at(slot.first) > remaining;
        };
        erase_if(flow.consumed_tasks, leaves);
        erase_if(flow.active_resize_tasks, leaves);
        for (auto it = flow.owners.begin(); it != flow.owners.end();) {
            auto &[v, state] = *it;
            if (state.cleanup > remaining) {
                state.moved = true;
                state.cleanup = 0;
            }
            if (state.scope <= remaining) { ++it; continue; }
            if (!state.moved)
                tc.Error(at, cat("owned resource ", v->name,
                                " is still held at scope exit; move it, return it, or release it with defer"));
            it = flow.owners.erase(it);
        }
    }

    void BlockBody(Block *b, bool move_result) {
        auto first_defer = active_defers.size();
        scope++;
        for (auto stmt : b->stmts) Visit(stmt);
        Visit(b->tail, move_result);
        Leave(--scope, b);
        active_defers.resize(first_defer);
    }

    void Declaration(VarDecl *vd) {
        auto moves = any_of(vd->defs.begin(), vd->defs.end(), [&](VarDef *v) { return Owns(v->type); });
        if (!vd->destruct_fields.empty() && moves)
            tc.Error(vd, "destructuring owned resources is not supported");
        if (!moves) {
            for (auto init : vd->inits) Visit(init);
            return;
        }
        auto call = vd->inits.size() == 1 ? Is<Call>(vd->inits[0]) : nullptr;
        if (call && call->task_stage && call->task_stage->task_child_start) {
            // A synthesized await prefix yields the saved state and its
            // child. The scheduler acquires both in one transition.
            assert(vd->defs.size() == 2 && Owns(vd->defs[1]->type));
            ResourceCall(call, false);
            if (flow.live)
                for (auto def : vd->defs)
                    if (Owns(def->type)) flow.owners.emplace(def, Owner { scope });
            return;
        }
        if (vd->isvar || vd->defs.size() != 1 || vd->inits.size() != 1 || vd->byref)
            tc.Error(vd, "bind an owned resource with a single initialized let");
        auto init = vd->inits[0];
        auto dot = Is<Dot>(init);
        auto env = dot ? Is<Ident>(dot->obj) : nullptr;
        // Closure entry unpacks its private environment into its capture
        // locals. Every owned field is transferred exactly once by parsing.
        if (!env || env->name != kClosureEnv) Visit(init, true);
        if (flow.live) flow.owners.emplace(vd->defs[0], Owner { scope });
    }

    void DeferBody(Defer *d) {
        auto saved = flow;
        defer_reads.emplace_back();
        BlockBody(d->body, false);
        auto reads = std::move(defer_reads.back());
        defer_reads.pop_back();
        auto after = std::move(flow);
        flow = std::move(saved);
        if (d->erronly) {
            for (auto &[v, before] : flow.owners) {
                auto released = after.live && !before.moved && after.owners.at(v).moved;
                if (reads.count(v) && !released)
                    tc.Error(d, cat("errdefer borrows ", v->name, " without taking responsibility for its release"));
            }
            return;
        }
        if (after.live) flow.consumed_tasks.insert(after.consumed_tasks.begin(), after.consumed_tasks.end());
        for (auto &[v, before] : flow.owners) {
            auto released = after.live && !before.moved && after.owners.at(v).moved;
            if (reads.count(v) && !released)
                tc.Error(d, cat("defer borrows ", v->name, " without taking responsibility for its release"));
            if (released) before.cleanup = scope;
        }
        active_defers.push_back(d);
    }

    void CheckScopedAccess(Node *at, const Roots &roots) {
        for (const auto &[controller, slot] : flow.active_resize_tasks)
            for (auto owner : controller->task_scope->scope_resize_owners[slot])
                if (tc.TaskResizeOverlaps(owner, roots))
                    tc.Error(at, cat(owner->name, " is exclusively borrowed by a scoped task"));
    }

    // Field paths can load a stored reference or read the storage containing
    // it. A direct root component starts a distinct storage path.
    bool CheckScopedPath(Node *at, Node *path) {
        if (flow.active_resize_tasks.empty()) return false;
        if (auto v = tc.nodevals.find(path)) {
            CheckScopedAccess(at, *v);
            if (v->hasslot) CheckScopedAccess(at, v->slot);
            if (v->holderset) CheckScopedAccess(at, v->contents);
        }
        if (auto component = tc.TaskRootComponent(path)) {
            CheckScopedAccess(at, tc.RootsOf(component));
            return true;
        }
        return false;
    }

    void CheckScopedCallee(Node *at, FnSpec *spec) {
        if (flow.active_resize_tasks.empty()) return;
        vector<VarDef *> names;
        vector<Node *> paths;
        tc.NamedOutside(spec, names, true, &paths);
        for (auto path : paths) {
            for (;;) {
                if (CheckScopedPath(at, path)) break;
                auto field = Is<Dot>(path);
                if (!field) break;
                path = field->obj;
            }
        }
    }

    void ResourceCall(Call *c, bool move_result) {
        if (auto spec = c->spec; spec && spec->sf->async_body && !c->task_borrow_consumed &&
            any_of(spec->argtypes.begin(), spec->argtypes.end(), [&](TypeExpr *t) {
                return tc.TaskInputBorrow(t) || tc.TaskInputHolder(t);
            }))
            tc.Error(c, "a task with borrowed arguments needs direct await, join, or task_scope");
        const auto old_pending = pending;
        vector<Ident *> borrowed;
        auto args = c->ArgNodes();
        auto controller = c->task_scope ? c->fvparams[0] : nullptr;
        if (controller) task_scope_levels[controller] = scope + 1;
        for (size_t i = 0; i < args.size() && flow.live; ++i) {
            auto a = args[i];
            auto spec = c->spec;
            if (spec && i < spec->sf->params.size() && spec->sf->params[i].borrows) {
                auto id = Is<Ident>(a);
                if (!id || !id->vdef || !id->vdef->type->owns)
                    tc.Error(a, "a foreign borrow requires the name of a live owned handle");
                RequireHeld(id);
                borrowed.push_back(id);
                if (!defer_reads.empty()) defer_reads.back().insert(id->vdef);
                continue;
            }
            auto moves = c->task_scope || c->native_task || (spec && i < spec->argtypes.size() && Owns(spec->argtypes[i]));
            Visit(a, moves);
            pending += moves;
            if (controller && !c->scope_resize_owners[i].empty())
                flow.active_resize_tasks.emplace(controller, i);
        }
        pending = old_pending;
        if (!flow.live) return;
        if (c->task_scope) {
            c->scope_slot_loans.resize(c->args.size());
            for (size_t i = 0; i < c->args.size(); ++i) {
                auto task = Is<Call>(c->args[i]);
                tc.AddGlobalTaskReferents(task, c->scope_loan_owners[i]);
                c->scope_slot_loans[i] = tc.PreciseTaskSlots(task, c->scope_loan_owners[i]);
            }
        } else if (c->task_await == TaskAwait::None && c->spec && c->spec->sf->task_join) {
            if (auto member = Is<Dot>(c->callee)) {
                auto task = Is<Call>(member->obj);
                tc.AddGlobalTaskReferents(task, c->task_shared_owners);
                c->task_slot_loans = tc.PreciseTaskSlots(task, c->task_shared_owners);
            }
        }
        for (auto id : borrowed) RequireHeld(id);
        if (c->spec) CheckScopedCallee(c, c->spec);
        for (auto spec : c->dispatch) CheckScopedCallee(c, spec);
        for (const auto &fmt : c->fmtspecs) CheckScopedCallee(c, fmt.second);
        for (auto task : c->scope_tasks) CheckScopedCallee(c, task);
        if (c->task_await != TaskAwait::None) {
            if (c->decorator_await) {
                if (flow.continuation_used) tc.Error(c, "the async decorator continuation has already been consumed");
                flow.continuation_used = true;
            }
            CheckPending(c);
            for (auto defer : active_defers)
                if (defer->task_external_effect)
                    tc.Error(defer, "defer across await cannot resize external K storage or replace external references");
            for (const auto &[v, state] : flow.owners)
                if (!state.moved && !state.cleanup)
                    tc.Error(c, cat("owned value ", v->name, " needs a defer to release it on cancellation"));
            erase_if(c->task_values, [&](auto v) {
                auto it = flow.owners.find(v);
                return it != flow.owners.end() && it->second.moved;
            });
        }
        if (c->defaultinit) Visit(c->defaultinit, move_result);
        if (c->spec && !c->spec->needs.empty()) {
            CheckPending(c);
            auto saved = flow;
            Leave(0, c);
            flow = std::move(saved);
        }
        if (c->fvbody) {
            for (auto v : c->fvparams)
                if (Owns(v->type)) tc.Error(c, "pass owned arguments through a concrete callable");
            BlockBody(c->fvbody, move_result);
        } else if (!c->spec && move_result && !c->defaultinit && !c->native_task) {
            tc.Error(c, "an owned result requires a direct owning call");
        }
        if (c->spec && !c->spec->record.mayreturn) flow.live = false;
    }

    void Capture(StructLit *sl) {
        auto previous = pending;
        auto st = sl->sinst ? sl->sinst->st : nullptr;
        if (!st || (!sl->Closure() && !st->task_join && !st->task_saved))
            tc.Error(sl, "an owned handle must come from an owning producer");
        for (size_t i = 0; i < sl->inits.size() && flow.live; ++i) {
            auto moves = Owns(sl->sinst->ftypes[sl->fieldindices[i]]);
            if (st->task_saved && moves) {
                // This literal is synthesized at the end of the start
                // stage. A pending defer transfers with its owner and is
                // checked again in both the resume and cancellation stages.
                auto id = Is<Ident>(sl->inits[i].val);
                assert(id);
                RequireHeld(id).cleanup = 0;
            }
            Visit(sl->inits[i].val, moves);
            pending += moves;
        }
        pending = previous;
    }

    void Loop(Node *n, Block *body, Node *condition, bool may_skip, bool move_result) {
        auto entry = flow;
        Visit(condition);
        if (!flow.live) return;
        Compare(entry, flow, n);
        auto skipped = flow;  // The condition still ran when the body is skipped.
        jumps.push_back({ scope, true, move_result, entry });
        BlockBody(body, false);
        Backedge(entry, flow, n);  // A normal iteration reaching the back edge.
        auto exits = std::move(jumps.back().exits);
        jumps.pop_back();
        flow = may_skip ? Join(std::move(skipped), std::move(exits), n) : std::move(exits);
    }

    void JumpOut(Node *n, bool again, Node *value) {
        auto i = (int)jumps.size() - 1;
        while (again && i >= 0 && !jumps[i].loop) --i;
        if (i < 0) tc.Error(n, "resource flow cannot resolve this jump");
        Visit(value, jumps[i].moves_result);
        if (!flow.live) return;
        CheckPending(n);
        auto &target = jumps[i];
        Leave(target.scope, n);
        if (again) Backedge(target.entry, flow, n);
        else target.exits = Join(std::move(target.exits), flow, n);
        flow.live = false;
    }

    void Visit(Node *n, bool move = false) {
        if (!n || !flow.live) return;
        if (auto c = Is<Call>(n); c && c->scope_controller) {
            if (c->scope_operation == TaskScopeOperation::WaitFor) Visit(c->args[0]);
            if (c->scope_operation == TaskScopeOperation::Join || c->scope_operation == TaskScopeOperation::Cancel) {
                TaskSlot slot { c->scope_controller, c->scope_slot };
                if (!flow.consumed_tasks.insert(slot).second)
                    tc.Error(c, "this scoped task may already have been consumed");
                flow.active_resize_tasks.erase(slot);
            }
            return;
        }
        if (Is<Ident>(n) || Is<Index>(n) || Is<Dot>(n)) {
            if (CheckScopedPath(n, n)) return;
        }
        if (auto d = Is<Dot>(n)) {
            auto env = Is<Ident>(d->obj);
            if (env && env->name == kClosureEnv && env->vdef && env->vdef->type->kind == TY_STRUCT &&
                env->vdef->type->struc->st->task_frame) return;
        }
        if (auto b = Is<Block>(n)) { BlockBody(b, move); return; }
        if (auto vd = Is<VarDecl>(n)) { Declaration(vd); return; }
        if (auto d = Is<Defer>(n)) { DeferBody(d); return; }
        if (auto r = Is<Return>(n)) {
            for (size_t i = 0; i < r->vals.size(); ++i) {
                auto target = r->targetspec;
                Visit(r->vals[i], target && i < target->rets.size() && Owns(target->rets[i]));
            }
            CheckPending(n);
            Leave(0, n);
            flow.live = false;
            return;
        }
        if (auto x = Is<IfExpr>(n)) {
            Visit(x->cond);
            auto entry = flow;
            Visit(x->thenb, move);
            auto yes = std::move(flow);
            flow = std::move(entry);
            Visit(x->elseb, move);
            flow = Join(std::move(yes), std::move(flow), n);
            return;
        }
        if (auto m = Is<MatchExpr>(n)) {
            Visit(m->scrutinee);
            auto entry = flow;
            Flow exits { false, {} };
            for (auto &arm : m->arms) {
                flow = entry;
                Visit(arm.body, move);
                exits = Join(std::move(exits), std::move(flow), n);
            }
            flow = std::move(exits);
            return;
        }
        if (auto w = Is<While>(n)) { Loop(n, w->body, w->cond, true, false); return; }
        if (auto f = Is<ForLoop>(n)) { Visit(f->iter); Loop(n, f->body, nullptr, true, false); return; }
        if (auto l = Is<LoopExpr>(n)) { Loop(n, l->body, nullptr, false, move); return; }
        if (auto b = Is<EarlyBlock>(n)) {
            jumps.push_back({ scope, false, move, flow });
            BlockBody(b->body, move);
            flow = Join(std::move(flow), std::move(jumps.back().exits), n);
            jumps.pop_back();
            return;
        }
        if (auto b = Is<Break>(n)) { JumpOut(n, false, b->val); return; }
        if (Is<Continue>(n)) { JumpOut(n, true, nullptr); return; }
        if (auto b = Is<Binary>(n); b && (b->op == T_ANDAND || b->op == T_OROR)) {
            Visit(b->left);
            auto skip = flow;
            Visit(b->right);
            flow = Join(std::move(skip), std::move(flow), n);
            return;
        }

        if (Owns(n) != move)
            tc.Error(n, move ? "an ownership transfer requires an owned value"
                             : "an owned value must be moved or passed to a foreign borrow");
        if (auto id = Is<Ident>(n); id && move) {
            auto &state = RequireHeld(id);
            if (state.cleanup) tc.Error(id, cat("owned resource ", id->name, " is already claimed by defer"));
            state.moved = true;
            return;
        }
        if (auto c = Is<Call>(n)) { ResourceCall(c, move); return; }
        if (auto sl = Is<StructLit>(n); sl && move) { Capture(sl); return; }
        if (move) tc.Error(n, "this expression cannot transfer ownership");
        n->Children([&](Node *child) { Visit(child); });
    }

public:
    explicit ResourceFlow(TypeCheck &tc) : tc(tc) {}

    void Check(FnSpec *spec) {
        if (spec->sf->task_foreign) return;  // The terminal callback consumes the descriptor.
        if (!spec->sf->task_child_start && spec->rets.size() > 1 && any_of(spec->rets.begin(), spec->rets.end(),
                                          [&](TypeExpr *t) { return Owns(t); }))
            tc.Error(spec->sf->line, "return an owned value as the sole result");
        if (!spec->body) return;
        for (auto p : spec->params) {
            if (!Owns(p->type) || p->name == kClosureEnv) continue;
            if (p->isvar || spec->sf->isthread || spec->sf->isexport)
                tc.Error(p->line, "owned parameters require an immutable K parameter");
            flow.owners.emplace(p, Owner { scope });
        }
        if (spec->sf->isexport && !spec->rets.empty() && Owns(spec->rets[0]))
            tc.Error(spec->sf->line, "an export cannot return an owned resource");
        BlockBody(spec->body, !spec->rets.empty() && Owns(spec->rets[0]));
        Leave(0, spec->body);
    }

    void CheckGlobal(VarDecl *g) {
        for (auto v : g->defs)
            if (Owns(v->type)) tc.Error(v->line, "globals cannot hold owned resources");
        for (auto init : g->inits) Visit(init);
    }
};

inline void TypeCheck::CheckOwnedProgram() {
    for (auto g : ast.globals) ResourceFlow(*this).CheckGlobal(g);
    for (auto sp : ast.fnspecs) {
        if (sp->failed || sp->sf->broken || sp->sf->brokensig) continue;
        ResourceFlow(*this).Check(sp);
        if (sp->sf->task_flow) TaskLiveness(*this, sp).Check();
    }
}

} // namespace klang
