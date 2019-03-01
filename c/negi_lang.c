#include "negi_lang.h"
#include "negi_lang_internals.h"
#include <assert.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define array_len(X) (sizeof(X) / sizeof(*X))

// ###############################################
// 汎用: デバッグ用
// ###############################################

static __attribute__((noreturn)) void
do_failwith(const char *file_name, int line, const char *message) {
    fprintf(stderr, "FATAL ERROR at %s:%d\n%s\n", file_name, line, message);
    abort();
}

// ###############################################
// 汎用: メモリ
// ###############################################

static void *mem_alloc(int count, int unit) {
    assert(count >= 0 && unit > 0);

    if (count == 0) {
        return NULL;
    }

    return calloc(count, unit);
}

// data をサイズ unit の要素の配列へのポインタとみなして、領域を拡張する。
// いまのキャパシティ (最大の要素数) が *capacity で、そのうち count
// 個が使用中であるとする。 これをキャパシティが new_capacity
// 以上になるように必要なら再確保する。縮めることはない。
static void mem_reserve(void **data, int count, int unit, int *capacity,
                        int new_capacity) {
    assert(data != NULL);
    assert(count >= 0);
    assert(unit > 0);
    assert(capacity != NULL);
    assert(count <= *capacity);
    assert(new_capacity >= 0);

    if (*capacity >= new_capacity) {
        return;
    }

    void *new_data = mem_alloc(new_capacity, unit);
    if (count > 0) {
        assert(*data != NULL);
        memcpy(new_data, *data, count * unit);
    }

    *data = new_data;
    *capacity = new_capacity;
}

// ###############################################
// 汎用: 文字列
// ###############################################

static char *str_slice(const char *str, int l, int r) {
    assert(str != NULL && 0 <= l && l <= r);

    char *slice = (char *)mem_alloc(r - l + 1, sizeof(char));

    strncpy(slice, str + l, r - l);
    slice[r - l] = '\0';
    return slice;
}

static char *str_format(const char *fmt, ...) {
    char buffer[4096];

    va_list ap;
    va_start(ap, fmt);
    int size = vsnprintf(buffer, sizeof(buffer), fmt, ap);
    va_end(ap);

    if (size < 0) {
        failwith("FATAL ERROR str_format");
    }

    return str_slice(buffer, 0, size);
}

// ###############################################
// 汎用: 文字列ビルダー
// ###############################################

static void sb_reserve(StringBuilder *sb, int new_capacity) {
    assert(sb != NULL && new_capacity >= 0);

    if (sb->capacity >= new_capacity) {
        return;
    }

    char *new_data = (char *)mem_alloc(new_capacity + 1, sizeof(char));
    strcpy(new_data, sb->data);

    sb->data = new_data;
    sb->capacity = new_capacity;
}

static void sb_append(StringBuilder *sb, const char *src) {
    int src_size = strlen(src);
    if (src_size == 0) {
        return;
    }

    int capacity = sb->size + src_size;
    if (capacity > sb->capacity) {
        capacity += sb->capacity;
        sb_reserve(sb, capacity);
    }

    assert(sb->data != NULL && sb->capacity >= capacity);

    strcpy(sb->data + sb->size, src);
    sb->size += src_size;
}

static void *sb_new() {
    StringBuilder *sb = mem_alloc(1, sizeof(StringBuilder));
    *sb = (StringBuilder){
        .data = "",
        .size = 0,
        .capacity = 0,
    };
    return sb;
}

static const char *sb_to_str(const StringBuilder *sb) { return sb->data; }

// ###############################################
// ソースコード
// ###############################################

static void src_initialize(Ctx *ctx, const char *src) {
    assert(ctx != NULL && src != NULL);

    int len = strlen(src);
    ctx->src_len = len;
    ctx->src = str_slice(src, 0, len);
}

static const char *src_slice(Ctx *ctx, int l, int r) {
    assert(0 <= l && l <= r && r <= ctx->src_len);
    return str_slice(ctx->src, l, r);
}

// ###############################################
// エラー
// ###############################################

// 入力されたプログラムの問題は実行時エラーとして報告する。(failwith
// は使わない。) エラーの報告には、そのエラーが発生した箇所
// (ソースコードのどのあたりか) を含める。

static void err_add(Ctx *ctx, const char *message, int src_l, int src_r) {
    assert(message != 0 && 0 <= src_l && src_l <= src_r &&
           src_r <= ctx->src_len);

    if (ctx->errs.capacity == ctx->errs.len) {
        mem_reserve((void **)&ctx->errs.data, ctx->errs.len, sizeof(struct Err),
                    &ctx->errs.capacity, 1 + ctx->errs.capacity * 2);
    }

    int err_i = ctx->errs.len++;

    ctx->errs.data[err_i] = (struct Err){
        .message = message,
        .src_l = src_l,
        .src_r = src_r,
    };
}

// 指定された位置の行番号 (y) と列番号 (x) を計算する。
static struct TextPos find_pos(Ctx *ctx, int src_i) {
    assert(ctx != NULL && 0 <= src_i && src_i <= ctx->src_len);

    int y = 0;
    int x = 0;

    for (int i = 0; i < src_i; i++) {
        if (ctx->src[i] == '\n') {
            y++;
            x = 0;
        } else {
            x++;
        }
    }

    return (struct TextPos){
        .y = y,
        .x = x,
    };
}

// エラーの一覧を表す文字列を生成する。
static const char *err_summary(Ctx *ctx) {
    StringBuilder *summary = sb_new();

    for (int err_i = 0; err_i < ctx->errs.len; err_i++) {
        struct Err *err = &ctx->errs.data[err_i];

        struct TextPos pos_l = find_pos(ctx, err->src_l);
        struct TextPos pos_r = find_pos(ctx, err->src_r);

        const char *text = src_slice(ctx, err->src_l, err->src_r);

        sb_append(summary, str_format("%d:%d..%d%d", 1 + pos_l.y, 1 + pos_l.x,
                                      1 + pos_r.y, 1 + pos_l.x));
        sb_append(summary, " near '");
        sb_append(summary, text);
        sb_append(summary, "'\n  ");
        sb_append(summary, err->message);
        sb_append(summary, "\n");
    }

    return sb_to_str(summary);
}

// ###############################################
// 字句解析
// ###############################################

// ソースコードをトークン (単語や記号などのまとまり) 単位に分割する。
// この工程により、後続の構文解析がやりやすくなる。

// トークンのリストは tok_eof で終わるようにする。
// エラーの報告のため、トークンがソースコードのどの位置にあるかを記録しておく。

// -----------------------------------------------
// 文字の種類の判定
// -----------------------------------------------

static bool is_digit(char c) { return '0' <= c && c <= '9'; }

static bool is_alphabet(char c) {
    return ('a' <= c && c <= 'z') || ('A' <= c && c <= 'Z');
}

static bool is_ident_char(char c) {
    return c == '_' || is_alphabet(c) || is_digit(c);
}

static bool is_op_char(char c) { return strchr("+-*/%&|^~!=<>.?:", c) != NULL; }

// -----------------------------------------------
// トークンリスト
// -----------------------------------------------

static void tok_add(Ctx *ctx, enum TokKind kind, int src_l, int src_r) {
    assert(src_l <= src_r && (kind == tok_eof || src_l < src_r));

    if (ctx->toks.len == ctx->toks.capacity) {
        mem_reserve((void **)&ctx->toks.data, ctx->toks.len, sizeof(struct Tok),
                    &ctx->toks.capacity, 1 + ctx->toks.capacity * 2);
    }
    assert(ctx->toks.len < ctx->toks.capacity);

    int tok_i = ctx->toks.len++;

    ctx->toks.data[tok_i] = (struct Tok){
        .kind = kind,
        .src_l = src_l,
        .src_r = src_r,
    };
}

static struct Tok *tok_get(Ctx *ctx, int tok_i) {
    assert(0 <= tok_i && tok_i < ctx->toks.len);
    return &ctx->toks.data[tok_i];
}

static const char *tok_text(Ctx *ctx, int tok_i) {
    struct Tok *tok = tok_get(ctx, tok_i);
    return src_slice(ctx, tok->src_l, tok->src_r);
}

struct TextTokKind {
    char const *text;
    enum TokKind kind;
};

static enum TokKind tok_text_to_kind(const char *text) {
    static struct TextTokKind table[] = {
        {"let", tok_let},       {"if", tok_if},       {"else", tok_else},
        {"while", tok_while},   {"break", tok_break}, {"fun", tok_fun},
        {"return", tok_return},
    };

    for (int i = 0; i < array_len(table); i++) {
        if (strcmp(table[i].text, text) == 0) {
            return table[i].kind;
        }
    }
    return tok_ident;
}

// -----------------------------------------------
// 字句解析
// -----------------------------------------------

static void tokenize(Ctx *ctx) {
    int r = 0;

    while (r < ctx->src_len) {
        // 1回のループで、位置 l から始まる1つのトークンを切り出す。
        // l の次の文字 c からトークンの種類を特定して、
        // そのトークンが広がる範囲の右端まで r を進める。
        // 最終的に、切り出されるトークンは位置 l から r までになるようにする。

        int l = r;

        // 次の文字を変数に入れておく。(先読み)
        char c = ctx->src[r];

        // 空白と改行は無視する。
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
            r++;
            continue;
        }

        if (is_digit(c)) {
            while (r < ctx->src_len && is_digit(ctx->src[r])) {
                r++;
            }

            tok_add(ctx, tok_int, l, r);
            continue;
        }

        if (c == '"') {
            r++;

            while (r < ctx->src_len) {
                char c = ctx->src[r];

                if (c == '\r' || c == '\n') {
                    break;
                }

                if (c == '"') {
                    r++;
                    break;
                }
                r++;
            }

            tok_add(ctx, tok_str, l, r);
            continue;
        }

        if (is_ident_char(c) && !is_digit(c)) {
            r++;
            while (r < ctx->src_len && is_ident_char(ctx->src[r])) {
                r++;
            }

            const char *text = src_slice(ctx, l, r);
            enum TokKind kind = tok_text_to_kind(text);
            tok_add(ctx, kind, l, r);
            continue;
        }

        if (c == '(' || c == ')' || c == '[' || c == ']' || c == '{' ||
            c == '}' || c == ',' || c == ';') {
            r++;

            enum TokKind kind = tok_err;
            if (c == '(')
                kind = tok_paren_l;
            if (c == ')')
                kind = tok_paren_r;
            if (c == '[')
                kind = tok_bracket_l;
            if (c == ']')
                kind = tok_bracket_r;
            if (c == '{')
                kind = tok_brace_l;
            if (c == '}')
                kind = tok_brace_r;
            if (c == ',')
                kind = tok_comma;
            if (c == ';')
                kind = tok_semi;
            assert(kind != tok_err);

            tok_add(ctx, kind, l, r);
            continue;
        }

        if (is_op_char(c)) {
            while (r < ctx->src_len && is_op_char(ctx->src[r])) {
                r++;
            }
            tok_add(ctx, tok_op, l, r);
            continue;
        }

        // このとき、文字 c
        // はトークンとして不正なもの。エラーを表すトークンとして追加する。
        d_trace(str_format("tok_err char = %c", c));
        r++;
        tok_add(ctx, tok_err, l, r);
    }

    assert(r == ctx->src_len);
    tok_add(ctx, tok_eof, r, r);
    return;
}

// ###############################################
// 構文解析
// ###############################################

// トークンのリストに再帰下降構文解析を適用して、抽象構文木を作る。

// -----------------------------------------------
// トークンの種類
// -----------------------------------------------

// トークンが項の始まりを表すか否か。
static bool tok_leads_term(enum TokKind kind) {
    return kind == tok_int || kind == tok_str || kind == tok_ident ||
           kind == tok_paren_l || kind == tok_bracket_l || kind == tok_fun ||
           kind == tok_op;
}

// トークンが文の始まりを表すか否か。
static bool tok_leads_stmt(enum TokKind kind) {
    return tok_leads_term(kind) || kind == tok_let || kind == tok_if ||
           kind == tok_while || kind == tok_break || kind == tok_return;
}

// -----------------------------------------------
// 部分式リスト
// -----------------------------------------------

typedef struct SubExpPair {
    int subexp_l, subexp_r;
} SubExpPair;

static struct SubExpPair subexp_add(Ctx *ctx, int *exp_is, int exp_i_len) {
    if (ctx->subexps.len + exp_i_len > ctx->subexps.capacity) {
        mem_reserve((void **)&ctx->subexps.data, ctx->subexps.len,
                    sizeof(SubExp), &ctx->subexps.capacity,
                    1 + ctx->subexps.capacity * 2 + exp_i_len);
    }
    assert(ctx->subexps.len + exp_i_len <= ctx->subexps.capacity);

    int subexp_l = ctx->subexps.len;
    int subexp_r = ctx->subexps.len + exp_i_len;
    ctx->subexps.len += exp_i_len;

    for (int i = 0; i < exp_i_len; i++) {
        ctx->subexps.data[subexp_l + i] = (SubExp){
            .exp_i = exp_is[i],
        };
    }

    return (SubExpPair){
        .subexp_l = subexp_l,
        .subexp_r = subexp_r,
    };
}

// -----------------------------------------------
// 式リスト
// -----------------------------------------------

static int exp_add(Ctx *ctx, ExpKind kind, int tok_i) {
    if (ctx->exps.len == ctx->exps.capacity) {
        mem_reserve((void **)&ctx->exps.data, ctx->exps.len, sizeof(Exp),
                    &ctx->exps.capacity, 1 + ctx->exps.capacity * 2);
    }

    int exp_i = ctx->exps.len++;
    ctx->exps.data[exp_i] = (Exp){
        .kind = kind,
        .tok_i = tok_i,
    };
    return exp_i;
}

static Exp *exp_get(Ctx *ctx, int exp_i) {
    assert(0 <= exp_i && exp_i < ctx->exps.len);
    return &ctx->exps.data[exp_i];
}

static int exp_add_err(Ctx *ctx, const char *message, int tok_i) {
    int exp_i = exp_add(ctx, exp_err, tok_i);
    exp_get(ctx, exp_i)->str_value = message;
    return exp_i;
}

static int exp_add_int(Ctx *ctx, ExpKind kind, int value, int tok_i) {
    int exp_i = exp_add(ctx, kind, tok_i);
    exp_get(ctx, exp_i)->int_value = value;
    return exp_i;
}

static int exp_add_str(Ctx *ctx, ExpKind kind, const char *value, int tok_i) {
    int exp_i = exp_add(ctx, kind, tok_i);
    exp_get(ctx, exp_i)->str_value = value;
    return exp_i;
}

static int exp_add_bin(Ctx *ctx, OpKind op, int exp_l, int exp_r, int tok_i) {
    int exp_i = exp_add(ctx, exp_op, tok_i);
    exp_get(ctx, exp_i)->exp_l = exp_l;
    exp_get(ctx, exp_i)->exp_r = exp_r;
    return exp_i;
}

static int exp_add_subexps(Ctx *ctx, ExpKind kind, int exp_l, int *exp_is,
                           int exp_i_len, int tok_i) {
    struct SubExpPair subexp = subexp_add(ctx, exp_is, exp_i_len);

    int exp_i = exp_add(ctx, kind, tok_i);
    Exp *exp = exp_get(ctx, exp_i);
    exp->exp_l = exp_l;
    exp->subexp_l = subexp.subexp_l;
    exp->subexp_r = subexp.subexp_r;
    return exp_i;
}

static int exp_add_let(Ctx *ctx, int ident_tok_i, int init_exp_i, int tok_i) {
    int exp_i = exp_add(ctx, exp_let, tok_i);
    exp_get(ctx, exp_i)->int_value = ident_tok_i;
    exp_get(ctx, exp_i)->exp_l = init_exp_i;
    return exp_i;
}

static int exp_add_if(Ctx *ctx, int cond_exp_i, int then_exp_i, int else_exp_i,
                      int tok_i) {
    int exp_i = exp_add(ctx, exp_if, tok_i);
    Exp *exp = exp_get(ctx, exp_i);
    exp->exp_cond = cond_exp_i;
    exp->exp_l = then_exp_i;
    exp->exp_r = else_exp_i;
    return exp_i;
}

static int exp_add_while(Ctx *ctx, int cond_exp_i, int body_exp_i, int tok_i) {
    int exp_i = exp_add(ctx, exp_while, tok_i);
    Exp *exp = exp_get(ctx, exp_i);
    exp->exp_cond = cond_exp_i;
    exp->exp_l = body_exp_i;
    return exp_i;
}

// -----------------------------------------------
// 構文解析
// -----------------------------------------------

static int parse_term(Ctx *ctx, int *tok_i);

static int parse_exp(Ctx *ctx, int *tok_i);

static int parse_atom(Ctx *ctx, int *tok_i) {
    switch (tok_get(ctx, *tok_i)->kind) {
    case tok_int: {
        int value = atol(tok_text(ctx, *tok_i));
        return exp_add_int(ctx, exp_int, value, *tok_i++);
    }
    default: { failwith("Unknown token kind as atom"); }
    }
}

static void parse_eof(Ctx *ctx, int *tok_i) {
    Tok *tok = tok_get(ctx, *tok_i);
    if (tok->kind != tok_eof) {
        err_add(
            ctx,
            str_format(
                "この字句を解釈できませんでした。 (tok_i %d, tok_kind = %d)",
                *tok_i, tok->kind),
            tok->src_l, tok->src_r);
    }
}

static void parse(Ctx *ctx) {
    int exp_i_bad = exp_add_err(ctx, "NOT AN EXPRESSION", 0);
    assert(exp_i_bad == 0);

    int tok_i = 0;
    ctx->exp_i_root = parse_atom(ctx, &tok_i);
    parse_eof(ctx, &tok_i);
}

// ###############################################
// 実行
// ###############################################

void negi_lang_test_util() {
    StringBuilder *sb = sb_new();
    sb_append(sb, "Hello");
    sb_append(sb, ", world!");
    assert(strcmp(sb_to_str(sb), "Hello, world!") == 0);
}

const char *negi_lang_tokenize_dump(const char *src) {
    Ctx *ctx = mem_alloc(1, sizeof(Ctx));

    *ctx = (Ctx){};

    src_initialize(ctx, src);
    tokenize(ctx);

    StringBuilder *sb = sb_new();
    for (int i = 0; i < ctx->toks.len; i++) {
        sb_append(sb, tok_text(ctx, i));
        sb_append(sb, ",");
    }

    return sb_to_str(sb);
}

static void dump_exp(Ctx *ctx, int exp_i, StringBuilder *out) {
    Exp *exp = exp_get(ctx, exp_i);
    switch (exp->kind) {
    case exp_int: {
        sb_append(out, str_format("%d", exp->int_value));
        break;
    }
    default: { failwith("Unknown exp kind"); }
    }
}

const char *negi_lang_parse_dump(const char *src) {
    Ctx *ctx = mem_alloc(1, sizeof(Ctx));

    *ctx = (Ctx){};

    src_initialize(ctx, src);
    tokenize(ctx);
    parse(ctx);

    StringBuilder *sb = sb_new();
    dump_exp(ctx, ctx->exp_i_root, sb);
    return sb_to_str(sb);
}

void _trace(const char *file_name, int line, const char *message) {
    fprintf(stderr, "[%s:%04d] %s\n", file_name, line, message);
}
