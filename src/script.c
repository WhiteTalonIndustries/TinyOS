#include <stdint.h>
#include "script.h"
#include "usb.h"
#include "fs.h"
#include "adc.h"
#include "string.h"

/* ================= arena allocator (AST nodes + string data) ================= */

#define ARENA_SIZE (32u * 1024u)
static uint8_t arena[ARENA_SIZE];
static uint32_t arena_used;

static void arena_reset(void) { arena_used = 0; }

static void *arena_alloc(uint32_t size) {
    void *p;
    size = (size + 3u) & ~3u;
    if (arena_used + size > ARENA_SIZE) return NULL;
    p = &arena[arena_used];
    arena_used += size;
    return p;
}

static void byte_copy(char *dst, const char *src, uint32_t n) {
    uint32_t i;
    for (i = 0; i < n; i++) dst[i] = src[i];
}

/* ================= tokens ================= */

enum {
    T_EOF, T_NUM, T_STR, T_IDENT,
    T_LPAREN, T_RPAREN, T_LBRACE, T_RBRACE, T_SEMI, T_COMMA,
    T_PLUS, T_MINUS, T_STAR, T_SLASH, T_PERCENT, T_CARET,
    T_ASSIGN, T_EQ, T_NEQ, T_LT, T_LE, T_GT, T_GE,
    T_AND, T_OR, T_NOT,
    T_IF, T_ELSE, T_WHILE, T_FUNCTION, T_RETURN, T_TRUE, T_FALSE
};

typedef struct {
    int type;
    int32_t num;
    char text[24];
    const char *str; /* string literal contents, arena-owned */
} token_t;

static const char *lex_src;
static int lex_line;
static token_t cur;
static int parse_error;

static void perror_at(const char *msg) {
    if (parse_error) return;
    parse_error = 1;
    console_puts("script: parse error (line ");
    {
        char tmp[8];
        int n = 0;
        uint32_t v = (uint32_t)lex_line;
        if (v == 0) tmp[n++] = '0';
        while (v > 0 && n < 8) { tmp[n++] = (char)('0' + v % 10); v /= 10; }
        while (n > 0) console_putc(tmp[--n]);
    }
    console_puts("): ");
    console_puts(msg);
    console_puts("\n");
}

static int is_alpha(char c) { return (c == '_') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'); }
static int is_digit(char c) { return c >= '0' && c <= '9'; }
static int is_alnum(char c) { return is_alpha(c) || is_digit(c); }

static void skip_ws(void) {
    for (;;) {
        char c = *lex_src;
        if (c == '\n') { lex_line++; lex_src++; }
        else if (c == ' ' || c == '\t' || c == '\r') { lex_src++; }
        else if (c == '/' && lex_src[1] == '/') {
            while (*lex_src && *lex_src != '\n') lex_src++;
        } else {
            break;
        }
    }
}

static void lex_next(void) {
    char c;
    skip_ws();
    c = *lex_src;

    cur.str = NULL;
    cur.num = 0;

    if (c == '\0') { cur.type = T_EOF; return; }

    if (is_digit(c)) {
        int32_t v = 0;
        while (is_digit(*lex_src)) { v = v * 10 + (*lex_src - '0'); lex_src++; }
        cur.type = T_NUM;
        cur.num = v;
        return;
    }

    if (is_alpha(c)) {
        int n = 0;
        while (is_alnum(*lex_src) && n < 23) { cur.text[n++] = *lex_src++; }
        while (is_alnum(*lex_src)) lex_src++; /* discard overflow chars */
        cur.text[n] = '\0';
        if (strcmp(cur.text, "if") == 0) cur.type = T_IF;
        else if (strcmp(cur.text, "else") == 0) cur.type = T_ELSE;
        else if (strcmp(cur.text, "while") == 0) cur.type = T_WHILE;
        else if (strcmp(cur.text, "function") == 0) cur.type = T_FUNCTION;
        else if (strcmp(cur.text, "return") == 0) cur.type = T_RETURN;
        else if (strcmp(cur.text, "true") == 0) cur.type = T_TRUE;
        else if (strcmp(cur.text, "false") == 0) cur.type = T_FALSE;
        else cur.type = T_IDENT;
        return;
    }

    if (c == '"') {
        char tmp[256];
        int n = 0;
        char *out;
        lex_src++;
        while (*lex_src && *lex_src != '"' && n < 255) {
            char ch = *lex_src++;
            if (ch == '\\' && *lex_src) {
                char esc = *lex_src++;
                if (esc == 'n') ch = '\n';
                else if (esc == 't') ch = '\t';
                else if (esc == '"') ch = '"';
                else if (esc == '\\') ch = '\\';
                else ch = esc;
            }
            tmp[n++] = ch;
        }
        if (*lex_src == '"') lex_src++;
        else perror_at("unterminated string");
        out = (char *)arena_alloc((uint32_t)n + 1);
        if (out) { byte_copy(out, tmp, (uint32_t)n); out[n] = '\0'; }
        cur.type = T_STR;
        cur.str = out ? out : "";
        return;
    }

    lex_src++;
    switch (c) {
        case '(': cur.type = T_LPAREN; return;
        case ')': cur.type = T_RPAREN; return;
        case '{': cur.type = T_LBRACE; return;
        case '}': cur.type = T_RBRACE; return;
        case ';': cur.type = T_SEMI; return;
        case ',': cur.type = T_COMMA; return;
        case '+': cur.type = T_PLUS; return;
        case '-': cur.type = T_MINUS; return;
        case '*': cur.type = T_STAR; return;
        case '/': cur.type = T_SLASH; return;
        case '%': cur.type = T_PERCENT; return;
        case '^': cur.type = T_CARET; return;
        case '=':
            if (*lex_src == '=') { lex_src++; cur.type = T_EQ; } else cur.type = T_ASSIGN;
            return;
        case '!':
            if (*lex_src == '=') { lex_src++; cur.type = T_NEQ; } else cur.type = T_NOT;
            return;
        case '<':
            if (*lex_src == '=') { lex_src++; cur.type = T_LE; } else cur.type = T_LT;
            return;
        case '>':
            if (*lex_src == '=') { lex_src++; cur.type = T_GE; } else cur.type = T_GT;
            return;
        case '&':
            if (*lex_src == '&') { lex_src++; cur.type = T_AND; return; }
            perror_at("unexpected '&'"); cur.type = T_EOF; return;
        case '|':
            if (*lex_src == '|') { lex_src++; cur.type = T_OR; return; }
            perror_at("unexpected '|'"); cur.type = T_EOF; return;
        default:
            perror_at("unexpected character");
            cur.type = T_EOF;
            return;
    }
}

/* ================= AST ================= */

enum {
    N_NUM, N_STR, N_BOOL, N_IDENT,
    N_BINOP, N_UNOP, N_ASSIGN, N_CALL,
    N_BLOCK, N_IF, N_WHILE, N_RETURN, N_EXPR_STMT, N_FUNC_DEF
};

typedef struct node {
    int type;
    int op;
    int32_t num;
    const char *str;
    struct node *a, *b, *c;
    struct node *next;
} node_t;

static node_t *new_node(int type) {
    node_t *n = (node_t *)arena_alloc(sizeof(node_t));
    if (!n) { perror_at("out of script memory"); return NULL; }
    n->type = type; n->op = 0; n->num = 0; n->str = NULL;
    n->a = n->b = n->c = n->next = NULL;
    return n;
}

static node_t *parse_expression(void);
static node_t *parse_statement(void);
static node_t *parse_block(void);
static node_t *parse_unary(void);

static void expect(int type, const char *what) {
    if (parse_error) return;
    if (cur.type != type) { perror_at(what); return; }
    lex_next();
}

static node_t *parse_args(void) {
    node_t *head = NULL, *tail = NULL;
    if (cur.type == T_RPAREN) return NULL;
    for (;;) {
        node_t *e = parse_expression();
        if (parse_error) return NULL;
        if (!head) head = tail = e; else { tail->next = e; tail = e; }
        if (cur.type == T_COMMA) { lex_next(); continue; }
        break;
    }
    return head;
}

static node_t *parse_primary(void) {
    node_t *n;
    if (parse_error) return NULL;

    if (cur.type == T_NUM) { n = new_node(N_NUM); if (n) n->num = cur.num; lex_next(); return n; }
    if (cur.type == T_STR) { n = new_node(N_STR); if (n) n->str = cur.str; lex_next(); return n; }
    if (cur.type == T_TRUE) { n = new_node(N_BOOL); if (n) n->num = 1; lex_next(); return n; }
    if (cur.type == T_FALSE) { n = new_node(N_BOOL); if (n) n->num = 0; lex_next(); return n; }

    if (cur.type == T_IDENT) {
        char name[24];
        int i;
        for (i = 0; cur.text[i]; i++) name[i] = cur.text[i];
        name[i] = '\0';
        lex_next();
        if (cur.type == T_LPAREN) {
            char *owned = (char *)arena_alloc((uint32_t)i + 1);
            lex_next();
            n = new_node(N_CALL);
            if (owned) byte_copy(owned, name, (uint32_t)i + 1);
            if (n) n->str = owned;
            if (n) n->a = parse_args();
            expect(T_RPAREN, "expected ')' after arguments");
            return n;
        }
        {
            char *owned = (char *)arena_alloc((uint32_t)i + 1);
            n = new_node(N_IDENT);
            if (owned) byte_copy(owned, name, (uint32_t)i + 1);
            if (n) n->str = owned;
            return n;
        }
    }

    if (cur.type == T_LPAREN) {
        lex_next();
        n = parse_expression();
        expect(T_RPAREN, "expected ')'");
        return n;
    }

    perror_at("expected expression");
    return NULL;
}

/* Right-associative and binds tighter than unary minus, so -2^2 is -4 and
 * 2^3^2 is 2^(3^2), matching ordinary math notation. */
static node_t *parse_power(void) {
    node_t *base = parse_primary();
    if (cur.type == T_CARET) {
        node_t *n = new_node(N_BINOP);
        if (n) n->op = T_CARET;
        lex_next();
        if (n) { n->a = base; n->b = parse_unary(); }
        return n;
    }
    return base;
}

static node_t *parse_unary(void) {
    if (cur.type == T_MINUS || cur.type == T_NOT) {
        node_t *n = new_node(N_UNOP);
        if (n) n->op = cur.type;
        lex_next();
        if (n) n->a = parse_unary();
        return n;
    }
    return parse_power();
}

static node_t *parse_binop_level(node_t *(*next_level)(void), const int *ops, int nops) {
    node_t *left = next_level();
    for (;;) {
        int i, matched = 0;
        if (parse_error) return left;
        for (i = 0; i < nops; i++) if (cur.type == ops[i]) { matched = 1; break; }
        if (!matched) break;
        {
            node_t *n = new_node(N_BINOP);
            if (n) n->op = cur.type;
            lex_next();
            if (n) { n->a = left; n->b = next_level(); }
            left = n;
        }
    }
    return left;
}

static node_t *parse_factor(void) { static const int ops[] = { T_STAR, T_SLASH, T_PERCENT }; return parse_binop_level(parse_unary, ops, 3); }
static node_t *parse_term(void)   { static const int ops[] = { T_PLUS, T_MINUS };            return parse_binop_level(parse_factor, ops, 2); }
static node_t *parse_comparison(void) { static const int ops[] = { T_LT, T_LE, T_GT, T_GE };  return parse_binop_level(parse_term, ops, 4); }
static node_t *parse_equality(void)   { static const int ops[] = { T_EQ, T_NEQ };             return parse_binop_level(parse_comparison, ops, 2); }
static node_t *parse_and(void)        { static const int ops[] = { T_AND };                   return parse_binop_level(parse_equality, ops, 1); }
static node_t *parse_or(void)         { static const int ops[] = { T_OR };                    return parse_binop_level(parse_and, ops, 1); }

static node_t *parse_assignment(void) {
    node_t *left = parse_or();
    if (cur.type == T_ASSIGN) {
        node_t *n;
        if (!left || left->type != N_IDENT) { perror_at("invalid assignment target"); return left; }
        n = new_node(N_ASSIGN);
        if (n) n->str = left->str;
        lex_next();
        if (n) n->a = parse_assignment();
        return n;
    }
    return left;
}

static node_t *parse_expression(void) { return parse_assignment(); }

static node_t *parse_block(void) {
    node_t *blk = new_node(N_BLOCK);
    node_t *tail = NULL;
    expect(T_LBRACE, "expected '{'");
    while (!parse_error && cur.type != T_RBRACE && cur.type != T_EOF) {
        node_t *s = parse_statement();
        if (parse_error) break;
        if (!blk->a) blk->a = s; else tail->next = s;
        tail = s;
    }
    expect(T_RBRACE, "expected '}'");
    return blk;
}

static node_t *parse_if(void) {
    node_t *n = new_node(N_IF);
    lex_next();
    expect(T_LPAREN, "expected '(' after if");
    if (n) n->a = parse_expression();
    expect(T_RPAREN, "expected ')'");
    if (n) n->b = parse_block();
    if (cur.type == T_ELSE) {
        lex_next();
        if (n) n->c = parse_block();
    }
    return n;
}

static node_t *parse_while(void) {
    node_t *n = new_node(N_WHILE);
    lex_next();
    expect(T_LPAREN, "expected '(' after while");
    if (n) n->a = parse_expression();
    expect(T_RPAREN, "expected ')'");
    if (n) n->b = parse_block();
    return n;
}

static node_t *parse_return(void) {
    node_t *n = new_node(N_RETURN);
    lex_next();
    if (cur.type != T_SEMI) { if (n) n->a = parse_expression(); }
    expect(T_SEMI, "expected ';' after return");
    return n;
}

static node_t *parse_funcdef(void) {
    node_t *n = new_node(N_FUNC_DEF);
    node_t *ptail = NULL;
    lex_next();
    if (cur.type != T_IDENT) { perror_at("expected function name"); return n; }
    {
        char *owned = (char *)arena_alloc(24);
        int i;
        for (i = 0; cur.text[i] && i < 23; i++) if (owned) owned[i] = cur.text[i];
        if (owned) owned[i] = '\0';
        if (n) n->str = owned;
    }
    lex_next();
    expect(T_LPAREN, "expected '(' after function name");
    if (cur.type != T_RPAREN) {
        for (;;) {
            node_t *p;
            if (cur.type != T_IDENT) { perror_at("expected parameter name"); break; }
            p = new_node(N_IDENT);
            {
                char *owned = (char *)arena_alloc(24);
                int i;
                for (i = 0; cur.text[i] && i < 23; i++) if (owned) owned[i] = cur.text[i];
                if (owned) owned[i] = '\0';
                if (p) p->str = owned;
            }
            lex_next();
            if (!n->a) n->a = p; else ptail->next = p;
            ptail = p;
            if (cur.type == T_COMMA) { lex_next(); continue; }
            break;
        }
    }
    expect(T_RPAREN, "expected ')' after parameters");
    if (n) n->b = parse_block();
    return n;
}

static node_t *parse_statement(void) {
    if (cur.type == T_FUNCTION) return parse_funcdef();
    if (cur.type == T_IF) return parse_if();
    if (cur.type == T_WHILE) return parse_while();
    if (cur.type == T_RETURN) return parse_return();
    if (cur.type == T_LBRACE) return parse_block();

    {
        node_t *n = new_node(N_EXPR_STMT);
        if (n) n->a = parse_expression();
        expect(T_SEMI, "expected ';'");
        return n;
    }
}

/* ================= values ================= */

enum { VAL_NIL, VAL_INT, VAL_STR, VAL_BOOL };

typedef struct {
    int type;
    int32_t i;
    const char *s;
} value_t;

static value_t val_nil(void)      { value_t v; v.type = VAL_NIL;  v.i = 0; v.s = NULL; return v; }
static value_t val_int(int32_t i) { value_t v; v.type = VAL_INT;  v.i = i; v.s = NULL; return v; }
static value_t val_bool(int b)    { value_t v; v.type = VAL_BOOL; v.i = b ? 1 : 0; v.s = NULL; return v; }
static value_t val_str(const char *s) { value_t v; v.type = VAL_STR; v.i = 0; v.s = s; return v; }

static char *int_to_str(int32_t v) {
    char tmp[12];
    int n = 0, i;
    uint32_t uv = (uint32_t)v;
    char *out;
    if (v < 0) uv = (uint32_t)(~uv + 1u);
    if (uv == 0) tmp[n++] = '0';
    while (uv > 0 && n < 11) { tmp[n++] = (char)('0' + uv % 10); uv /= 10; }
    if (v < 0) tmp[n++] = '-';
    out = (char *)arena_alloc((uint32_t)n + 1);
    if (!out) return "";
    for (i = 0; i < n; i++) out[i] = tmp[n - 1 - i];
    out[n] = '\0';
    return out;
}

static const char *val_to_cstr(value_t v) {
    if (v.type == VAL_STR) return v.s ? v.s : "";
    if (v.type == VAL_BOOL) return v.i ? "true" : "false";
    if (v.type == VAL_INT) return int_to_str(v.i);
    return "nil";
}

static value_t str_concat(value_t l, value_t r) {
    const char *ls = val_to_cstr(l);
    const char *rs = val_to_cstr(r);
    uint32_t llen = (uint32_t)strlen(ls);
    uint32_t rlen = (uint32_t)strlen(rs);
    char *out = (char *)arena_alloc(llen + rlen + 1);
    if (!out) return val_str("");
    byte_copy(out, ls, llen);
    byte_copy(out + llen, rs, rlen);
    out[llen + rlen] = '\0';
    return val_str(out);
}

static int truthy(value_t v) {
    if (v.type == VAL_NIL) return 0;
    if (v.type == VAL_INT || v.type == VAL_BOOL) return v.i != 0;
    if (v.type == VAL_STR) return v.s && v.s[0] != '\0';
    return 0;
}

/* ================= runtime error handling ================= */

static int runtime_error;

static void rt_error(const char *msg) {
    if (runtime_error) return;
    runtime_error = 1;
    console_puts("script: runtime error: ");
    console_puts(msg);
    console_puts("\n");
}

/* ================= scopes ================= */

#define MAX_GLOBALS 32
#define MAX_CALL_DEPTH 16
#define MAX_LOCALS 16

typedef struct { char name[24]; value_t value; int used; } var_t;

static var_t globals[MAX_GLOBALS];

typedef struct { var_t locals[MAX_LOCALS]; int count; } frame_t;
static frame_t frames[MAX_CALL_DEPTH];
static int frame_top = -1; /* -1 = global scope */

static value_t get_var(const char *name) {
    int i;
    if (frame_top >= 0) {
        frame_t *f = &frames[frame_top];
        for (i = 0; i < f->count; i++) if (strcmp(f->locals[i].name, name) == 0) return f->locals[i].value;
    }
    for (i = 0; i < MAX_GLOBALS; i++) if (globals[i].used && strcmp(globals[i].name, name) == 0) return globals[i].value;
    rt_error("undefined variable");
    return val_nil();
}

static void set_var(const char *name, value_t v) {
    int i;
    if (frame_top >= 0) {
        frame_t *f = &frames[frame_top];
        for (i = 0; i < f->count; i++) {
            if (strcmp(f->locals[i].name, name) == 0) { f->locals[i].value = v; return; }
        }
        if (f->count < MAX_LOCALS) {
            var_t *slot = &f->locals[f->count++];
            int j;
            for (j = 0; name[j] && j < 23; j++) slot->name[j] = name[j];
            slot->name[j] = '\0';
            slot->value = v;
            slot->used = 1;
            return;
        }
        rt_error("too many local variables");
        return;
    }
    for (i = 0; i < MAX_GLOBALS; i++) {
        if (globals[i].used && strcmp(globals[i].name, name) == 0) { globals[i].value = v; return; }
    }
    for (i = 0; i < MAX_GLOBALS; i++) {
        if (!globals[i].used) {
            int j;
            for (j = 0; name[j] && j < 23; j++) globals[i].name[j] = name[j];
            globals[i].name[j] = '\0';
            globals[i].value = v;
            globals[i].used = 1;
            return;
        }
    }
    rt_error("too many global variables");
}

/* ================= functions ================= */

#define MAX_FUNCS 16

typedef struct {
    char name[24];
    node_t *params;
    node_t *body;
    int used;
} func_def_t;

static func_def_t funcs[MAX_FUNCS];

static func_def_t *find_func(const char *name) {
    int i;
    for (i = 0; i < MAX_FUNCS; i++) if (funcs[i].used && strcmp(funcs[i].name, name) == 0) return &funcs[i];
    return NULL;
}

static void register_func(node_t *def) {
    func_def_t *f = find_func(def->str);
    if (!f) {
        int i;
        for (i = 0; i < MAX_FUNCS; i++) if (!funcs[i].used) { f = &funcs[i]; break; }
    }
    if (!f) { rt_error("too many function definitions"); return; }
    {
        int j;
        for (j = 0; def->str[j] && j < 23; j++) f->name[j] = def->str[j];
        f->name[j] = '\0';
    }
    f->params = def->a;
    f->body = def->b;
    f->used = 1;
}

/* ================= native ("exec") functions ================= */

static char script_read_buf[4096];

static value_t native_print(value_t *args, int argc) {
    int i;
    for (i = 0; i < argc; i++) {
        if (i > 0) console_puts(" ");
        console_puts(val_to_cstr(args[i]));
    }
    console_puts("\n");
    return val_nil();
}

static value_t native_write(value_t *args, int argc) {
    if (argc < 2) { rt_error("write(name, content) needs 2 arguments"); return val_bool(0); }
    return val_bool(fs_write(val_to_cstr(args[0]), val_to_cstr(args[1])) == 0);
}

static value_t native_read(value_t *args, int argc) {
    int n;
    if (argc < 1) { rt_error("read(name) needs 1 argument"); return val_nil(); }
    n = fs_read(val_to_cstr(args[0]), script_read_buf, sizeof(script_read_buf));
    if (n < 0) return val_nil();
    return val_str(script_read_buf);
}

static value_t native_exists(value_t *args, int argc) {
    fs_stat_t st;
    if (argc < 1) { rt_error("exists(name) needs 1 argument"); return val_bool(0); }
    return val_bool(fs_stat(val_to_cstr(args[0]), &st) == 0);
}

static value_t native_len(value_t *args, int argc) {
    if (argc < 1) { rt_error("len(x) needs 1 argument"); return val_int(0); }
    if (args[0].type == VAL_STR) return val_int((int32_t)strlen(args[0].s ? args[0].s : ""));
    return val_int(0);
}

static value_t native_randdigit(value_t *args, int argc) {
    (void)args; (void)argc;
    return val_int(adc_random_digit());
}

typedef value_t (*native_fn_t)(value_t *args, int argc);
typedef struct { const char *name; native_fn_t fn; } native_def_t;

static const native_def_t natives[] = {
    { "print",     native_print },
    { "write",     native_write },
    { "read",      native_read },
    { "exists",    native_exists },
    { "len",       native_len },
    { "randdigit", native_randdigit },
};
#define NUM_NATIVES ((int)(sizeof(natives) / sizeof(natives[0])))

/* ================= interpreter ================= */

static value_t eval_expr(node_t *n);
static void exec_block(node_t *n, int *did_return, value_t *return_value);

static value_t call_function(const char *name, node_t *arg_exprs) {
    value_t args[8];
    int argc = 0;
    node_t *a;
    int i;

    for (a = arg_exprs; a && argc < 8; a = a->next) args[argc++] = eval_expr(a);
    if (runtime_error) return val_nil();

    for (i = 0; i < NUM_NATIVES; i++) {
        if (strcmp(natives[i].name, name) == 0) return natives[i].fn(args, argc);
    }

    {
        func_def_t *f = find_func(name);
        node_t *p;
        int did_return = 0;
        value_t ret = val_nil();

        if (!f) { rt_error("undefined function"); return val_nil(); }
        if (frame_top + 1 >= MAX_CALL_DEPTH) { rt_error("stack overflow"); return val_nil(); }

        frame_top++;
        frames[frame_top].count = 0;

        i = 0;
        for (p = f->params; p; p = p->next) {
            set_var(p->str, i < argc ? args[i] : val_nil());
            i++;
        }

        exec_block(f->body, &did_return, &ret);

        frame_top--;
        return ret;
    }
}

static value_t eval_expr(node_t *n) {
    if (!n || runtime_error) return val_nil();

    switch (n->type) {
        case N_NUM:  return val_int(n->num);
        case N_STR:  return val_str(n->str);
        case N_BOOL: return val_bool(n->num);
        case N_IDENT: return get_var(n->str);
        case N_ASSIGN: {
            value_t v = eval_expr(n->a);
            set_var(n->str, v);
            return v;
        }
        case N_CALL: return call_function(n->str, n->a);
        case N_UNOP: {
            value_t v = eval_expr(n->a);
            if (n->op == T_MINUS) return val_int(-v.i);
            return val_bool(!truthy(v));
        }
        case N_BINOP: {
            if (n->op == T_AND) return val_bool(truthy(eval_expr(n->a)) && truthy(eval_expr(n->b)));
            if (n->op == T_OR)  return val_bool(truthy(eval_expr(n->a)) || truthy(eval_expr(n->b)));

            {
                value_t l = eval_expr(n->a);
                value_t r = eval_expr(n->b);

                if (n->op == T_PLUS && (l.type == VAL_STR || r.type == VAL_STR)) return str_concat(l, r);

                if (n->op == T_EQ) {
                    if (l.type == VAL_STR || r.type == VAL_STR) return val_bool(strcmp(val_to_cstr(l), val_to_cstr(r)) == 0);
                    return val_bool(l.i == r.i);
                }
                if (n->op == T_NEQ) {
                    if (l.type == VAL_STR || r.type == VAL_STR) return val_bool(strcmp(val_to_cstr(l), val_to_cstr(r)) != 0);
                    return val_bool(l.i != r.i);
                }

                switch (n->op) {
                    case T_PLUS:  return val_int(l.i + r.i);
                    case T_MINUS: return val_int(l.i - r.i);
                    case T_STAR:  return val_int(l.i * r.i);
                    case T_SLASH:
                        if (r.i == 0) { rt_error("division by zero"); return val_int(0); }
                        return val_int(l.i / r.i);
                    case T_PERCENT:
                        if (r.i == 0) { rt_error("division by zero"); return val_int(0); }
                        return val_int(l.i % r.i);
                    case T_CARET: {
                        int32_t result = 1, i;
                        if (r.i < 0) { rt_error("negative exponent not supported"); return val_int(0); }
                        for (i = 0; i < r.i; i++) result *= l.i;
                        return val_int(result);
                    }
                    case T_LT: return val_bool(l.i < r.i);
                    case T_LE: return val_bool(l.i <= r.i);
                    case T_GT: return val_bool(l.i > r.i);
                    case T_GE: return val_bool(l.i >= r.i);
                    default: break;
                }
            }
            return val_nil();
        }
        default: return val_nil();
    }
}

static void exec_stmt(node_t *n, int *did_return, value_t *return_value);

static void exec_block(node_t *n, int *did_return, value_t *return_value) {
    node_t *s;
    if (!n) return;
    for (s = n->a; s; s = s->next) {
        exec_stmt(s, did_return, return_value);
        if (*did_return || runtime_error) return;
    }
}

static void exec_stmt(node_t *n, int *did_return, value_t *return_value) {
    if (!n || *did_return || runtime_error) return;

    switch (n->type) {
        case N_FUNC_DEF:
            register_func(n);
            return;
        case N_EXPR_STMT:
            eval_expr(n->a);
            return;
        case N_BLOCK:
            exec_block(n, did_return, return_value);
            return;
        case N_IF:
            if (truthy(eval_expr(n->a))) exec_block(n->b, did_return, return_value);
            else if (n->c) exec_block(n->c, did_return, return_value);
            return;
        case N_WHILE: {
            int guard = 0;
            while (!runtime_error && truthy(eval_expr(n->a))) {
                exec_block(n->b, did_return, return_value);
                if (*did_return || runtime_error) return;
                if (++guard > 1000000) { rt_error("while loop exceeded iteration limit"); return; }
            }
            return;
        }
        case N_RETURN:
            *return_value = n->a ? eval_expr(n->a) : val_nil();
            *did_return = 1;
            return;
        default:
            return;
    }
}

/* ================= public entry point ================= */

void script_run(const char *source) {
    node_t *program, *tail;
    int i;

    arena_reset();
    parse_error = 0;
    runtime_error = 0;
    frame_top = -1;
    for (i = 0; i < MAX_GLOBALS; i++) globals[i].used = 0;
    for (i = 0; i < MAX_FUNCS; i++) funcs[i].used = 0;

    lex_src = source;
    lex_line = 1;
    lex_next();

    program = new_node(N_BLOCK);
    tail = NULL;
    while (!parse_error && cur.type != T_EOF) {
        node_t *s = parse_statement();
        if (parse_error) break;
        if (!program->a) program->a = s; else tail->next = s;
        tail = s;
    }
    if (parse_error) return;

    {
        int did_return = 0;
        value_t ret;
        exec_block(program, &did_return, &ret);
    }
}
