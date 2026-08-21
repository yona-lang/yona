/* Std\Json — recursive Json ADT parse/stringify.
 *
 * Layout (RC_TYPE_ADT): [tag, num_fields, heap_mask, fields…]
 *   JsonNull=0, JsonBool=1, JsonInt=2, JsonFloat=3,
 *   JsonString=4 (heap), JsonArray=5 (heap seq), JsonObject=6 (heap seq of 2-tuples).
 * parse returns Prelude Result (Ok Json | Err String).
 */

#ifndef YONA_JSON_MAX_DEPTH
#define YONA_JSON_MAX_DEPTH 64
#endif
#ifndef YONA_JSON_MAX_BYTES
#define YONA_JSON_MAX_BYTES (16u * 1024u * 1024u)
#endif

extern int64_t* yona_rt_seq_alloc(int64_t count);
extern void yona_rt_seq_set(int64_t* seq, int64_t index, int64_t value);
extern void yona_rt_seq_set_heap(int64_t* seq, int64_t flag);
extern int64_t yona_rt_seq_length(int64_t* seq);
extern int64_t yona_rt_seq_get(int64_t* seq, int64_t index);
extern void* yona_rt_rc_alloc_string_len(size_t bytes, size_t str_len);

typedef struct {
    int64_t** items;
    size_t n;
    size_t cap;
} yona_json_vec;

static int yona_json_vec_push(yona_json_vec* v, int64_t* x) {
    if (v->n == v->cap) {
        size_t ncap = v->cap ? v->cap * 2 : 8;
        if (ncap > (1u << 20))
            return 0;
        int64_t** ni = (int64_t**)realloc(v->items, ncap * sizeof(*ni));
        if (!ni)
            return 0;
        v->items = ni;
        v->cap = ncap;
    }
    v->items[v->n++] = x;
    return 1;
}

static char* yona_json_copy_str(const char* s, size_t n) {
    char* r = (char*)yona_rt_rc_alloc_string_len(n + 1, n);
    memcpy(r, s, n);
    r[n] = '\0';
    return r;
}

static int64_t* yona_json_adt(int64_t tag, int64_t nfields, int64_t heap_mask) {
    int64_t* adt = (int64_t*)rc_alloc(RC_TYPE_ADT, (ADT_HDR_SIZE + nfields) * sizeof(int64_t));
    adt[0] = tag;
    adt[1] = nfields;
    adt[2] = heap_mask;
    return adt;
}

static int64_t yona_json_result_ok(int64_t* json) {
    int64_t* adt = yona_json_adt(0, 1, 1);
    adt[3] = (int64_t)(intptr_t)json;
    return (int64_t)(intptr_t)adt;
}

static int64_t yona_json_result_err(const char* msg) {
    int64_t* adt = yona_json_adt(1, 1, 1);
    adt[3] = (int64_t)(intptr_t)yona_json_copy_str(msg, strlen(msg));
    return (int64_t)(intptr_t)adt;
}

const char* yona_Std_Json__stringifyString(const char* s) {
    const char* src = s ? s : "";
    size_t len = strlen(src);
    char* r = (char*)rc_alloc(RC_TYPE_STRING, len * 6 + 3);
    size_t j = 0;
    r[j++] = '"';
    for (size_t i = 0; i < len; i++) {
        unsigned char c = (unsigned char)src[i];
        switch (c) {
        case '"':
            r[j++] = '\\';
            r[j++] = '"';
            break;
        case '\\':
            r[j++] = '\\';
            r[j++] = '\\';
            break;
        case '\n':
            r[j++] = '\\';
            r[j++] = 'n';
            break;
        case '\r':
            r[j++] = '\\';
            r[j++] = 'r';
            break;
        case '\t':
            r[j++] = '\\';
            r[j++] = 't';
            break;
        default:
            r[j++] = (char)c;
            break;
        }
    }
    r[j++] = '"';
    r[j] = '\0';
    return r;
}

const char* yona_Std_Json__stringifyBool(int64_t b) {
    const char* src = b ? "true" : "false";
    return yona_json_copy_str(src, strlen(src));
}

const char* yona_Std_Json__stringifyFloat(double f) {
    char buf[64];
    int n = snprintf(buf, sizeof(buf), "%g", f);
    if (n < 0)
        n = 0;
    return yona_json_copy_str(buf, (size_t)n);
}

const char* yona_Std_Json__null(void) { return yona_json_copy_str("null", 4); }

int64_t yona_Std_Json__parseInt(const char* s) {
    if (!s)
        return 0;
    return (int64_t)strtoll(s, NULL, 10);
}

double yona_Std_Json__parseFloat(const char* s) { return s ? atof(s) : 0.0; }

typedef struct {
    char* buf;
    size_t len;
    size_t cap;
    int ok;
} yona_json_buf;

static void yona_json_buf_init(yona_json_buf* b) {
    b->buf = (char*)malloc(64);
    b->len = 0;
    b->cap = b->buf ? 64 : 0;
    b->ok = b->buf != NULL;
}

static void yona_json_buf_put(yona_json_buf* b, const char* s, size_t n) {
    if (!b->ok)
        return;
    if (b->len + n + 1 > b->cap) {
        size_t ncap = b->cap ? b->cap : 64;
        while (ncap < b->len + n + 1) {
            if (ncap > YONA_JSON_MAX_BYTES / 2) {
                b->ok = 0;
                return;
            }
            ncap *= 2;
        }
        char* nb = (char*)realloc(b->buf, ncap);
        if (!nb) {
            b->ok = 0;
            return;
        }
        b->buf = nb;
        b->cap = ncap;
    }
    memcpy(b->buf + b->len, s, n);
    b->len += n;
    b->buf[b->len] = '\0';
}

static void yona_json_buf_cstr(yona_json_buf* b, const char* s) { yona_json_buf_put(b, s, strlen(s)); }

static void yona_json_dump(int64_t* j, yona_json_buf* b, int depth);

static void yona_json_dump(int64_t* j, yona_json_buf* b, int depth) {
    if (!b->ok || !j || depth > YONA_JSON_MAX_DEPTH) {
        b->ok = 0;
        return;
    }
    int64_t tag = j[0];
    switch (tag) {
    case 0:
        yona_json_buf_cstr(b, "null");
        break;
    case 1:
        yona_json_buf_cstr(b, j[3] ? "true" : "false");
        break;
    case 2: {
        char tmp[32];
        int n = snprintf(tmp, sizeof(tmp), "%" PRId64, j[3]);
        if (n > 0)
            yona_json_buf_put(b, tmp, (size_t)n);
        break;
    }
    case 3: {
        union {
            int64_t i;
            double d;
        } u;
        u.i = j[3];
        const char* fs = yona_Std_Json__stringifyFloat(u.d);
        yona_json_buf_cstr(b, fs);
        break;
    }
    case 4:
        yona_json_buf_cstr(b, yona_Std_Json__stringifyString((const char*)(intptr_t)j[3]));
        break;
    case 5: {
        int64_t* seq = (int64_t*)(intptr_t)j[3];
        int64_t n = yona_rt_seq_length(seq);
        yona_json_buf_cstr(b, "[");
        for (int64_t i = 0; i < n; i++) {
            if (i)
                yona_json_buf_cstr(b, ",");
            yona_json_dump((int64_t*)(intptr_t)yona_rt_seq_get(seq, i), b, depth + 1);
        }
        yona_json_buf_cstr(b, "]");
        break;
    }
    case 6: {
        int64_t* seq = (int64_t*)(intptr_t)j[3];
        int64_t n = yona_rt_seq_length(seq);
        yona_json_buf_cstr(b, "{");
        for (int64_t i = 0; i < n; i++) {
            if (i)
                yona_json_buf_cstr(b, ",");
            int64_t* tup = (int64_t*)(intptr_t)yona_rt_seq_get(seq, i);
            const char* key = (const char*)(intptr_t)tup[2];
            yona_json_buf_cstr(b, yona_Std_Json__stringifyString(key));
            yona_json_buf_cstr(b, ":");
            yona_json_dump((int64_t*)(intptr_t)tup[3], b, depth + 1);
        }
        yona_json_buf_cstr(b, "}");
        break;
    }
    default:
        yona_json_buf_cstr(b, "null");
        break;
    }
}

const char* yona_Std_Json__stringify(int64_t json_i64) {
    yona_json_buf b;
    yona_json_buf_init(&b);
    if (json_i64)
        yona_json_dump((int64_t*)(intptr_t)json_i64, &b, 0);
    else
        yona_json_buf_cstr(&b, "null");
    if (!b.ok) {
        free(b.buf);
        return yona_json_copy_str("null", 4);
    }
    char* r = yona_json_copy_str(b.buf, b.len);
    free(b.buf);
    return r;
}

typedef struct {
    const char* s;
    size_t n;
    size_t i;
    int depth;
    const char* err;
} yona_jsp;

static void yona_jsp_skip(yona_jsp* p) {
    while (p->i < p->n) {
        char c = p->s[p->i];
        if (c != ' ' && c != '\t' && c != '\n' && c != '\r')
            break;
        p->i++;
    }
}

static int yona_hex4(yona_jsp* p, uint32_t* out) {
    if (p->i + 4 > p->n)
        return 0;
    uint32_t v = 0;
    for (int k = 0; k < 4; k++) {
        unsigned char c = (unsigned char)p->s[p->i + (size_t)k];
        uint32_t d;
        if (c >= '0' && c <= '9')
            d = (uint32_t)(c - '0');
        else if (c >= 'A' && c <= 'F')
            d = (uint32_t)(c - 'A' + 10);
        else if (c >= 'a' && c <= 'f')
            d = (uint32_t)(c - 'a' + 10);
        else
            return 0;
        v = (v << 4) | d;
    }
    p->i += 4;
    *out = v;
    return 1;
}

static void yona_utf8_put(yona_json_buf* b, uint32_t cp) {
    char tmp[4];
    size_t n = 0;
    if (cp < 0x80) {
        tmp[n++] = (char)cp;
    } else if (cp < 0x800) {
        tmp[n++] = (char)(0xC0 | (cp >> 6));
        tmp[n++] = (char)(0x80 | (cp & 0x3F));
    } else if (cp < 0x10000) {
        tmp[n++] = (char)(0xE0 | (cp >> 12));
        tmp[n++] = (char)(0x80 | ((cp >> 6) & 0x3F));
        tmp[n++] = (char)(0x80 | (cp & 0x3F));
    } else {
        tmp[n++] = (char)(0xF0 | (cp >> 18));
        tmp[n++] = (char)(0x80 | ((cp >> 12) & 0x3F));
        tmp[n++] = (char)(0x80 | ((cp >> 6) & 0x3F));
        tmp[n++] = (char)(0x80 | (cp & 0x3F));
    }
    yona_json_buf_put(b, tmp, n);
}

static int64_t* yona_jsp_string(yona_jsp* p) {
    if (p->i >= p->n || p->s[p->i] != '"') {
        p->err = "expected string";
        return NULL;
    }
    p->i++;
    yona_json_buf b;
    yona_json_buf_init(&b);
    while (p->i < p->n) {
        unsigned char c = (unsigned char)p->s[p->i];
        if (c == '"') {
            p->i++;
            if (!b.ok) {
                free(b.buf);
                p->err = "string too large";
                return NULL;
            }
            int64_t* adt = yona_json_adt(4, 1, 1);
            adt[3] = (int64_t)(intptr_t)yona_json_copy_str(b.buf ? b.buf : "", b.len);
            free(b.buf);
            return adt;
        }
        if (c < 32) {
            free(b.buf);
            p->err = "unescaped control";
            return NULL;
        }
        if (c != '\\') {
            yona_json_buf_put(&b, (const char*)&c, 1);
            p->i++;
            continue;
        }
        if (p->i + 1 >= p->n) {
            free(b.buf);
            p->err = "truncated escape";
            return NULL;
        }
        char e = p->s[p->i + 1];
        p->i += 2;
        switch (e) {
        case '"':
        case '\\':
        case '/':
            yona_json_buf_put(&b, &e, 1);
            break;
        case 'b':
            yona_json_buf_put(&b, "\b", 1);
            break;
        case 'f':
            yona_json_buf_put(&b, "\f", 1);
            break;
        case 'n':
            yona_json_buf_put(&b, "\n", 1);
            break;
        case 'r':
            yona_json_buf_put(&b, "\r", 1);
            break;
        case 't':
            yona_json_buf_put(&b, "\t", 1);
            break;
        case 'u': {
            uint32_t hi = 0;
            if (!yona_hex4(p, &hi)) {
                free(b.buf);
                p->err = "bad hex";
                return NULL;
            }
            if (hi >= 0xD800 && hi <= 0xDBFF && p->i + 2 < p->n && p->s[p->i] == '\\' && p->s[p->i + 1] == 'u') {
                p->i += 2;
                uint32_t lo = 0;
                if (!yona_hex4(p, &lo) || lo < 0xDC00 || lo > 0xDFFF) {
                    free(b.buf);
                    p->err = "bad surrogate";
                    return NULL;
                }
                yona_utf8_put(&b, 0x10000u + ((hi - 0xD800u) << 10) + (lo - 0xDC00u));
            } else {
                yona_utf8_put(&b, hi);
            }
            break;
        }
        default:
            free(b.buf);
            p->err = "bad escape";
            return NULL;
        }
    }
    free(b.buf);
    p->err = "unterminated string";
    return NULL;
}

static int64_t* yona_jsp_value(yona_jsp* p);

static int64_t* yona_jsp_number(yona_jsp* p) {
    size_t start = p->i;
    if (p->i < p->n && p->s[p->i] == '-')
        p->i++;
    size_t dig0 = p->i;
    while (p->i < p->n && p->s[p->i] >= '0' && p->s[p->i] <= '9')
        p->i++;
    if (p->i == dig0) {
        p->err = "bad number";
        return NULL;
    }
    int isf = 0;
    if (p->i < p->n && p->s[p->i] == '.') {
        p->i++;
        size_t f0 = p->i;
        while (p->i < p->n && p->s[p->i] >= '0' && p->s[p->i] <= '9')
            p->i++;
        if (p->i == f0) {
            p->err = "bad number";
            return NULL;
        }
        isf = 1;
    }
    if (p->i < p->n && (p->s[p->i] == 'e' || p->s[p->i] == 'E')) {
        p->i++;
        if (p->i < p->n && (p->s[p->i] == '+' || p->s[p->i] == '-'))
            p->i++;
        size_t e0 = p->i;
        while (p->i < p->n && p->s[p->i] >= '0' && p->s[p->i] <= '9')
            p->i++;
        if (p->i == e0) {
            p->err = "bad number";
            return NULL;
        }
        isf = 1;
    }
    char* tok = (char*)malloc(p->i - start + 1);
    if (!tok) {
        p->err = "oom";
        return NULL;
    }
    memcpy(tok, p->s + start, p->i - start);
    tok[p->i - start] = '\0';
    int64_t* adt;
    if (isf) {
        union {
            int64_t i;
            double d;
        } u;
        u.d = atof(tok);
        adt = yona_json_adt(3, 1, 0);
        adt[3] = u.i;
    } else {
        adt = yona_json_adt(2, 1, 0);
        adt[3] = (int64_t)strtoll(tok, NULL, 10);
    }
    free(tok);
    return adt;
}

static int64_t* yona_jsp_array(yona_jsp* p) {
    p->i++;
    if (p->depth >= YONA_JSON_MAX_DEPTH) {
        p->err = "too deep";
        return NULL;
    }
    p->depth++;
    yona_json_vec tmp = {NULL, 0, 0};
    yona_jsp_skip(p);
    if (p->i < p->n && p->s[p->i] == ']') {
        p->i++;
        p->depth--;
        int64_t* seq = yona_rt_seq_alloc(0);
        int64_t* adt = yona_json_adt(5, 1, 1);
        adt[3] = (int64_t)(intptr_t)seq;
        return adt;
    }
    for (;;) {
        int64_t* v = yona_jsp_value(p);
        if (!v) {
            free(tmp.items);
            return NULL;
        }
        if (!yona_json_vec_push(&tmp, v)) {
            free(tmp.items);
            p->err = "array too long";
            return NULL;
        }
        yona_jsp_skip(p);
        if (p->i >= p->n) {
            free(tmp.items);
            p->err = "unterminated array";
            return NULL;
        }
        if (p->s[p->i] == ',') {
            p->i++;
            continue;
        }
        if (p->s[p->i] == ']') {
            p->i++;
            break;
        }
        free(tmp.items);
        p->err = "expected comma or ]";
        return NULL;
    }
    p->depth--;
    int64_t* seq = yona_rt_seq_alloc((int64_t)tmp.n);
    for (size_t i = 0; i < tmp.n; i++)
        yona_rt_seq_set(seq, (int64_t)i, (int64_t)(intptr_t)tmp.items[i]);
    free(tmp.items);
    yona_rt_seq_set_heap(seq, 1);
    int64_t* adt = yona_json_adt(5, 1, 1);
    adt[3] = (int64_t)(intptr_t)seq;
    return adt;
}

static int64_t* yona_jsp_object(yona_jsp* p) {
    p->i++;
    if (p->depth >= YONA_JSON_MAX_DEPTH) {
        p->err = "too deep";
        return NULL;
    }
    p->depth++;
    yona_json_vec tmp = {NULL, 0, 0};
    yona_jsp_skip(p);
    if (p->i < p->n && p->s[p->i] == '}') {
        p->i++;
        p->depth--;
        int64_t* seq = yona_rt_seq_alloc(0);
        int64_t* adt = yona_json_adt(6, 1, 1);
        adt[3] = (int64_t)(intptr_t)seq;
        return adt;
    }
    for (;;) {
        yona_jsp_skip(p);
        int64_t* ks = yona_jsp_string(p);
        if (!ks) {
            free(tmp.items);
            return NULL;
        }
        yona_jsp_skip(p);
        if (p->i >= p->n || p->s[p->i] != ':') {
            free(tmp.items);
            p->err = "expected colon";
            return NULL;
        }
        p->i++;
        int64_t* v = yona_jsp_value(p);
        if (!v) {
            free(tmp.items);
            return NULL;
        }
        int64_t* tup = (int64_t*)rc_alloc(RC_TYPE_TUPLE, 4 * sizeof(int64_t));
        tup[0] = 2;
        tup[1] = 3;
        tup[2] = ks[3];
        tup[3] = (int64_t)(intptr_t)v;
        if (!yona_json_vec_push(&tmp, tup)) {
            free(tmp.items);
            p->err = "object too large";
            return NULL;
        }
        yona_jsp_skip(p);
        if (p->i >= p->n) {
            free(tmp.items);
            p->err = "unterminated object";
            return NULL;
        }
        if (p->s[p->i] == ',') {
            p->i++;
            continue;
        }
        if (p->s[p->i] == '}') {
            p->i++;
            break;
        }
        free(tmp.items);
        p->err = "expected comma or }";
        return NULL;
    }
    p->depth--;
    int64_t* seq = yona_rt_seq_alloc((int64_t)tmp.n);
    for (size_t i = 0; i < tmp.n; i++)
        yona_rt_seq_set(seq, (int64_t)i, (int64_t)(intptr_t)tmp.items[i]);
    free(tmp.items);
    yona_rt_seq_set_heap(seq, 1);
    int64_t* adt = yona_json_adt(6, 1, 1);
    adt[3] = (int64_t)(intptr_t)seq;
    return adt;
}

static int64_t* yona_jsp_value(yona_jsp* p) {
    yona_jsp_skip(p);
    if (p->i >= p->n) {
        p->err = "unexpected eof";
        return NULL;
    }
    char c = p->s[p->i];
    if (c == 'n' && p->i + 4 <= p->n && memcmp(p->s + p->i, "null", 4) == 0) {
        p->i += 4;
        return yona_json_adt(0, 0, 0);
    }
    if (c == 't' && p->i + 4 <= p->n && memcmp(p->s + p->i, "true", 4) == 0) {
        p->i += 4;
        int64_t* adt = yona_json_adt(1, 1, 0);
        adt[3] = 1;
        return adt;
    }
    if (c == 'f' && p->i + 5 <= p->n && memcmp(p->s + p->i, "false", 5) == 0) {
        p->i += 5;
        int64_t* adt = yona_json_adt(1, 1, 0);
        adt[3] = 0;
        return adt;
    }
    if (c == '"')
        return yona_jsp_string(p);
    if (c == '-' || (c >= '0' && c <= '9'))
        return yona_jsp_number(p);
    if (c == '[')
        return yona_jsp_array(p);
    if (c == '{')
        return yona_jsp_object(p);
    p->err = "expected value";
    return NULL;
}

int64_t yona_Std_Json__parse(const char* s) {
    if (!s)
        return yona_json_result_err("unexpected eof");
    size_t n = (size_t)yona_rt_string_length_fast(s);
    if (n > YONA_JSON_MAX_BYTES)
        return yona_json_result_err("too large");
    yona_jsp p = {s, n, 0, 0, NULL};
    int64_t* v = yona_jsp_value(&p);
    if (!v)
        return yona_json_result_err(p.err ? p.err : "parse error");
    yona_jsp_skip(&p);
    if (p.i != p.n)
        return yona_json_result_err("trailing junk");
    return yona_json_result_ok(v);
}
