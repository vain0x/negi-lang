#include "negi_lang.h"
#include "negi_lang_internals.h"
#include <assert.h>
#include <inttypes.h>
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
// 汎用: 整数のベクタ
// ###############################################

typedef struct VecInt {
    int *data;
    int len;
    int capacity;
} VecInt;

static VecInt *vec_int_new() {
    VecInt *vec = mem_alloc(1, sizeof(VecInt));
    *vec = (VecInt){};
    return vec;
}

static void vec_int_push(VecInt *vec, int value) {
    if (vec->len == vec->capacity) {
        mem_reserve((void **)&vec->data, vec->len, sizeof(int), &vec->capacity,
                    1 + vec->capacity * 2);
    }
    assert(vec->len + 1 <= vec->capacity);

    vec->data[vec->len++] = value;
}

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

static TokKind tok_kind(Ctx *ctx, int tok_i) {
    return tok_get(ctx, tok_i)->kind;
}

static const char *tok_text(Ctx *ctx, int tok_i) {
    struct Tok *tok = tok_get(ctx, tok_i);
    return src_slice(ctx, tok->src_l, tok->src_r);
}

static bool tok_text_equals(Ctx *ctx, int tok_i, const char *expected) {
    // HELP: We could optimize this.
    return strcmp(tok_text(ctx, tok_i), expected) == 0;
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

                if (c == '"' || c == '\r' || c == '\n') {
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

static int bump(int *p) { return (*p)++; }

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

static int exp_add_null(Ctx *ctx, int tok_i) {
    return exp_add_int(ctx, exp_int, 0, tok_i);
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
static int exp_add_return(Ctx *ctx, int exp_l, int tok_i) {
    int exp_i = exp_add(ctx, exp_return, tok_i);
    Exp *exp = exp_get(ctx, exp_i);
    exp->exp_l = exp_l;
    return exp_i;
}

// -----------------------------------------------
// 演算子
// -----------------------------------------------

typedef struct OpLevelOpKindOpTextTuple {
    OpLevel level;
    OpKind kind;
    const char *text;
} OpLevelOpKindOpTextTuple;

static const OpLevelOpKindOpTextTuple op_table[] = {
    {op_level_set, op_set, "="},      {op_level_set, op_set_add, "+="},
    {op_level_set, op_set_sub, "-="}, {op_level_set, op_set_mul, "*="},
    {op_level_set, op_set_div, "/="}, {op_level_set, op_set_mod, "%="},
    {op_level_cmp, op_eq, "=="},      {op_level_cmp, op_ne, "!="},
    {op_level_cmp, op_lt, "<"},       {op_level_cmp, op_le, "<="},
    {op_level_cmp, op_gt, ">"},       {op_level_cmp, op_ge, ">="},
    {op_level_add, op_add, "+"},      {op_level_add, op_sub, "-"},
    {op_level_mul, op_mul, "*"},      {op_level_mul, op_div, "/"},
    {op_level_mul, op_mod, "%"},
};

// -----------------------------------------------
// 構文解析
// -----------------------------------------------

static int parse_bin_l(Ctx *ctx, int *tok_i, OpLevel op_level);

static int parse_term(Ctx *ctx, int *tok_i);

static int parse_exp(Ctx *ctx, int *tok_i);

static int parse_str(Ctx *ctx, int *tok_i) {
    assert(tok_kind(ctx, *tok_i) == tok_str);

    Tok *tok = tok_get(ctx, *tok_i);
    assert(tok->src_r - tok->src_l >= 2);

    const char *text = src_slice(ctx, tok->src_l + 1, tok->src_r - 1);
    return exp_add_str(ctx, tok_str, text, bump(tok_i));
}

static VecInt *parse_list(Ctx *ctx, int *tok_i, int bracket_tok_i) {
    VecInt *exp_is = vec_int_new();

    while (true) {
        if (!tok_leads_term(tok_kind(ctx, *tok_i))) {
            break;
        }

        int exp_i = parse_term(ctx, tok_i);
        vec_int_push(exp_is, exp_i);

        if (tok_kind(ctx, *tok_i) != tok_comma) {
            break;
        }
        bump(tok_i);
    }

    return exp_is;
}

static int parse_array(Ctx *ctx, int *tok_i) {
    assert(tok_kind(ctx, *tok_i) == tok_bracket_l);
    int bracket_tok_i = bump(tok_i);

    VecInt *exp_is = parse_list(ctx, tok_i, bracket_tok_i);
    int exp_i = exp_add_subexps(ctx, exp_array, exp_i_none, exp_is->data,
                                exp_is->len, bracket_tok_i);

    if (tok_kind(ctx, *tok_i) != tok_bracket_r) {
        return exp_add_err(ctx, "角カッコが閉じられていません。",
                           bracket_tok_i);
    }
    bump(tok_i);

    return exp_i;
}

static int parse_block(Ctx *ctx, int *tok_i) {
    assert(tok_kind(ctx, *tok_i) == tok_brace_l);
    int brace_tok_i = bump(tok_i);

    int exp_i = parse_exp(ctx, tok_i);

    if (tok_kind(ctx, *tok_i) != tok_brace_r) {
        return exp_add_err(ctx, "波カッコが閉じられていません。", brace_tok_i);
    }
    bump(tok_i);

    return exp_i;
}

static int parse_fun(Ctx *ctx, int *tok_i) {
    assert(tok_kind(ctx, *tok_i) == tok_fun);
    int fun_tok_i = bump(tok_i);

    if (tok_kind(ctx, *tok_i) != tok_paren_l) {
        return exp_add_err(ctx, "仮引数リストが必要です。", *tok_i);
    }
    int paren_tok_i = bump(tok_i);

    VecInt *exp_is = parse_list(ctx, tok_i, paren_tok_i);

    if (tok_kind(ctx, *tok_i) != tok_paren_r) {
        return exp_add_err(ctx, "丸カッコが閉じられていません。", paren_tok_i);
    }
    bump(tok_i);

    int body_exp_i = tok_kind(ctx, *tok_i) == tok_brace_l
                         ? parse_block(ctx, tok_i)
                         : parse_term(ctx, tok_i);

    int fun_exp_i = exp_add_subexps(ctx, exp_fun, body_exp_i, exp_is->data,
                                    exp_is->len, fun_tok_i);
    return fun_exp_i;
}

static int parse_atom(Ctx *ctx, int *tok_i) {
    switch (tok_kind(ctx, *tok_i)) {
    case tok_int: {
        int value = atol(tok_text(ctx, *tok_i));
        return exp_add_int(ctx, exp_int, value, bump(tok_i));
    }
    case tok_str:
        return parse_str(ctx, tok_i);
    case tok_ident: {
        const char *text = tok_text(ctx, *tok_i);
        return exp_add_str(ctx, exp_ident, text, bump(tok_i));
    }
    case tok_paren_l: {
        int paren_tok_i = bump(tok_i);
        int exp_i = parse_term(ctx, tok_i);
        if (tok_kind(ctx, *tok_i) != tok_paren_r) {
            return exp_add_err(ctx, "丸カッコが閉じられていません。",
                               paren_tok_i);
        }
        bump(tok_i);
        return exp_i;
    }
    case tok_bracket_l:
        return parse_array(ctx, tok_i);
    default: {
        assert(!tok_leads_term(tok_kind(ctx, *tok_i)));
        failwith("Unknown token kind as atom");
    }
    }
}

static int parse_suffix(Ctx *ctx, int *tok_i) {
    int exp_l = parse_atom(ctx, tok_i);

    while (true) {
        TokKind kind = tok_kind(ctx, *tok_i);

        if (kind == tok_paren_l) {
            int paren_tok_i = bump(tok_i);

            VecInt *arg_exp_is = parse_list(ctx, tok_i, paren_tok_i);

            if (tok_kind(ctx, *tok_i) != tok_paren_r) {
                return exp_add_err(ctx, "丸カッコが閉じられていません。",
                                   paren_tok_i);
            }
            bump(tok_i);

            exp_l = exp_add_subexps(ctx, exp_call, exp_l, arg_exp_is->data,
                                    arg_exp_is->len, paren_tok_i);
            continue;
        }

        if (kind == tok_bracket_l) {
            int bracket_tok_i = bump(tok_i);

            int exp_r = parse_term(ctx, tok_i);

            if (tok_kind(ctx, *tok_i) != tok_bracket_r) {
                exp_r = exp_add_err(ctx, "角カッコが閉じられていません。",
                                    bracket_tok_i);
            }
            bump(tok_i);

            exp_l = exp_add_bin(ctx, op_index, exp_l, exp_r, bracket_tok_i);
            continue;
        }

        break;
    }

    return exp_l;
}

static int parse_prefix(Ctx *ctx, int *tok_i) {
    if (tok_kind(ctx, *tok_i) == tok_op) {
        int op_tok_i = *tok_i;

        if (tok_text_equals(ctx, *tok_i, "-")) {
            bump(tok_i);
            int exp_l = exp_add_int(ctx, exp_int, 0, op_tok_i);
            int exp_r = parse_suffix(ctx, tok_i);
            return exp_add_bin(ctx, op_sub, exp_l, exp_r, op_tok_i);
        }

        return exp_add_err(ctx, "この演算子は前置演算子ではありません。",
                           op_tok_i);
    }

    return parse_suffix(ctx, tok_i);
}

static OpKind parse_op(Ctx *ctx, int *tok_i, OpLevel op_level) {
    if (tok_kind(ctx, *tok_i) != tok_op) {
        return op_err;
    }

    for (int i = 0; i < array_len(op_table); i++) {
        if (op_table[i].level != op_level) {
            continue;
        }
        if (!tok_text_equals(ctx, *tok_i, op_table[i].text)) {
            continue;
        }
        return op_table[i].kind;
    }
    return op_err;
}

static int parse_bin_next(Ctx *ctx, int *tok_i, OpLevel op_level) {
    if (op_level == op_level_mul) {
        return parse_prefix(ctx, tok_i);
    }
    return parse_bin_l(ctx, tok_i, op_level + 1);
}

static int parse_bin_l(Ctx *ctx, int *tok_i, OpLevel op_level) {
    int exp_l = parse_bin_next(ctx, tok_i, op_level);

    while (true) {
        if (tok_kind(ctx, *tok_i) != tok_op) {
            break;
        }

        OpKind op = parse_op(ctx, tok_i, op_level);
        if (op == op_err) {
            break;
        }
        int op_tok_i = bump(tok_i);

        int exp_r = parse_bin_next(ctx, tok_i, op_level);

        exp_l = exp_add_bin(ctx, op, exp_l, exp_r, op_tok_i);
    }

    return exp_l;
}

static int parse_bin_set(Ctx *ctx, int *tok_i) {
    int exp_l = parse_bin_l(ctx, tok_i, op_level_cmp);

    if (tok_kind(ctx, *tok_i) == tok_op) {
        int op_tok_i = *tok_i;
        OpKind op = parse_op(ctx, tok_i, op_level_set);
        if (op != op_err) {
            bump(tok_i);

            int exp_r = parse_term(ctx, tok_i);
            exp_l = exp_add_bin(ctx, op, exp_l, exp_r, op_tok_i);
        }
    }

    return exp_l;
}

static int parse_cond(Ctx *ctx, int *tok_i) {
    int cond_exp_i = parse_bin_set(ctx, tok_i);

    if (!(tok_kind(ctx, *tok_i) == tok_op &&
          tok_text_equals(ctx, *tok_i, "?"))) {
        return cond_exp_i;
    }
    int question_tok_i = bump(tok_i);

    int then_exp_i = parse_term(ctx, tok_i);

    if (!(tok_kind(ctx, *tok_i) == tok_op &&
          tok_text_equals(ctx, *tok_i, ":"))) {
        return exp_add_err(ctx, "対応するコロンがありません。", question_tok_i);
    }
    bump(tok_i);

    int else_exp_i = parse_term(ctx, tok_i);

    return exp_add_if(ctx, cond_exp_i, then_exp_i, else_exp_i, question_tok_i);
}

int parse_term(Ctx *ctx, int *tok_i) {
    assert(tok_leads_term(tok_kind(ctx, *tok_i)));

    TokKind kind = tok_kind(ctx, *tok_i);
    if (kind == tok_fun) {
        return parse_fun(ctx, tok_i);
    }
    return parse_cond(ctx, tok_i);
}

static int parse_let(Ctx *ctx, int *tok_i) {
    assert(tok_kind(ctx, *tok_i) == tok_let);

    int let_tok_i = bump(tok_i);

    if (tok_kind(ctx, *tok_i) != tok_ident) {
        return exp_add_err(ctx, "変数名が必要です。", *tok_i);
    }
    int ident_tok_i = bump(tok_i);

    if (!(tok_kind(ctx, *tok_i) == tok_op &&
          tok_text_equals(ctx, *tok_i, "="))) {
        return exp_add_err(ctx, "'=' が必要です。", *tok_i);
    }
    bump(tok_i);

    int init_exp_i = parse_term(ctx, tok_i);

    return exp_add_let(ctx, ident_tok_i, init_exp_i, let_tok_i);
}

static int parse_if(Ctx *ctx, int *tok_i) {
    assert(tok_kind(ctx, *tok_i) == tok_if);

    int if_tok_i = bump(tok_i);

    if (tok_kind(ctx, *tok_i) != tok_paren_l) {
        return exp_add_err(ctx, "丸カッコが必要です。", *tok_i);
    }
    int cond_exp_i = parse_term(ctx, tok_i);

    if (tok_kind(ctx, *tok_i) != tok_brace_l) {
        return exp_add_err(ctx, "波カッコが必要です。", *tok_i);
    }
    int then_exp_i = parse_block(ctx, tok_i);

    int else_exp_i;
    if (tok_kind(ctx, *tok_i) == tok_else) {
        bump(tok_i);

        if (tok_kind(ctx, *tok_i) == tok_if) {
            else_exp_i = parse_if(ctx, tok_i);
        } else if (tok_kind(ctx, *tok_i) == tok_brace_l) {
            else_exp_i = parse_block(ctx, tok_i);
        } else {
            else_exp_i =
                exp_add_err(ctx, "if または波カッコが必要です。", *tok_i);
        }
    } else {
        // if (p) { x } ---> if (p) { x } else { null }
        else_exp_i = exp_add_null(ctx, if_tok_i);
    }

    return exp_add_if(ctx, cond_exp_i, then_exp_i, else_exp_i, if_tok_i);
}

static int parse_while(Ctx *ctx, int *tok_i) {
    assert(tok_kind(ctx, *tok_i) == tok_while);
    int while_tok_i = bump(tok_i);

    if (tok_kind(ctx, *tok_i) != tok_paren_l) {
        return exp_add_err(ctx, "丸カッコが必要です。", *tok_i);
    }
    int cond_exp_i = parse_atom(ctx, tok_i);

    if (tok_kind(ctx, *tok_i) != tok_brace_l) {
        return exp_add_err(ctx, "波カッコが必要です。", *tok_i);
    }
    int body_exp_i = parse_block(ctx, tok_i);

    return exp_add_while(ctx, cond_exp_i, body_exp_i, while_tok_i);
}

static int parse_break(Ctx *ctx, int *tok_i) {
    assert(tok_kind(ctx, *tok_i) == tok_break);
    int break_tok_i = bump(tok_i);

    return exp_add(ctx, exp_break, break_tok_i);
}

static int parse_return(Ctx *ctx, int *tok_i) {
    assert(tok_kind(ctx, *tok_i) == tok_return);
    int return_tok_i = bump(tok_i);

    int exp_l = tok_leads_term(tok_kind(ctx, *tok_i))
                    ? parse_term(ctx, tok_i)
                    : exp_add_null(ctx, return_tok_i);

    return exp_add_return(ctx, exp_l, return_tok_i);
}

int parse_stmt(Ctx *ctx, int *tok_i) {
    assert(tok_leads_stmt(tok_kind(ctx, *tok_i)));

    switch (tok_kind(ctx, *tok_i)) {
    case tok_let:
        return parse_let(ctx, tok_i);
    case tok_if:
        return parse_if(ctx, tok_i);
    case tok_while:
        return parse_while(ctx, tok_i);
    case tok_break:
        return parse_break(ctx, tok_i);
    case tok_return:
        return parse_return(ctx, tok_i);
    default:
        return parse_term(ctx, tok_i);
    }
}

static int parse_semi(Ctx *ctx, int *tok_i) {
    while (tok_kind(ctx, *tok_i) == tok_semi) {
        bump(tok_i);
    }

    if (!tok_leads_stmt(tok_kind(ctx, *tok_i))) {
        return exp_add_null(ctx, *tok_i);
    }

    int exp_l = parse_stmt(ctx, tok_i);
    while (true) {
        int semi_tok_i = *tok_i;

        while (tok_kind(ctx, *tok_i) == tok_semi) {
            bump(tok_i);
        }

        if (!tok_leads_stmt(tok_kind(ctx, *tok_i))) {
            break;
        }

        int exp_r = parse_stmt(ctx, tok_i);

        exp_l = exp_add_bin(ctx, op_semi, exp_l, exp_r, semi_tok_i);
    }
    return exp_l;
}

static int parse_exp(Ctx *ctx, int *tok_i) {
    int exp_l = parse_semi(ctx, tok_i);

    // 結果を捨てる。
    int exp_r = exp_add_null(ctx, *tok_i);
    exp_l = exp_add_bin(ctx, op_semi, exp_l, exp_r, *tok_i);

    return exp_l;
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
    int exp_i = exp_add_err(ctx, "NOT AN EXPRESSION", 0);
    assert(exp_i == exp_i_none);

    int tok_i = 0;
    ctx->exp_i_root = parse_semi(ctx, &tok_i);
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
    case exp_err: {
        sb_append(out, str_format("err '%s' \"%s\"", tok_text(ctx, exp->tok_i),
                                  exp->str_value));
        break;
    }
    case exp_int: {
        sb_append(out, str_format("%d", exp->int_value));
        break;
    }
    case exp_str: {
        sb_append(out, str_format("\"%s\"", exp->str_value));
        break;
    }
    case exp_ident: {
        sb_append(out, exp->str_value);
        break;
    }
    default: {
        sb_append(out, "(");

        {
            const char *text = tok_text(ctx, exp->tok_i);
            if (strcmp(text, "(") == 0) {
                text = "paren";
            } else if (strcmp(text, "[") == 0) {
                text = "bracket";
            } else if (strcmp(text, "{") == 0) {
                text = "brace";
            } else if (strcmp(text, "}") == 0) {
                text = ";";
            }
            sb_append(out, text);
        }

        if (exp->str_value != NULL) {
            sb_append(out, str_format(" \"%s\"", exp->str_value));
        }
        if (exp->exp_cond != exp_i_none) {
            sb_append(out, " ");
            dump_exp(ctx, exp->exp_cond, out);
        }
        if (exp->exp_l != exp_i_none) {
            sb_append(out, " ");
            dump_exp(ctx, exp->exp_l, out);
        }
        if (exp->exp_r != exp_i_none) {
            sb_append(out, " ");
            dump_exp(ctx, exp->exp_r, out);
        }
        for (int i = exp->subexp_l; i < exp->subexp_r; i++) {
            sb_append(out, " ");
            dump_exp(ctx, ctx->subexps.data[i].exp_i, out);
        }

        sb_append(out, ")");
        break;
    }
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
