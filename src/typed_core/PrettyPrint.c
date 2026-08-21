/* Example non-LLVM typed-core backend: deterministic textual dump.
 * This translation unit includes only the C ABI (no LLVM, no parser). */

#include "typed_core/abi.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    char* data;
    size_t len;
    size_t cap;
} Buf;

static int buf_reserve(Buf* b, size_t extra) {
    if (b->len + extra + 1 <= b->cap)
        return 1;
    size_t ncap = b->cap ? b->cap : 256;
    while (ncap < b->len + extra + 1)
        ncap *= 2;
    char* p = (char*)realloc(b->data, ncap);
    if (!p)
        return 0;
    b->data = p;
    b->cap = ncap;
    return 1;
}

static void buf_puts(Buf* b, const char* s) {
    if (!s)
        s = "";
    size_t n = strlen(s);
    if (!buf_reserve(b, n))
        return;
    memcpy(b->data + b->len, s, n);
    b->len += n;
    b->data[b->len] = '\0';
}

static void buf_printf(Buf* b, const char* fmt, ...) {
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

static const char* nz(const char* s) { return s && s[0] ? s : "-"; }

static const char* kind_name(YonaTcKind k) {
    switch (k) {
    case YONA_TC_KIND_MODULE:
        return "module";
    case YONA_TC_KIND_FUNCTION:
        return "function";
    case YONA_TC_KIND_ADT:
        return "adt";
    case YONA_TC_KIND_CONSTRUCTOR:
        return "constructor";
    case YONA_TC_KIND_BINDING:
        return "binding";
    case YONA_TC_KIND_IMPORT:
        return "import";
    case YONA_TC_KIND_CASE:
        return "case";
    case YONA_TC_KIND_PATTERN:
        return "pattern";
    case YONA_TC_KIND_EFFECT:
        return "effect";
    case YONA_TC_KIND_UNSUPPORTED:
        return "unsupported";
    default:
        return "unknown";
    }
}

static void emit_span(Buf* b, YonaTcRange r) {
    buf_printf(b, "span=%u:%u-%u:%u", r.start.line, r.start.character, r.end.line, r.end.character);
}

static void emit_node(Buf* b, const YonaTcNode* n, int indent) {
    if (!n)
        return;
    for (int i = 0; i < indent; ++i)
        buf_puts(b, "  ");
    buf_puts(b, kind_name(n->kind));
    buf_puts(b, " ");
    buf_puts(b, nz(n->name));
    buf_puts(b, " : ");
    buf_puts(b, nz(n->type));
    if (n->module && n->module[0]) {
        buf_puts(b, " module=");
        buf_puts(b, n->module);
    }
    buf_puts(b, " effects=");
    buf_puts(b, nz(n->effects));
    buf_puts(b, " linearity=");
    buf_puts(b, nz(n->linearity));
    buf_puts(b, " ");
    emit_span(b, n->span);
    if (n->detail && n->detail[0]) {
        buf_puts(b, " detail=");
        buf_puts(b, n->detail);
    }
    buf_puts(b, "\n");
    for (uint32_t i = 0; i < n->child_count; ++i)
        emit_node(b, &n->children[i], indent + 1);
}

char* yona_tc_pretty_print(const YonaTcModule* module) {
    if (!module)
        return NULL;
    Buf b = {0};
    buf_printf(&b, "typed-core abi=%u\n", module->abi_version);
    buf_puts(&b, "file=");
    buf_puts(&b, nz(module->filename));
    buf_puts(&b, "\n");
    buf_puts(&b, "module=");
    buf_puts(&b, nz(module->module_name));
    buf_puts(&b, "\n");
    for (uint32_t i = 0; i < module->node_count; ++i)
        emit_node(&b, &module->nodes[i], 0);
    for (uint32_t i = 0; i < module->diagnostic_count; ++i) {
        const YonaTcDiagnostic* d = &module->diagnostics[i];
        buf_printf(&b, "diagnostic severity=%d code=%s message=%s ", d->severity, nz(d->code),
                   nz(d->message));
        emit_span(&b, d->range);
        buf_puts(&b, "\n");
    }
    if (!b.data) {
        char* empty = (char*)malloc(1);
        if (empty)
            empty[0] = '\0';
        return empty;
    }
    return b.data;
}

void yona_tc_string_free(char* text) { free(text); }
