'use strict';

// Run with --extensionTestsPath=<this file> in an isolated, trusted workspace.
// The workspace is a disposable test fixture, not the user's K project.
const vscode = require('vscode');
const assert = require('node:assert/strict');
const path = require('node:path');
const fs = require('node:fs/promises');
const { compilerPath } = require('../src/config');

async function until(predicate, description) {
    const deadline = Date.now() + 15000;
    while (Date.now() < deadline) {
        if (await predicate()) return;
        await new Promise(resolve => setTimeout(resolve, 50));
    }
    throw new Error(`Timed out: ${description}`);
}

async function run() {
    const folder = vscode.workspace.workspaceFolders?.[0];
    assert.ok(folder && path.basename(folder.uri.fsPath) === 'klang-extension-fixture', 'Use an isolated klang-extension-fixture workspace');
    assert.ok(vscode.workspace.isTrusted);
    await fs.mkdir(path.join(folder.uri.fsPath, '.vscode'), { recursive: true });
    await fs.writeFile(path.join(folder.uri.fsPath, '.vscode', 'tasks.json'), JSON.stringify({
        version: '2.0.0', tasks: [{ type: 'klang', label: 'Fixture check', action: 'check', file: 'main.k', group: 'test', problemMatcher: '$klang' }]
    }));
    const root = path.resolve(__dirname, '../..');
    const config = vscode.workspace.getConfiguration('klang', folder.uri);
    await config.update('compilerPath', process.env.K_TEST_COMPILER || compilerPath('', root), vscode.ConfigurationTarget.WorkspaceFolder);
    await config.update('entryFile', 'main.k', vscode.ConfigurationTarget.WorkspaceFolder);
    await config.update('checkOnSave', false, vscode.ConfigurationTarget.WorkspaceFolder);
    // The checks of saved files first; the language server at the end.
    await config.update('useLanguageServer', false, vscode.ConfigurationTarget.Workspace);
    const main = vscode.Uri.joinPath(folder.uri, 'main.k');
    const helper = vscode.Uri.joinPath(folder.uri, 'helper.k');
    await fs.writeFile(main.fsPath, 'import helper;\nfn main() { helper(); }\n');
    await fs.writeFile(helper.fsPath, 'fn helper() { let x = 1; x = 2; }\n');
    const doc = await vscode.workspace.openTextDocument(main);
    assert.equal(doc.languageId, 'klang');
    await vscode.window.showTextDocument(doc);
    const extension = vscode.extensions.getExtension('aardappel.klang-language');
    assert.ok(extension);
    await extension.activate();
    await vscode.commands.executeCommand('klang.check');
    const problems = () => vscode.languages.getDiagnostics(helper).filter(item => item.source === 'K');
    await until(() => problems().length === 1, 'compiler error in imported file');
    await new Promise(resolve => setTimeout(resolve, 600));
    assert.equal(problems().length, 1, 'late filesystem notifications must preserve manual check results');
    assert.ok(problems()[0].range.end.character > problems()[0].range.start.character);
    assert.ok(problems()[0].relatedInformation.some(item => item.location.uri.fsPath === main.fsPath));

    const symbols = await vscode.commands.executeCommand('vscode.executeDocumentSymbolProvider', main);
    assert.ok(symbols.some(item => item.name === 'main'));
    const links = await vscode.commands.executeCommand('vscode.executeLinkProvider', main);
    assert.ok(links.some(link => link.target.fsPath === helper.fsPath));
    const tasks = await vscode.tasks.fetchTasks({ type: 'klang' });
    for (const action of ['check', 'run', 'build', 'generateC']) {
        const task = tasks.find(item => item.definition.action === action);
        assert.ok(task, action);
        assert.ok(task.execution instanceof vscode.ProcessExecution);
        assert.ok(task.execution.args.includes(main.fsPath));
        if (action === 'run') {
            assert.equal(task.group.id, vscode.TaskGroup.Build.id);
            assert.ok(task.execution.args.includes('--jit'));
        } else assert.notEqual(task.group?.id, vscode.TaskGroup.Build.id);
    }
    const configured = tasks.find(task => task.name === 'Fixture check');
    assert.ok(configured, 'configured task is resolved');
    assert.ok(configured.execution.args.includes('check'));
    assert.equal(configured.group.id, vscode.TaskGroup.Test.id, 'keep explicit task groups');
    const matchers = extension.packageJSON.contributes.problemMatchers;
    const matcher = new RegExp(matchers[0].pattern.regexp);
    assert.equal(matcher.exec(`${helper.fsPath}:1: error: test`)[1], helper.fsPath);

    const helperDoc = await vscode.workspace.openTextDocument(helper);
    await vscode.window.showTextDocument(helperDoc);
    await config.update('checkOnSave', true, vscode.ConfigurationTarget.WorkspaceFolder);
    await until(() => problems().length === 1, 'enabling checks rechecks the open entry file');
    const replace = async text => {
        const edit = new vscode.WorkspaceEdit();
        edit.replace(helper, new vscode.Range(helperDoc.positionAt(0), helperDoc.positionAt(helperDoc.getText().length)), text);
        assert.ok(await vscode.workspace.applyEdit(edit));
    };
    await replace('fn helper() { var x = 1; x = 2; }\n');
    await new Promise(resolve => setTimeout(resolve, 300));
    assert.equal(problems().length, 1, 'an edit keeps the last error until a recheck');
    assert.ok(await helperDoc.save());
    await until(() => problems().length === 0, 'save replaces the stale error');
    await vscode.commands.executeCommand('klang.check');
    assert.equal(problems().length, 0);
    await replace('fn helper() { let x = 1; x = 2; }\n');
    assert.ok(await helperDoc.save());
    await until(() => problems().length === 1, 'save rechecks configured entry file');
    await config.update('checkOnSave', false, vscode.ConfigurationTarget.WorkspaceFolder);
    await until(() => problems().length === 0, 'disabling checks clears errors');

    // Run the real compiler, both through commands and the native Run picker.
    // Start with an unsaved fix: Run must save the module before launching main.
    await replace('fn helper() { print("K_JIT_TEST"); }\n');
    const runTask = async (trigger, expectedFile = main.fsPath) => {
        let finished;
        const listener = vscode.tasks.onDidEndTaskProcess(event => {
            if (event.execution.task.definition.type === 'klang' && event.execution.task.definition.action === 'run') finished = event;
        });
        try {
            await trigger();
            await until(() => finished !== undefined, 'JIT task completed');
            assert.equal(finished.exitCode, 0, 'JIT compiler/program must succeed');
            assert.ok(finished.execution.task.execution.args.includes('--jit'));
            assert.ok(finished.execution.task.execution.args.includes(expectedFile));
            await until(() => !vscode.tasks.taskExecutions.some(execution => execution.task.definition.type === 'klang'), 'JIT task released');
        } finally {
            listener.dispose();
        }
    };
    await runTask(() => vscode.commands.executeCommand('klang.run'));
    assert.equal(helperDoc.isDirty, false);
    assert.equal((await fs.readdir(folder.uri.fsPath)).filter(file => file.endsWith('.c')).length, 0);
    // Task-group defaults are owned by tasks.json, not the provider API. Check
    // the documented default configuration with the native build command.
    await fs.writeFile(path.join(folder.uri.fsPath, '.vscode', 'tasks.json'), JSON.stringify({
        version: '2.0.0', tasks: [{
            type: 'klang', label: 'Default K JIT', action: 'run', file: 'main.k',
            group: { kind: 'build', isDefault: true }, problemMatcher: '$klang'
        }]
    }));
    await until(async () => (await vscode.tasks.fetchTasks({ type: 'klang' })).some(task => task.name === 'Default K JIT' && task.group.isDefault), 'default JIT task configuration');
    await vscode.window.showTextDocument(helperDoc);
    await runTask(() => vscode.commands.executeCommand('workbench.action.tasks.build'));
    // Launch configurations must also work without an active source editor.
    await vscode.commands.executeCommand('workbench.action.closeAllEditors');
    const withoutEditor = await vscode.tasks.fetchTasks({ type: 'klang' });
    assert.ok(withoutEditor.some(task => task.definition.action === 'run' && task.group.id === vscode.TaskGroup.Build.id));
    await runTask(() => vscode.debug.startDebugging(folder, { type: 'klang', request: 'launch', name: 'JIT test', noDebug: true }));
    const other = vscode.Uri.joinPath(folder.uri, 'other program.k');
    await fs.writeFile(other.fsPath, 'fn main() { print("OTHER_JIT_TEST"); }\n');
    await runTask(() => vscode.debug.startDebugging(folder, {
        type: 'klang', request: 'launch', name: 'JIT explicit file', program: '${workspaceFolder}/other program.k'
    }), other.fsPath);
    assert.equal(vscode.debug.activeDebugSession, undefined, 'JIT runs do not create a debugger session');
    console.log('K extension host: editor features, diagnostics, build tasks and JIT run configurations passed.');

    // The language server, where the compiler serves one: it answers the
    // editor's hover for the program, in place of the checks of saved files.
    await config.update('useLanguageServer', true, vscode.ConfigurationTarget.Workspace);
    const server = () => extension.exports.server();
    const deadline = Date.now() + 15000;
    while (server().status !== 'running' && Date.now() < deadline) await new Promise(resolve => setTimeout(resolve, 100));
    if (server().status !== 'running') {
        console.log('K extension host: the compiler serves no language server; its checks were skipped.');
        return;
    }
    const mainDoc = await vscode.workspace.openTextDocument(main);
    await vscode.window.showTextDocument(mainDoc);
    await until(async () => {
        const hovers = await vscode.commands.executeCommand('vscode.executeHoverProvider', main, new vscode.Position(1, 12));
        return (hovers || []).some(hover => hover.contents.some(content => String(content.value ?? content).includes('fn helper')));
    }, 'the language server answers a hover on the call of helper');
    console.log('K extension host: the language server answered a hover.');
    // A file opened while the server analyzes another is highlighted and
    // hinted before any edit. The editor asks for both as it opens, the
    // server answers once the other analysis is done but before the file's
    // own, and the editor asks again when the server says to. The client's
    // middleware sees what the editor itself asked for, where the provider
    // commands would ask anew.
    const busy = vscode.Uri.joinPath(folder.uri, 'busy.k');
    const functions = Array.from({ length: 3000 }, (_, k) => k);
    await fs.writeFile(busy.fsPath, functions.map(k => `fn g${k}(x) { let y = x + ${k}; y * 2 }\n`).join('') +
        `fn main() { var s = 0;${functions.map(k => ` s = g${k}(s);`).join('')} print(s); }\n`);
    const fresh = vscode.Uri.joinPath(folder.uri, 'fresh.k');
    await fs.writeFile(fresh.fsPath, 'fn main() { let n = 1 + 2; print(n); }\n');
    const middleware = server().client.clientOptions.middleware;
    const isFresh = document => document.uri.fsPath === fresh.fsPath;
    const shown = { tokens: false, hints: false };
    middleware.provideDocumentSemanticTokens = async (document, token, next) => {
        const tokens = await next(document, token);
        if (isFresh(document) && tokens?.data.length) shown.tokens = true;
        return tokens;
    };
    middleware.provideInlayHints = async (document, range, token, next) => {
        const hints = await next(document, range, token);
        if (isFresh(document) && hints?.length) shown.hints = true;
        return hints;
    };
    try {
        await vscode.workspace.openTextDocument(busy);
        await vscode.window.showTextDocument(fresh);
        await until(() => shown.tokens && shown.hints, 'the editor gets semantic tokens and inlay hints for a file just opened');
    } finally {
        delete middleware.provideDocumentSemanticTokens;
        delete middleware.provideInlayHints;
    }
    console.log('K extension host: a file just opened got semantic tokens and inlay hints.');
    // The code lenses: a generic function's instantiations, which a quick
    // pick lists, and above a `main`, Run, which runs its own file rather
    // than the entry file.
    const lensed = vscode.Uri.joinPath(folder.uri, 'lensed.k');
    await fs.writeFile(lensed.fsPath, 'fn twice(x) { x + x }\nfn main() { print(twice(1)); print(twice(1.5)); }\n');
    await vscode.window.showTextDocument(await vscode.workspace.openTextDocument(lensed));
    let lenses = [];
    await until(async () => {
        lenses = await vscode.commands.executeCommand('vscode.executeCodeLensProvider', lensed) || [];
        return lenses.some(lens => lens.command?.title === 'Run');
    }, 'the language server gives a Run lens above main');
    const count = lenses.find(lens => lens.command?.title === '2 instantiations');
    assert.ok(count, JSON.stringify(lenses.map(lens => lens.command)));
    assert.equal(count.command.command, 'klang.showInstantiations');
    assert.deepEqual(count.command.arguments, ['twice', ['twice(literal i64)', 'twice(literal f64)']]);
    await vscode.commands.executeCommand(count.command.command, ...count.command.arguments);
    await vscode.commands.executeCommand('workbench.action.closeQuickOpen');
    const run = lenses.find(lens => lens.command?.title === 'Run');
    assert.equal(run.command.command, 'klang.run');
    await runTask(() => vscode.commands.executeCommand(run.command.command, ...run.command.arguments), lensed.fsPath);
    console.log('K extension host: the code lenses listed instantiations and ran their file.');
}

module.exports = { run };
