import { readFileSync } from "node:fs";
import { join } from "node:path";

export function llmsFile(
  name: "llms.txt" | "llms-full.txt" | "llms-small.txt",
): Response {
  const body = readFileSync(
    join(process.cwd(), "generated", "llms", name),
    "utf-8",
  );
  return new Response(body, {
    headers: {
      "Content-Type": "text/plain; charset=utf-8",
      "Cache-Control": "no-cache",
    },
  });
}
