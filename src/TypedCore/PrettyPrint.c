/* Example non-LLVM typed-core backend: deterministic textual dump.
 * This translation unit includes only the C ABI (no LLVM, no parser). */

#include "yona/TypedCore/Abi.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
  char *data;
  size_t len;
  size_t cap;
} Buf;

static int buf_reserve(Buf *b, size_t extra) {
  if (b->len + extra + 1 <= b->cap)
    return 1;
  size_t ncap = b->cap ? b->cap : 256;
  while (ncap < b->len + extra + 1)
    ncap *= 2;
  char *p = (char *)realloc(b->data, ncap);
  if (!p)
    return 0;
  b->data = p;
  b->cap = ncap;
  return 1;
}

static void buf_puts(Buf *b, const char *s) {
  if (!s)
    s = "";
  size_t n = strlen(s);
  if (!buf_reserve(b, n))
    return;
  memcpy(b->data + b->len, s, n);
  b->len += n;
  b->data[b->len] = '\0';
}

static void buf_printf(Buf *b, const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  char stack[256];
  int n = vsnprintf(stack, sizeof(stack), fmt, ap);
  va_end(ap);
  if (n < 0)
    return;
  if ((size_t)n < sizeof(stack)) {
    buf_puts(b, stack);
    return;
  }
  if (!buf_reserve(b, (size_t)n))
    return;
  va_start(ap, fmt);
  vsnprintf(b->data + b->len, (size_t)n + 1, fmt, ap);
  va_end(ap);
  b->len += (size_t)n;
  b->data[b->len] = '\0';
}

static const char *nz(const char *s) { return s && s[0] ? s : "-"; }

static const char *kind_name(YonaTypedCoreNodeKind k) {
  switch (k) {
  case YonaTypedCoreNodeKindModule:
    return "module";
  case YonaTypedCoreNodeKindFunction:
    return "function";
  case YonaTypedCoreNodeKindAdt:
    return "adt";
  case YonaTypedCoreNodeKindConstructor:
    return "constructor";
  case YonaTypedCoreNodeKindBinding:
    return "binding";
  case YonaTypedCoreNodeKindImport:
    return "import";
  case YonaTypedCoreNodeKindCase:
    return "case";
  case YonaTypedCoreNodeKindPattern:
    return "pattern";
  case YonaTypedCoreNodeKindEffect:
    return "effect";
  case YonaTypedCoreNodeKindUnsupported:
    return "unsupported";
  default:
    return "unknown";
  }
}

static void emit_span(Buf *b, YonaTypedCoreSourceRange r) {
  buf_printf(b, "span=%u:%u-%u:%u", r.Start.Line, r.Start.Character, r.End.Line,
             r.End.Character);
}

static void emit_node(Buf *b, const YonaTypedCoreNode *n, int indent) {
  if (!n)
    return;
  for (int i = 0; i < indent; ++i)
    buf_puts(b, "  ");
  buf_puts(b, kind_name(n->Kind));
  buf_puts(b, " ");
  buf_puts(b, nz(n->Name));
  buf_puts(b, " : ");
  buf_puts(b, nz(n->Type));
  if (n->Module && n->Module[0]) {
    buf_puts(b, " module=");
    buf_puts(b, n->Module);
  }
  buf_puts(b, " effects=");
  buf_puts(b, nz(n->Effects));
  buf_puts(b, " linearity=");
  buf_puts(b, nz(n->Linearity));
  buf_puts(b, " ");
  emit_span(b, n->SourceRange);
  if (n->Detail && n->Detail[0]) {
    buf_puts(b, " detail=");
    buf_puts(b, n->Detail);
  }
  buf_puts(b, "\n");
  for (uint32_t i = 0; i < n->ChildCount; ++i)
    emit_node(b, &n->Children[i], indent + 1);
}

char *YonaTypedCorePrettyPrint(const YonaTypedCoreModule *module) {
  if (!module)
    return NULL;
  Buf b = {0};
  buf_puts(&b, "typed-core\n");
  buf_puts(&b, "file=");
  buf_puts(&b, nz(module->Filename));
  buf_puts(&b, "\n");
  buf_puts(&b, "module=");
  buf_puts(&b, nz(module->ModuleName));
  buf_puts(&b, "\n");
  for (uint32_t i = 0; i < module->NodeCount; ++i)
    emit_node(&b, &module->Nodes[i], 0);
  for (uint32_t i = 0; i < module->DiagnosticCount; ++i) {
    const YonaTypedCoreDiagnostic *d = &module->Diagnostics[i];
    buf_printf(&b, "diagnostic severity=%d code=%s message=%s ", d->Severity,
               nz(d->Code), nz(d->Message));
    emit_span(&b, d->SourceRange);
    buf_puts(&b, "\n");
  }
  if (!b.data) {
    char *empty = (char *)malloc(1);
    if (empty)
      empty[0] = '\0';
    return empty;
  }
  return b.data;
}

void YonaTypedCoreDisposeString(char *text) { free(text); }
