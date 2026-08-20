import * as fs from "node:fs";
import * as path from "node:path";
import { execFileSync } from "node:child_process";

export function which(cmd: string): string | undefined {
  if (!/^[A-Za-z0-9_.-]+$/.test(cmd)) {
    return undefined;
  }
  try {
    if (process.platform === "win32") {
      const out = execFileSync("where", [cmd], { encoding: "utf8" }).trim();
      return out.split(/\r?\n/)[0] || undefined;
    }
    const out = execFileSync("/bin/sh", ["-c", "command -v -- \"$1\"", "sh", cmd], {
      encoding: "utf8",
    }).trim();
    return out || undefined;
  } catch {
    return undefined;
  }
}

function existsFile(p: string | undefined): p is string {
  return !!p && fs.existsSync(p);
}

export function resolveYlsPath(configured: string): string | undefined {
  if (configured.trim()) {
    return configured.trim();
  }
  const fromPath = which("yls");
  if (existsFile(fromPath)) {
    return fromPath;
  }
  const home = process.env.YONA_HOME;
  if (home) {
    const candidate = path.join(home, "bin", process.platform === "win32" ? "yls.exe" : "yls");
    if (existsFile(candidate)) {
      return candidate;
    }
  }
  const yonac = which("yonac");
  if (yonac) {
    const sibling = path.join(path.dirname(yonac), process.platform === "win32" ? "yls.exe" : "yls");
    if (existsFile(sibling)) {
      return sibling;
    }
  }
  return undefined;
}
