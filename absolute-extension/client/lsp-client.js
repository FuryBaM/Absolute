'use strict';

const path = require('path');
const childProcess = require('child_process');
const vscode = require('vscode');

class LspClient {
    constructor() {
        this.child = undefined;
        this.buffer = Buffer.alloc(0);
        this.nextId = 1;
        this.pending = new Map();
        this.diagnostics = undefined;
        this.semanticEmitter = new vscode.EventEmitter();
        this.ready = false;
    }

    start(context, initializationOptions) {
        this.diagnostics = vscode.languages.createDiagnosticCollection('absolute');
        context.subscriptions.push(this.diagnostics, this.semanticEmitter);

        const serverPath = path.join(context.extensionPath, 'server', 'lsp-server.js');
        this.child = childProcess.spawn(process.execPath, [serverPath], {
            cwd: context.extensionPath,
            stdio: ['pipe', 'pipe', 'pipe'],
            windowsHide: true,
            env: process.env
        });

        this.child.stdout.on('data', data => this.#onData(data));
        this.child.stderr.on('data', data => {
            // Server should not write to stderr in normal operation.
            console.error('[absolute-lsp]', data.toString());
        });
        this.child.on('exit', code => {
            this.ready = false;
            console.error(`[absolute-lsp] exited with code ${code}`);
        });

        const workspaceFolders = (vscode.workspace.workspaceFolders || []).map(folder => ({
            uri: folder.uri.toString(),
            name: folder.name
        }));

        return this.request('initialize', {
            processId: process.pid,
            rootUri: workspaceFolders[0]?.uri || null,
            workspaceFolders,
            capabilities: {
                textDocument: {
                    synchronization: { dynamicRegistration: false },
                    completion: { completionItem: { snippetSupport: true } },
                    hover: { contentFormat: ['markdown'] },
                    signatureHelp: {
                        signatureInformation: {
                            documentationFormat: ['markdown'],
                            parameterInformation: { labelOffsetSupport: true }
                        }
                    },
                    definition: {},
                    references: {},
                    rename: { prepareSupport: true },
                    documentSymbol: {},
                    formatting: {},
                    codeAction: {},
                    semanticTokens: { requests: { full: true } }
                },
                workspace: { workspaceFolders: true }
            },
            initializationOptions: initializationOptions || {}
        }).then(result => {
            this.notify('initialized', {});
            this.ready = true;
            this.#registerProviders(context, result && result.capabilities);
            this.#hookDocuments(context);
            return result;
        });
    }

    stop() {
        if (!this.child) return Promise.resolve();
        return this.request('shutdown', null).catch(() => undefined).then(() => {
            this.notify('exit', undefined);
            this.child.kill();
            this.child = undefined;
            this.ready = false;
        });
    }

    setConfiguration(config) {
        if (!this.ready) return Promise.resolve();
        return this.request('absolute/setConfiguration', config);
    }

    request(method, params) {
        const id = this.nextId++;
        const message = { jsonrpc: '2.0', id, method, params };
        return new Promise((resolve, reject) => {
            this.pending.set(id, { resolve, reject });
            this.#send(message);
        });
    }

    notify(method, params) {
        this.#send({ jsonrpc: '2.0', method, params });
    }

    #send(message) {
        if (!this.child || !this.child.stdin.writable) return;
        const json = JSON.stringify(message);
        this.child.stdin.write(`Content-Length: ${Buffer.byteLength(json, 'utf8')}\r\n\r\n${json}`);
    }

    #onData(chunk) {
        this.buffer = Buffer.concat([this.buffer, chunk]);
        while (true) {
            const headerEnd = this.buffer.indexOf('\r\n\r\n');
            if (headerEnd < 0) return;
            const header = this.buffer.slice(0, headerEnd).toString('utf8');
            const match = header.match(/Content-Length:\s*(\d+)/i);
            if (!match) {
                this.buffer = this.buffer.slice(headerEnd + 4);
                continue;
            }
            const length = parseInt(match[1], 10);
            const total = headerEnd + 4 + length;
            if (this.buffer.length < total) return;
            const body = this.buffer.slice(headerEnd + 4, total).toString('utf8');
            this.buffer = this.buffer.slice(total);
            try {
                this.#dispatch(JSON.parse(body));
            } catch (error) {
                console.error('[absolute-lsp] bad message', error);
            }
        }
    }

    #dispatch(message) {
        if (message.id !== undefined && (message.result !== undefined || message.error)) {
            const pending = this.pending.get(message.id);
            if (!pending) return;
            this.pending.delete(message.id);
            if (message.error) pending.reject(new Error(message.error.message || 'LSP error'));
            else pending.resolve(message.result);
            return;
        }
        if (message.method === 'textDocument/publishDiagnostics') {
            const uri = vscode.Uri.parse(message.params.uri);
            const items = (message.params.diagnostics || []).map(diag => {
                const severity = diag.severity === 2 ? vscode.DiagnosticSeverity.Warning :
                    diag.severity === 3 ? vscode.DiagnosticSeverity.Information :
                    diag.severity === 4 ? vscode.DiagnosticSeverity.Hint :
                    vscode.DiagnosticSeverity.Error;
                const item = new vscode.Diagnostic(
                    new vscode.Range(
                        diag.range.start.line, diag.range.start.character,
                        diag.range.end.line, diag.range.end.character),
                    diag.message, severity);
                item.source = diag.source || 'absolutec';
                return item;
            });
            this.diagnostics.set(uri, items);
            return;
        }
        if (message.method === 'window/logMessage') {
            // Optional: route to Absolute output channel later.
            return;
        }
    }

    #hookDocuments(context) {
        const open = document => {
            if (document.languageId !== 'absolute') return;
            this.notify('textDocument/didOpen', {
                textDocument: {
                    uri: document.uri.toString(),
                    languageId: document.languageId,
                    version: document.version,
                    text: document.getText()
                }
            });
        };
        for (const document of vscode.workspace.textDocuments) open(document);
        context.subscriptions.push(vscode.workspace.onDidOpenTextDocument(open));
        context.subscriptions.push(vscode.workspace.onDidChangeTextDocument(event => {
            if (event.document.languageId !== 'absolute') return;
            this.notify('textDocument/didChange', {
                textDocument: {
                    uri: event.document.uri.toString(),
                    version: event.document.version
                },
                contentChanges: [{ text: event.document.getText() }]
            });
            this.semanticEmitter.fire();
        }));
        context.subscriptions.push(vscode.workspace.onDidSaveTextDocument(document => {
            if (document.languageId !== 'absolute') return;
            this.notify('textDocument/didSave', {
                textDocument: { uri: document.uri.toString() }
            });
            this.semanticEmitter.fire();
        }));
        context.subscriptions.push(vscode.workspace.onDidCloseTextDocument(document => {
            if (document.languageId !== 'absolute') return;
            this.notify('textDocument/didClose', {
                textDocument: { uri: document.uri.toString() }
            });
        }));
    }

    #registerProviders(context, capabilities) {
        const selector = { language: 'absolute' };
        const asRange = range => new vscode.Range(
            range.start.line, range.start.character, range.end.line, range.end.character);
        const asLocation = location => new vscode.Location(vscode.Uri.parse(location.uri), asRange(location.range));

        context.subscriptions.push(vscode.languages.registerCompletionItemProvider(selector, {
            provideCompletionItems: async (document, position) => {
                const items = await this.request('textDocument/completion', {
                    textDocument: { uri: document.uri.toString() },
                    position: { line: position.line, character: position.character }
                });
                return (items || []).map(item => {
                    const completion = new vscode.CompletionItem(item.label, item.kind || vscode.CompletionItemKind.Text);
                    completion.detail = item.detail;
                    if (item.documentation) completion.documentation = new vscode.MarkdownString(item.documentation);
                    if (item.insertText) {
                        completion.insertText = item.insertTextFormat === 2
                            ? new vscode.SnippetString(item.insertText)
                            : item.insertText;
                    }
                    return completion;
                });
            }
        }, '.'));

        context.subscriptions.push(vscode.languages.registerHoverProvider(selector, {
            provideHover: async (document, position) => {
                const hover = await this.request('textDocument/hover', {
                    textDocument: { uri: document.uri.toString() },
                    position: { line: position.line, character: position.character }
                });
                if (!hover) return undefined;
                const value = hover.contents && hover.contents.value ? hover.contents.value : String(hover.contents || '');
                return new vscode.Hover(new vscode.MarkdownString(value), hover.range ? asRange(hover.range) : undefined);
            }
        }));

        context.subscriptions.push(vscode.languages.registerSignatureHelpProvider(selector, {
            provideSignatureHelp: async (document, position, _token, contextInfo) => {
                const result = await this.request('textDocument/signatureHelp', {
                    textDocument: { uri: document.uri.toString() },
                    position: { line: position.line, character: position.character },
                    context: {
                        triggerKind: contextInfo.triggerKind,
                        triggerCharacter: contextInfo.triggerCharacter,
                        isRetrigger: contextInfo.isRetrigger
                    }
                });
                if (!result || !Array.isArray(result.signatures)) return undefined;
                const help = new vscode.SignatureHelp();
                help.activeSignature = result.activeSignature || 0;
                help.activeParameter = result.activeParameter || 0;
                help.signatures = result.signatures.map(signature => {
                    const info = new vscode.SignatureInformation(
                        signature.label,
                        signature.documentation
                            ? new vscode.MarkdownString(signature.documentation)
                            : undefined
                    );
                    info.parameters = (signature.parameters || []).map(parameter =>
                        new vscode.ParameterInformation(
                            parameter.label,
                            parameter.documentation
                                ? new vscode.MarkdownString(parameter.documentation)
                                : undefined
                        ));
                    return info;
                });
                return help;
            }
        }, '(', ','));

        context.subscriptions.push(vscode.languages.registerDefinitionProvider(selector, {
            provideDefinition: async (document, position) => {
                const result = await this.request('textDocument/definition', {
                    textDocument: { uri: document.uri.toString() },
                    position: { line: position.line, character: position.character }
                });
                if (!result) return undefined;
                const list = Array.isArray(result) ? result : [result];
                return list.map(asLocation);
            }
        }));

        context.subscriptions.push(vscode.languages.registerReferenceProvider(selector, {
            provideReferences: async (document, position, contextRef) => {
                const result = await this.request('textDocument/references', {
                    textDocument: { uri: document.uri.toString() },
                    position: { line: position.line, character: position.character },
                    context: { includeDeclaration: contextRef.includeDeclaration }
                });
                return (result || []).map(asLocation);
            }
        }));

        context.subscriptions.push(vscode.languages.registerRenameProvider(selector, {
            prepareRename: async (document, position) => {
                const prepared = await this.request('textDocument/prepareRename', {
                    textDocument: { uri: document.uri.toString() },
                    position: { line: position.line, character: position.character }
                });
                if (!prepared) throw new Error('Rename not available here');
                return { range: asRange(prepared.range), placeholder: prepared.placeholder };
            },
            provideRenameEdits: async (document, position, newName) => {
                const edit = await this.request('textDocument/rename', {
                    textDocument: { uri: document.uri.toString() },
                    position: { line: position.line, character: position.character },
                    newName
                });
                const workspaceEdit = new vscode.WorkspaceEdit();
                for (const [uri, edits] of Object.entries(edit.changes || {})) {
                    for (const change of edits) {
                        workspaceEdit.replace(vscode.Uri.parse(uri), asRange(change.range), change.newText);
                    }
                }
                return workspaceEdit;
            }
        }));

        context.subscriptions.push(vscode.languages.registerDocumentSymbolProvider(selector, {
            provideDocumentSymbols: async document => {
                const symbols = await this.request('textDocument/documentSymbol', {
                    textDocument: { uri: document.uri.toString() }
                });
                return (symbols || []).map(symbol => new vscode.DocumentSymbol(
                    symbol.name,
                    symbol.detail || '',
                    symbol.kind || vscode.SymbolKind.Function,
                    asRange(symbol.range),
                    asRange(symbol.selectionRange || symbol.range)
                ));
            }
        }));

        context.subscriptions.push(vscode.languages.registerWorkspaceSymbolProvider({
            provideWorkspaceSymbols: async query => {
                const symbols = await this.request('workspace/symbol', { query: query || '' });
                return (symbols || []).map(symbol => new vscode.SymbolInformation(
                    symbol.name,
                    symbol.kind || vscode.SymbolKind.Function,
                    symbol.containerName || '',
                    asLocation(symbol.location)
                ));
            }
        }));

        context.subscriptions.push(vscode.languages.registerDocumentFormattingEditProvider(selector, {
            provideDocumentFormattingEdits: async (document, options) => {
                const edits = await this.request('textDocument/formatting', {
                    textDocument: { uri: document.uri.toString() },
                    options: {
                        tabSize: options.tabSize,
                        insertSpaces: options.insertSpaces
                    }
                });
                return (edits || []).map(edit => vscode.TextEdit.replace(asRange(edit.range), edit.newText));
            }
        }));

        context.subscriptions.push(vscode.languages.registerCodeActionsProvider(selector, {
            provideCodeActions: async (document, range) => {
                const actions = await this.request('textDocument/codeAction', {
                    textDocument: { uri: document.uri.toString() },
                    range: {
                        start: { line: range.start.line, character: range.start.character },
                        end: { line: range.end.line, character: range.end.character }
                    },
                    context: { diagnostics: [] }
                });
                return (actions || []).map(action => {
                    const item = new vscode.CodeAction(action.title, vscode.CodeActionKind.QuickFix);
                    if (action.command) {
                        item.command = {
                            title: action.command.title,
                            command: action.command.command,
                            arguments: action.command.arguments
                        };
                    }
                    return item;
                });
            }
        }));

        if (capabilities && capabilities.semanticTokensProvider) {
            const legend = new vscode.SemanticTokensLegend(
                capabilities.semanticTokensProvider.legend.tokenTypes,
                capabilities.semanticTokensProvider.legend.tokenModifiers || []
            );
            context.subscriptions.push(vscode.languages.registerDocumentSemanticTokensProvider(selector, {
                onDidChangeSemanticTokens: this.semanticEmitter.event,
                provideDocumentSemanticTokens: async document => {
                    const tokens = await this.request('textDocument/semanticTokens/full', {
                        textDocument: { uri: document.uri.toString() }
                    });
                    const data = tokens && tokens.data ? tokens.data : [];
                    return new vscode.SemanticTokens(new Uint32Array(data));
                }
            }, legend));
        }

        context.subscriptions.push(vscode.commands.registerCommand('absolute.languageServer.formatDocument', async uriValue => {
            const uri = typeof uriValue === 'string' ? vscode.Uri.parse(uriValue) : uriValue;
            const document = await vscode.workspace.openTextDocument(uri);
            await vscode.commands.executeCommand('editor.action.formatDocument');
            return document;
        }));

        context.subscriptions.push(vscode.commands.registerCommand('absolute.languageServer.refreshDiagnostics', uriValue => {
            const uri = typeof uriValue === 'string' ? uriValue : uriValue.toString();
            this.notify('textDocument/didSave', { textDocument: { uri } });
        }));
    }
}

module.exports = { LspClient };
