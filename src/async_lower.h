// AST lowering for async functions and decorator task layers.
#pragma once

namespace klang {

[[noreturn]] inline void AsyncLoweringError(Ast &ast, Line line, const string &message) {
    Diagnostic d;
    d.where = LocOf(line);
    d.message = message;
    d.text = ast.DiagnosticText(d);
    throw CompileError { d.text, { d } };
}

// The synthesized join checks the task body with its original lexical
// scopes intact. A non-suspending task needs only a plain body call.
inline void LowerAsyncFunction(Ast &ast, SFunction *sf) {
    auto ns = sf->ns;
    auto body = sf->body;
    auto continuation = sf->decorator_result_source ? sf->decorator_next : nullptr;
    if (sf->IsGeneric() || sf->rets.size() > 1 || (sf->name == "main" && sf->ns.empty()))
        AsyncLoweringError(ast, sf->line, "async fn needs typed parameters, at most one result, and a synchronous entry point");
    auto named_call = [](Node *n, string_view name) -> Call * {
        auto call = Is<Call>(n);
        auto id = call ? Is<Ident>(call->callee) : nullptr;
        return id && id->name == name ? call : nullptr;
    };
    auto empty_body = [&] {
        auto block = ast.New<Block>(body->line);
        block->end = body->end;
        return block;
    };
    auto line = sf->line;
    auto prefix = cat("task#", ast.structs.size());
    auto frame = [&](string_view suffix, const vector<Param> &fields) {
        auto st = new SStruct();
        ast.structs.push_back(st);
        st->name = st->qname = ast.Intern(cat(prefix, suffix));
        st->line = line;
        st->task_frame = true;
        if (fields.empty()) st->fields.push_back({ "task#empty", line, {}, ast.inttypes[IS_U8] });
        for (auto &p : fields) st->fields.push_back({ p.name, p.line, {}, p.type });
        return ast.StructOf(st, {}, line);
    };
    auto literal = [&](TypeExpr *type, const vector<Param> &fields) {
        auto lit = ast.New<StructLit>(line, type);
        if (fields.empty()) lit->inits.push_back({ "task#empty", ast.New<IntLit>(line, 0) });
        for (auto &p : fields) lit->inits.push_back({ p.name, ast.New<Ident>(p.line, p.name, ns) });
        return lit;
    };
    auto helper = [&](string_view suffix, string_view receiver, TypeExpr *arg,
                      vector<TypeExpr *> rets) {
        auto fn = new SFunction();
        ast.functions.push_back(fn);
        fn->name = fn->qname = ast.Intern(cat(prefix, suffix));
        fn->line = line;
        fn->end = sf->end;
        fn->params.push_back({ receiver, line, arg });
        fn->rets = std::move(rets);
        fn->has_rets = !fn->rets.empty();
        fn->body = empty_body();
        return fn;
    };
    auto bind = [&](Block *where, const vector<Param> &fields) {
        for (auto &p : fields) {
            auto v = ast.New<VarDecl>(p.line, p.isvar);
            v->type = p.type;
            v->names.push_back(p.name);
            v->inits.push_back(ast.New<Dot>(p.line, ast.New<Ident>(line, kClosureEnv, ns), p.name, ns));
            where->stmts.push_back(v);
        }
    };
    auto reparent = [&](SFunction *stage) {
        function<void(Node *)> walk = [&](Node *n) {
            if (auto fn = Is<FnDecl>(n)) {
                if (fn->sf->outer == sf) fn->sf->outer = stage;
                return;
            }
            n->Children(walk);
        };
        stage->body->Children(walk);
    };
    auto clone = [&](Node *node, ExpansionClone &context) {
        auto saved = ast.expansionclone;
        ast.expansionclone = &context;
        Node *copy;
        try { copy = node->Clone(ast); }
        catch (...) { ast.expansionclone = saved; throw; }
        ast.expansionclone = saved;
        return copy;
    };
    auto cleanup = [&](string_view suffix, TypeExpr *type, const vector<Param> &fields,
                       const vector<Defer *> &defers) {
        auto fn = helper(suffix, kClosureEnv, type, {});
        fn->task_stage = true;
        fn->checkfree = sf->checkfree;
        bind(fn->body, fields);
        for (auto d : defers) {
            ExpansionClone context;
            fn->body->stmts.push_back(clone(d, context));
        }
        reparent(fn);
        return fn;
    };

    auto initial = frame("#initial", sf->params);
    auto join = helper("#join", "task#value", initial, sf->rets);
    join->task_join = true;
    initial->struc->st->task_join = join;
    vector<Defer *> leading_defers;
    for (auto stmt : body->stmts) {
        auto d = Is<Defer>(stmt);
        if (!d) break;
        leading_defers.push_back(d);
    }
    if (continuation) {
        join->decorator_result_source = sf->decorator_result_source;
        // Inputs belong to this pending continuation until a child is
        // started. An unstarted child runs the original input cleanup.
        if (auto prototype = sf->decorator_result_source->task_cleanup) {
            auto fn = helper("#unstarted", kClosureEnv, initial, {});
            fn->task_stage = true;
            fn->checkfree = prototype->checkfree;
            ExpansionClone context;
            context.functions.emplace(prototype, fn);
            fn->body = Is<Block>(clone(prototype->body, context));
            join->task_cleanup = fn;
        }
    } else if (!leading_defers.empty()) {
        join->task_cleanup = cleanup("#unstarted", initial, sf->params, leading_defers);
    }
    auto finish = [&] {
        sf->async_body = body;
        sf->async_rets = std::move(sf->rets);
        sf->body = empty_body();
        sf->body->tail = literal(initial, sf->params);
        sf->rets = { initial };
        sf->has_rets = true;
        ast.has_ownership = true;
    };

    bool structured = continuation != nullptr;
    function<void(Node *, bool)> visit = [&](Node *n, bool lower) {
        if (!n || Is<FnDecl>(n) || Is<FunVal>(n) || Is<Defer>(n)) return;
        if (auto binding = Is<VarDecl>(n); binding && binding->inits.size() == 1 &&
            named_call(binding->inits[0], kTaskAwait) &&
            (binding->names.size() != 1 || binding->byref || binding->reusable || binding->isconst)) {
            AsyncLoweringError(ast, binding->line, "an awaited result needs a single value binding");
        }
        auto await = named_call(n, kTaskAwait);
        if (!await) {
            n->Children([&](Node *child) { visit(child, lower); });
            return;
        }
        auto operand = await->args[0];
        visit(operand, lower);
        if (!lower) {
            structured = true;
            return;
        }
        if (auto next = continuation ? named_call(operand, "next") : nullptr) {
            if (next->trailing || !next->tyargs.empty() || !next->args.empty())
                AsyncLoweringError(ast, next->line, "an async decorator continuation is awaited as await next()");
            await->decorator_await = true;
            next->task_stage = continuation;
            // The wrapper cannot name the input fields. Read them only
            // when transferring the continuation to a checked child call.
            for (const auto &p : sf->params)
                next->args.push_back(ast.New<Dot>(next->line,
                    ast.New<Ident>(next->line, kClosureEnv, ns), p.name, ns));
        }
        if (auto yield = named_call(operand, "yield_now")) {
            if (yield->trailing || !yield->tyargs.empty() || yield->args.size() > 1)
                AsyncLoweringError(ast, await->line, "yield_now expects at most one argument");
            await->task_await = TaskAwait::Yield;
            await->args = yield->args;
        } else {
            await->task_await = TaskAwait::Child;
            await->callee = ast.New<Dot>(await->line, operand, kTaskAwaitResult, ns);
            await->args.clear();
        }
    };
    visit(body, false);
    if (structured) {
        auto stage = helper("#flow", kClosureEnv, initial, sf->rets);
        stage->task_stage = stage->task_flow = true;
        stage->task_final_of = join;
        stage->decorator_layer = sf->decorator_layer;
        stage->decorator_next = continuation;
        stage->checkfree = sf->checkfree;
        ExpansionClone context;
        auto lowered = Is<Block>(clone(body, context));
        visit(lowered, true);
        if (!continuation) bind(stage->body, sf->params);
        auto bound = (int)stage->body->stmts.size();
        stage->body->stmts.insert(stage->body->stmts.end(), lowered->stmts.begin(), lowered->stmts.end());
        stage->body->tail = lowered->tail;
        stage->body->brokenat = lowered->brokenat;
        if (stage->body->brokenat >= 0) stage->body->brokenat += bound;
        reparent(stage);
        auto call = ast.New<Call>(line, ast.New<Ident>(line, stage->name, string_view()));
        call->task_stage = stage;
        call->args.push_back(ast.New<Ident>(line, join->params[0].name, ns));
        join->body->tail = call;
        finish();
        return;
    }
    auto stage = helper("#stage0", kClosureEnv, initial, sf->rets);
    stage->task_stage = true;
    stage->task_final_of = join;
    stage->checkfree = sf->checkfree;
    bind(stage->body, sf->params);
    stage->body->stmts.insert(stage->body->stmts.end(), body->stmts.begin(), body->stmts.end());
    stage->body->tail = body->tail;
    reparent(stage);
    auto call = ast.New<Call>(line, ast.New<Ident>(line, stage->name, string_view()));
    call->task_stage = stage;
    call->args.push_back(ast.New<Ident>(line, join->params[0].name, ns));
    join->body->tail = call;
    finish();
}

}  // namespace klang
