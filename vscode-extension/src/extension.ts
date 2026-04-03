/**
 * C-Prime VS Code Extension — extension.ts
 * ==========================================
 * Entry point. Wires up all providers, commands, and status bar.
 *
 * Registered commands:
 *   cprime.runFile         — Compile + run current .cp file
 *   cprime.checkFile       — Run cpg on current file
 *   cprime.buildProject    — Run cppm build
 *   cprime.openTutorial    — Open tutorial webview
 *   cprime.openReleases    — Open releases webview
 *   cprime.checkForUpdates — Run cppm update in terminal
 *   cprime.newFile         — Create a new .cp file from template
 */

import * as vscode from 'vscode';
import * as cp     from 'child_process';
import * as path   from 'path';
import * as os     from 'os';
import * as fs     from 'fs';

import { CprimeHoverProvider }   from './hover';
import { getTutorialHtml, LESSONS } from './tutorial';
import { createReleasesPanel }   from './releases';

// ─── Types ────────────────────────────────────────────────────────────────────

interface CpgDiagnostic {
    file:     string;
    line:     number;
    column:   number;
    severity: 'error' | 'warning' | 'info' | 'hint';
    code:     string;
    message:  string;
    hint?:    string;
    cwe?:     string;
}

// ─── Globals ──────────────────────────────────────────────────────────────────

const diagnosticCollection = vscode.languages.createDiagnosticCollection('cprime');
let   statusBarItem: vscode.StatusBarItem;
let   outputChannel: vscode.OutputChannel;

// ─── Config Helpers ───────────────────────────────────────────────────────────

function cfg<T>(key: string, def: T): T {
    return vscode.workspace.getConfiguration('cprime').get<T>(key, def);
}

function cpcPath()  { return cfg<string>('compiler.path', 'cpc'); }
function cpgPath()  { return cfg<string>('guard.path',    'cpg'); }
function cppmPath() { return cfg<string>('pkgman.path',  'cppm'); }

// ─── Status Bar ───────────────────────────────────────────────────────────────

function initStatusBar(context: vscode.ExtensionContext) {
    statusBarItem = vscode.window.createStatusBarItem(vscode.StatusBarAlignment.Left, 100);
    statusBarItem.text        = '$(shield) C-Prime';
    statusBarItem.tooltip     = 'C-Prime Guard — click to run cpg on this file';
    statusBarItem.command     = 'cprime.checkFile';
    statusBarItem.backgroundColor = undefined;
    context.subscriptions.push(statusBarItem);

    // Only show for .cp files
    vscode.window.onDidChangeActiveTextEditor(ed => {
        if (ed && ed.document.languageId === 'cprime') statusBarItem.show();
        else statusBarItem.hide();
    }, null, context.subscriptions);

    if (vscode.window.activeTextEditor?.document.languageId === 'cprime') {
        statusBarItem.show();
    }
}

// ─── Output Channel ───────────────────────────────────────────────────────────

function getOutput(): vscode.OutputChannel {
    if (!outputChannel) {
        outputChannel = vscode.window.createOutputChannel('C-Prime');
    }
    return outputChannel;
}

// ─── cpg Diagnostics ─────────────────────────────────────────────────────────

function runGuard(document: vscode.TextDocument) {
    if (document.languageId !== 'cprime') return;

    const strict = cfg<boolean>('guard.strictMode', false);
    const args   = [document.fileName, '--report=json'];
    if (strict) args.push('--strict');

    const cmd = `${cpgPath()} ${args.map(a => `"${a}"`).join(' ')}`;

    cp.exec(cmd, { timeout: 30000 }, (err, stdout, stderr) => {
        try {
            const raw: CpgDiagnostic[] = JSON.parse(stdout || '[]');
            const diags: vscode.Diagnostic[] = raw.map(d => {
                const line = Math.max(0, d.line - 1);
                const col  = Math.max(0, d.column - 1);
                const range = new vscode.Range(line, col, line, col + 80);
                const sev =
                    d.severity === 'error'   ? vscode.DiagnosticSeverity.Error   :
                    d.severity === 'warning' ? vscode.DiagnosticSeverity.Warning :
                    d.severity === 'hint'    ? vscode.DiagnosticSeverity.Hint    :
                                               vscode.DiagnosticSeverity.Information;

                const diag  = new vscode.Diagnostic(range, d.message, sev);
                diag.code   = d.code;
                diag.source = 'cpg';
                if (d.hint || d.cwe) {
                    const detail = [d.hint, d.cwe ? `[${d.cwe}]` : ''].filter(Boolean).join(' ');
                    diag.relatedInformation = [
                        new vscode.DiagnosticRelatedInformation(
                            new vscode.Location(document.uri, range),
                            detail
                        )
                    ];
                }
                return diag;
            });
            diagnosticCollection.set(document.uri, diags);
        } catch {
            // If cpg isn't installed yet, silently skip
        }
    });
}

// ─── Run File ─────────────────────────────────────────────────────────────────

async function runCurrentFile() {
    const editor = vscode.window.activeTextEditor;
    if (!editor) {
        vscode.window.showWarningMessage('No active C-Prime file.');
        return;
    }
    if (editor.document.isDirty) {
        await editor.document.save();
    }

    const file    = editor.document.fileName;
    const tmpBin  = path.join(os.tmpdir(), `cprime_run_${Date.now()}`);
    const optimize = cfg<boolean>('compiler.optimizeOnBuild', false);
    const compileCmd = `${cpcPath()} "${file}" -o "${tmpBin}"${optimize ? ' -O' : ''}`;

    const terminal = vscode.window.createTerminal({
        name: 'C-Prime',
        env:  { TERM: 'xterm-256color' }
    });
    terminal.show(false);

    // Compile then run, clean up temp binary
    terminal.sendText(`${compileCmd} && "${tmpBin}"; rm -f "${tmpBin}"`);
}

// ─── Check File (cpg) ─────────────────────────────────────────────────────────

function checkCurrentFile() {
    const editor = vscode.window.activeTextEditor;
    if (!editor) return;
    if (editor.document.languageId !== 'cprime') {
        vscode.window.showInformationMessage('cpg only works on C-Prime (.cp) files.');
        return;
    }

    runGuard(editor.document);

    // Show in output channel too
    const out = getOutput();
    out.clear();
    out.show(true);

    const cmd = `${cpgPath()} "${editor.document.fileName}"`;
    const proc = cp.exec(cmd, { timeout: 30000 });
    proc.stdout?.on('data', d => out.append(d));
    proc.stderr?.on('data', d => out.append(d));
    proc.on('close', code => {
        out.appendLine('');
        out.appendLine(`--- cpg exited with code ${code} ---`);
        if (code === 0) {
            statusBarItem.text = '$(check) C-Prime';
            setTimeout(() => { statusBarItem.text = '$(shield) C-Prime'; }, 3000);
        } else {
            statusBarItem.text = '$(error) C-Prime';
            setTimeout(() => { statusBarItem.text = '$(shield) C-Prime'; }, 5000);
        }
    });
}

// ─── New File ─────────────────────────────────────────────────────────────────

async function newCprimeFile() {
    const template =
`import io;

fn main() -> i32 {
    io.println("Hello, C-Prime!");
    return 0;
}
`;
    const doc = await vscode.workspace.openTextDocument({
        language: 'cprime',
        content:  template,
    });
    await vscode.window.showTextDocument(doc);
}

// ─── Tutorial Webview ─────────────────────────────────────────────────────────

function openTutorial(context: vscode.ExtensionContext, lessonId?: string) {
    const panel = vscode.window.createWebviewPanel(
        'cprimeTutorial',
        'C-Prime Tutorial',
        vscode.ViewColumn.Two,
        { enableScripts: true, retainContextWhenHidden: true }
    );

    panel.webview.html = getTutorialHtml(panel, lessonId || 'intro');

    // Handle "Try it" button — open code in a new editor
    panel.webview.onDidReceiveMessage(async msg => {
        if (msg.command === 'openCode' && msg.code) {
            const doc = await vscode.workspace.openTextDocument({
                language: 'cprime',
                content:  msg.code,
            });
            await vscode.window.showTextDocument(doc, vscode.ViewColumn.One);
        }
    }, undefined, context.subscriptions);
}

// ─── Activation ───────────────────────────────────────────────────────────────

export function activate(context: vscode.ExtensionContext) {
    // Status bar
    initStatusBar(context);

    // Hover provider
    context.subscriptions.push(
        vscode.languages.registerHoverProvider(
            { language: 'cprime' },
            new CprimeHoverProvider()
        )
    );

    // Auto-run cpg on save
    context.subscriptions.push(
        vscode.workspace.onDidSaveTextDocument(doc => {
            if (cfg<boolean>('guard.runOnSave', true)) {
                runGuard(doc);
            }
        })
    );

    // Clear diagnostics when file is closed
    context.subscriptions.push(
        vscode.workspace.onDidCloseTextDocument(doc => {
            diagnosticCollection.delete(doc.uri);
        })
    );

    // ── Register Commands ──────────────────────────────────────────────────

    context.subscriptions.push(
        vscode.commands.registerCommand('cprime.runFile', runCurrentFile),

        vscode.commands.registerCommand('cprime.checkFile', checkCurrentFile),

        vscode.commands.registerCommand('cprime.buildProject', () => {
            const terminal = vscode.window.createTerminal('C-Prime Build');
            terminal.show();
            terminal.sendText('cppm build');
        }),

        vscode.commands.registerCommand('cprime.openTutorial', () => {
            openTutorial(context);
        }),

        vscode.commands.registerCommand('cprime.openReleases', () => {
            createReleasesPanel(context);
        }),

        vscode.commands.registerCommand('cprime.checkForUpdates', () => {
            const terminal = vscode.window.createTerminal('C-Prime Updates');
            terminal.show();
            terminal.sendText('cppm update');
        }),

        vscode.commands.registerCommand('cprime.newFile', newCprimeFile),
    );

    // Show tutorial on first install
    const isFirstRun = context.globalState.get('cprime.firstRun', true);
    if (isFirstRun) {
        context.globalState.update('cprime.firstRun', false);
        vscode.window.showInformationMessage(
            '👋 C-Prime extension installed! Open the tutorial to get started.',
            'Open Tutorial', 'Dismiss'
        ).then(choice => {
            if (choice === 'Open Tutorial') openTutorial(context);
        });
    }

    console.log('C-Prime extension activated');
}

export function deactivate() {
    diagnosticCollection.dispose();
    outputChannel?.dispose();
}
