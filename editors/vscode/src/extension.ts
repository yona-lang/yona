import { execFileSync } from "node:child_process";
import * as vscode from "vscode";
import {
  LanguageClient,
  type LanguageClientOptions,
  type ServerOptions,
  TransportKind,
} from "vscode-languageclient/node";
import { resolveYlsPath, which } from "./ylsPath";

let client: LanguageClient | undefined;

export { resolveYlsPath };

export function activate(context: vscode.ExtensionContext): void {
  const configured =
    vscode.workspace.getConfiguration("yona").get<string>("languageServer.path") ?? "";
  const serverPath = resolveYlsPath(configured);
  if (!serverPath) {
    void vscode.window.showWarningMessage(
      "Yona: yls was not found. Install the Yona toolchain or set yona.languageServer.path. Syntax highlighting still works."
    );
    return;
  }
  const serverOptions: ServerOptions = {
    command: serverPath,
    args: ["--stdio"],
    transport: TransportKind.stdio,
  };
  const clientOptions: LanguageClientOptions = {
    documentSelector: [
      { scheme: "file", language: "yona" },
      { scheme: "untitled", language: "yona" },
    ],
    synchronize: {
      fileEvents: vscode.workspace.createFileSystemWatcher("**/*.{yona,yonai}"),
    },
  };
  client = new LanguageClient("yona", "Yona Language Server", serverOptions, clientOptions);
  context.subscriptions.push(client);
  context.subscriptions.push(
    vscode.commands.registerCommand("yona.explain", (code?: string) => {
      const yonac = which("yonac");
      if (yonac && code) {
        try {
          const out = execFileSync(yonac, ["--explain", code], { encoding: "utf8" });
          void vscode.window.showInformationMessage(out.slice(0, 500));
          return;
        } catch {
          /* fall through */
        }
      }
      void vscode.window.showInformationMessage(
        code
          ? `Yona ${code}. Run: yonac --explain ${code}`
          : "Select a Yona diagnostic, then run Explain."
      );
    })
  );
  void client.start();
}

export async function deactivate(): Promise<void> {
  if (client) {
    await client.stop();
  }
}
