#include "negi_lang.h"
#include "negi_lang_internals.h"
#include <assert.h>
#include <inttypes.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define array_len(X) (sizeof(X) / sizeof(*X))

static void extern_fun_builtin(Ctx *ctx);

// ###############################################
// 汎用: デバッグ用
// ###############################################

static __attribute__((noreturn)) void
do_failwith(const char *file_name, int line, const char *message) {
    fprintf(stderr, "FATAL ERROR at %s:%d\n%s\n", file_name, line, message);
    abort();
}

static void do_trace(const char *file_name, int line, const char *message) {
    fprintf(stderr, "[%s:%04d] %s\n", file_name, line, message);
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
// 汎用: ベクタ
// ###############################################

static void vec_grow(void **data, int len, int *capacity, int unit,
                     int grow_size) {
    assert(data != NULL);
    assert(capacity != NULL);
    assert(0 <= len && len <= *capacity);
    assert(grow_size >= 0);

    if (len + grow_size > *capacity) {
        int new_capacity = *capacity * 2 + grow_size;
        mem_reserve(data, len, unit, capacity, new_capacity);
    }

    assert(len + grow_size <= *capacity);
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
    vec_grow((void **)&vec->data, vec->len, &vec->capacity, sizeof(int), 1);
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

    vec_grow((void **)&ctx->errs.data, ctx->errs.len, &ctx->errs.capacity,
             sizeof(Err), 1);

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

        sb_append(summary, str_format("%d:%d..%d:%d", 1 + pos_l.y, 1 + pos_l.x,
                                      1 + pos_r.y, 1 + pos_r.x));
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

    vec_grow((void **)&ctx->toks.data, ctx->toks.len, &ctx->toks.capacity,
             sizeof(Tok), 1);
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
    ctx->tok_i_root = ctx->toks.len;

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
        trace(str_format("tok_err char = %c", c));
        r++;
        tok_add(ctx, tok_err, l, r);
    }

    assert(r == ctx->src_len);
    ctx->tok_i_eof = ctx->toks.len;
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
    vec_grow((void **)&ctx->subexps.data, ctx->subexps.len,
             &ctx->subexps.capacity, sizeof(SubExp), exp_i_len);
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

static SubExp *subexp_get(Ctx *ctx, int subexp_i) {
    assert(0 <= subexp_i && subexp_i < ctx->subexps.len);
    return &ctx->subexps.data[subexp_i];
}

// -----------------------------------------------
// 式リスト
// -----------------------------------------------

static int exp_add(Ctx *ctx, ExpKind kind, int tok_i) {
    vec_grow((void **)&ctx->exps.data, ctx->exps.len, &ctx->exps.capacity,
             sizeof(Exp), 1);

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
    Exp *exp = exp_get(ctx, exp_i);
    exp->int_value = (int)op;
    exp->exp_l = exp_l;
    exp->exp_r = exp_r;
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
    return exp_add_str(ctx, exp_str, text, bump(tok_i));
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
    case tok_eof:
        return exp_add_err(ctx, "式が必要です。", *tok_i);
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
                return exp_add_err(ctx, "角カッコが閉じられていません。",
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
    assert(tok_kind(ctx, *tok_i) == tok_op);

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
    TokKind kind = tok_kind(ctx, *tok_i);

    if (!tok_leads_term(kind)) {
        return exp_add_err(ctx, "式が必要です。", *tok_i);
    }

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
        err_add(ctx, str_format("この字句を解釈できませんでした。"), tok->src_l,
                tok->src_r);
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
// コード生成
// ###############################################

// -----------------------------------------------
// 演算子
// -----------------------------------------------

typedef struct SetOpOpPair {
    OpKind set_op;
    OpKind op;
} SetOpOpPair;

static const SetOpOpPair set_ops[] = {
    {op_set_add, op_add}, {op_set_sub, op_sub}, {op_set_mul, op_mul},
    {op_set_div, op_div}, {op_set_mod, op_mod},
};

static bool op_find_op_by_set_op(OpKind set_op, OpKind *op) {
    for (int i = 0; i < array_len(set_ops); i++) {
        if (set_ops[i].set_op == set_op) {
            *op = set_ops[i].op;
            return true;
        }
    }
    return false;
}

static bool op_is_set_op(OpKind op) {
    OpKind result;
    return op_find_op_by_set_op(op, &result);
}

// -----------------------------------------------
// ラベルリスト
// -----------------------------------------------

static int label_add(Ctx *ctx) {
    vec_grow((void **)&ctx->labels.data, ctx->labels.len, &ctx->labels.capacity,
             sizeof(Label), 1);

    int label_i = ctx->labels.len++;
    ctx->labels.data[label_i].cmd_i = -1;
    return label_i;
}

static Label *label_get(Ctx *ctx, int label_i) {
    assert(0 <= label_i && label_i < ctx->labels.len);
    return &ctx->labels.data[label_i];
}

static void label_set(Ctx *ctx, int label_i, int cmd_i) {
    Label *label = label_get(ctx, label_i);
    assert(label->cmd_i < 0);

    label->cmd_i = cmd_i;
}

// -----------------------------------------------
// スコープリスト
// -----------------------------------------------

static int scope_add(Ctx *ctx, int parent, int tok_i) {
    vec_grow((void **)&ctx->scopes.data, ctx->scopes.len, &ctx->scopes.capacity,
             sizeof(Scope), 1);

    int scope_i = ctx->scopes.len++;
    ctx->scopes.data[scope_i] = (Scope){
        .parent = parent,
        .tok_i = tok_i,
    };
    return scope_i;
}

static Scope *scope_get(Ctx *ctx, int scope_i) {
    assert(0 <= scope_i && scope_i < ctx->scopes.len);
    return &ctx->scopes.data[scope_i];
}

static int scope_add_global(Ctx *ctx, int tok_i) {
    return scope_add(ctx, -1, tok_i);
}

static void scope_push(Ctx *ctx, int tok_i) {
    int scope_i = scope_add(ctx, ctx->scope_i_current, tok_i);
    ctx->scope_i_current = scope_i;
}

static void scope_pop(Ctx *ctx) {
    Scope *scope = scope_get(ctx, ctx->scope_i_current);

    assert(scope->parent >= 0);
    ctx->scope_i_current = scope->parent;
}

// -----------------------------------------------
// ローカルリスト
// -----------------------------------------------

static int local_add(Ctx *ctx, const char *ident, int scope_i, int tok_i) {
    vec_grow((void **)&ctx->locals.data, ctx->locals.len, &ctx->locals.capacity,
             sizeof(Local), 1);

    int index = scope_get(ctx, scope_i)->len++;

    int local_i = ctx->locals.len++;
    ctx->locals.data[local_i] = (Local){
        .ident = ident,
        .scope_i = scope_i,
        .index = index,
        .tok_i = tok_i,
    };
    return local_i;
}

static Local *local_get(Ctx *ctx, int local_i) {
    assert(0 <= local_i && local_i < ctx->locals.len);
    return &ctx->locals.data[local_i];
}

static int local_add_var(Ctx *ctx, const char *ident, int tok_i) {
    return local_add(ctx, ident, ctx->scope_i_current, tok_i);
}

static bool local_find_var(Ctx *ctx, const char *ident, int tok_i,
                           int *local_i) {
    int scope_i = ctx->scope_i_current;
    while (true) {
        for (int i = 0; i < ctx->locals.len; i++) {
            Local *local = local_get(ctx, i);
            if (local->scope_i != scope_i) {
                continue;
            }
            if (strcmp(local->ident, ident) != 0) {
                continue;
            }
            *local_i = i;
            return true;
        }

        Scope *scope = scope_get(ctx, scope_i);
        assert(scope_i != scope->parent);
        if (scope->parent < 0) {
            return false;
        }
        scope_i = scope->parent;
    }
}

// -----------------------------------------------
// 関数リスト
// -----------------------------------------------

static int fun_add(Ctx *ctx, FunKind kind, const char *name) {
    vec_grow((void **)&ctx->funs.data, ctx->funs.len, &ctx->funs.capacity,
             sizeof(Fun), 1);

    int fun_i = ctx->funs.len++;
    ctx->funs.data[fun_i] = (Fun){
        .kind = kind,
        .name = name,
    };
    return fun_i;
}

static Fun *fun_get(Ctx *ctx, int fun_i) {
    assert(0 <= fun_i && fun_i < ctx->funs.len);
    return &ctx->funs.data[fun_i];
}

static int fun_add_closure(Ctx *ctx, int scope_i, int label_i) {
    int fun_i =
        fun_add(ctx, fun_kind_closure, str_format("<anonymous: %d>", label_i));

    Fun *fun = fun_get(ctx, fun_i);
    fun->scope_i = scope_i;
    fun->label_i = label_i;

    return fun_i;
}

// -----------------------------------------------
// 外部関数リスト
// -----------------------------------------------

static int extern_fun_add(Ctx *ctx, const char *name, extern_fun_t fun) {
    vec_grow((void **)&ctx->extern_funs.data, ctx->extern_funs.len,
             &ctx->extern_funs.capacity, sizeof(ExternFun), 1);

    int extern_fun_i = ctx->extern_funs.len++;
    ctx->extern_funs.data[extern_fun_i] = (ExternFun){
        .name = name,
        .fun = fun,
    };
    return extern_fun_i;
}

static ExternFun *extern_fun_get(Ctx *ctx, int extern_fun_i) {
    assert(0 <= extern_fun_i && extern_fun_i < ctx->extern_funs.len);
    return &ctx->extern_funs.data[extern_fun_i];
}

static bool extern_fun_find(Ctx *ctx, const char *name, int *extern_fun_i) {
    for (int i = 0; i < ctx->extern_funs.len; i++) {
        if (strcmp(ctx->extern_funs.data[i].name, name) == 0) {
            *extern_fun_i = i;
            return true;
        }
    }
    return false;
}

// -----------------------------------------------
// ループスタック
// -----------------------------------------------

static int loop_push(Ctx *ctx, int break_label_i) {
    vec_grow((void **)&ctx->loops.data, ctx->loops.len, &ctx->loops.capacity,
             sizeof(Loop), 1);

    int loop_i = ctx->loops.len++;
    ctx->loops.data[loop_i] = (Loop){
        .break_label_i = break_label_i,
    };

    return loop_i;
}

static void loop_pop(Ctx *ctx) {
    assert(ctx->loops.len >= 1);
    ctx->loops.len--;
}

static Loop *loop_current_or_null(Ctx *ctx) {
    if (ctx->loops.len == 0) {
        return NULL;
    }
    return &ctx->loops.data[ctx->loops.len - 1];
}

// -----------------------------------------------
// 命令リスト
// -----------------------------------------------

static int cmd_do_add(Ctx *ctx, Cmd cmd) {
    vec_grow((void **)&ctx->cmds.data, ctx->cmds.len, &ctx->cmds.capacity,
             sizeof(Cmd), 1);

    int cmd_i = ctx->cmds.len++;
    ctx->cmds.data[cmd_i] = cmd;
    return cmd_i;
}

static Cmd *cmd_get(Ctx *ctx, int cmd_i) {
    assert(0 <= cmd_i && cmd_i < ctx->cmds.len);
    return &ctx->cmds.data[cmd_i];
}

static void cmd_add_err(Ctx *ctx, const char *message, int tok_i) {
    cmd_do_add(ctx, (Cmd){
                        .kind = cmd_err,
                        .str = message,
                        .tok_i = tok_i,
                    });
}

static void cmd_add_int(Ctx *ctx, CmdKind kind, int value, int tok_i) {
    cmd_do_add(ctx, (Cmd){
                        .kind = kind,
                        .x = value,
                        .tok_i = tok_i,
                    });
}

static void cmd_add_str(Ctx *ctx, CmdKind kind, const char *str, int tok_i) {
    cmd_do_add(ctx, (Cmd){
                        .kind = kind,
                        .str = str,
                        .tok_i = tok_i,
                    });
}

static void cmd_add(Ctx *ctx, CmdKind kind, int tok_i) {
    cmd_add_int(ctx, kind, 0, tok_i);
}

static void cmd_add_closure(Ctx *ctx, int fun_i, int tok_i) {
    assert(0 <= fun_i && fun_i < ctx->funs.len);
    cmd_add_int(ctx, cmd_push_closure, fun_i, tok_i);
}

static void cmd_add_local_var(Ctx *ctx, int index, int scope_i, int tok_i) {
    cmd_do_add(ctx, (Cmd){
                        .kind = cmd_local_var,
                        .x = index,
                        .scope_i = scope_i,
                        .tok_i = tok_i,
                    });
}

static void cmd_add_op(Ctx *ctx, OpKind op, int tok_i) {
    cmd_add_int(ctx, cmd_op, op, tok_i);
}

static void cmd_add_label(Ctx *ctx, int label_i, int tok_i) {
    assert(0 <= label_i && label_i < ctx->labels.len);
    cmd_add_int(ctx, cmd_label, label_i, tok_i);
}

static void cmd_add_jump_unless(Ctx *ctx, int label_i, int tok_i) {
    assert(0 <= label_i && label_i < ctx->labels.len);
    cmd_add_int(ctx, cmd_jump_unless, label_i, tok_i);
}

static void cmd_add_null(Ctx *ctx, int tok_i) {
    cmd_add_int(ctx, cmd_push_int, 0, tok_i);
}

// !x ---> x == null
static void cmd_add_negate(Ctx *ctx, int tok_i) {
    cmd_add_null(ctx, tok_i);
    cmd_add_op(ctx, op_eq, tok_i);
}

// goto l ---> null; jump_unless l
static void cmd_add_goto(Ctx *ctx, int label_i, int tok_i) {
    cmd_add_null(ctx, tok_i);
    cmd_add_jump_unless(ctx, label_i, tok_i);
}

// -----------------------------------------------
// コード生成
// -----------------------------------------------

#define xkind exp_kind(ctx, exp_i)
#define defexp Exp *exp = exp_get(ctx, exp_i)

static void gen_exp(Ctx *ctx, int exp_i);

static void gen_lval(Ctx *ctx, int exp_i);

static void gen_ident(Ctx *ctx, int exp_i, bool lval) {
    defexp;
    assert(exp->kind == exp_ident);
    const char *name = exp->str_value;
    int tok_i = exp->tok_i;

    int local_i;
    if (local_find_var(ctx, name, tok_i, &local_i)) {
        Local *local = local_get(ctx, local_i);
        cmd_add_local_var(ctx, local->index, local->scope_i, tok_i);

        if (!lval) {
            cmd_add(ctx, cmd_cell_get, tok_i);
        }
        return;
    }

    if (!lval) {
        int extern_fun_i;
        if (extern_fun_find(ctx, name, &extern_fun_i)) {
            cmd_add_int(ctx, cmd_push_extern, extern_fun_i, tok_i);
            return;
        }
    }

    cmd_add_err(ctx, "未定義の変数を使用しています。", tok_i);
}

static void gen_array(Ctx *ctx, int exp_i) {
    defexp;
    assert(exp->kind == exp_array);
    int len = exp->subexp_r - exp->subexp_l;
    int tok_i = exp->tok_i;

    cmd_add_int(ctx, cmd_push_array, len, tok_i);
    for (int i = exp->subexp_l; i < exp->subexp_r; i++) {
        gen_exp(ctx, subexp_get(ctx, i)->exp_i);
        cmd_add_op(ctx, op_array_push, tok_i);
    }
}

static void gen_call(Ctx *ctx, int exp_i) {
    defexp;
    assert(exp->kind == exp_call);
    int len = exp->subexp_r - exp->subexp_l;
    int tok_i = exp->tok_i;

    gen_exp(ctx, exp->exp_l);
    for (int i = exp->subexp_l; i < exp->subexp_r; i++) {
        gen_exp(ctx, subexp_get(ctx, i)->exp_i);
    }
    cmd_add_int(ctx, cmd_call, len, tok_i);
}

static void gen_set(Ctx *ctx, int exp_i) {
    defexp;
    assert(exp->kind == exp_op);
    assert(exp->int_value == op_set);

    gen_lval(ctx, exp->exp_l);
    gen_exp(ctx, exp->exp_r);
    cmd_add(ctx, cmd_cell_set, exp->tok_i);
}

static void gen_set_op(Ctx *ctx, int exp_i) {
    defexp;
    assert(exp->kind == exp_op);
    OpKind set_op = (OpKind)exp->int_value;
    int tok_i = exp->tok_i;

    OpKind op;
    int ok = op_find_op_by_set_op(set_op, &op);
    assert(ok);

    gen_lval(ctx, exp->exp_l);

    // 左辺の参照セルを複製して値を取り出す。
    // スタック上は、左辺の参照セル、左辺の値、という並びになる。
    cmd_add(ctx, cmd_dup, tok_i);
    cmd_add(ctx, cmd_cell_get, tok_i);

    // スタック上は、左辺の値、右辺の値、という並びになる。
    gen_exp(ctx, exp->exp_r);
    cmd_add_op(ctx, op, tok_i);

    // スタックの上は、左辺の参照セル、演算結果、という並びになる。
    cmd_add(ctx, cmd_cell_set, tok_i);
}

static void gen_op(Ctx *ctx, int exp_i, bool lval) {
    defexp;
    assert(exp->kind == exp_op);
    OpKind op = exp->int_value;
    int tok_i = exp->tok_i;

    if (op == op_semi) {
        gen_exp(ctx, exp->exp_l);
        cmd_add(ctx, cmd_pop, tok_i);
        gen_exp(ctx, exp->exp_r);
        return;
    }
    if (op == op_set) {
        gen_set(ctx, exp_i);
        return;
    }
    if (op_is_set_op(op)) {
        gen_set_op(ctx, exp_i);
        return;
    }

    gen_exp(ctx, exp->exp_l);
    gen_exp(ctx, exp->exp_r);

    if (op == op_ne) {
        // l != r ---> !(l == r)
        cmd_add_op(ctx, op_eq, tok_i);
        cmd_add_negate(ctx, tok_i);
        return;
    }
    if (op == op_le) {
        // l <= r ---> !(r < l)
        cmd_add(ctx, cmd_swap, tok_i);
        cmd_add_op(ctx, op_lt, tok_i);
        cmd_add_negate(ctx, tok_i);
        return;
    }
    if (op == op_gt) {
        // l > r ---> r < l
        cmd_add(ctx, cmd_swap, tok_i);
        cmd_add_op(ctx, op_lt, tok_i);
        return;
    }
    if (op == op_ge) {
        // l >= r ---> !(l < r)
        cmd_add_op(ctx, op_lt, tok_i);
        cmd_add_negate(ctx, tok_i);
        return;
    }

    if (op == op_index && lval) {
        cmd_add_op(ctx, op_index_ref, tok_i);
        return;
    }

    cmd_add_op(ctx, op, tok_i);
}

static void gen_fun(Ctx *ctx, int exp_i) {
    defexp;
    assert(exp->kind == exp_fun);
    int body_exp_i = exp->exp_l;

    int body_label_i = label_add(ctx);
    int next_label_i = label_add(ctx);

    // 関数本体が実行されないようにスキップする。
    cmd_add_goto(ctx, next_label_i, exp->tok_i);

    // 関数の入り口
    cmd_add_label(ctx, body_label_i, exp->tok_i);

    scope_push(ctx, exp->tok_i);
    int scope_i = ctx->scope_i_current;

    // 仮引数リストを解析する。
    for (int subexp_i = exp->subexp_l; subexp_i < exp->subexp_r; subexp_i++) {
        Exp *param = exp_get(ctx, subexp_get(ctx, subexp_i)->exp_i);
        if (param->kind != exp_ident) {
            cmd_add_err(ctx, "仮引数は識別子でなければいけません。",
                        param->tok_i);
            continue;
        }

        local_add_var(ctx, param->str_value, param->tok_i);
    }

    // 関数本体を解析する。
    gen_exp(ctx, body_exp_i);
    cmd_add(ctx, cmd_return, exp->tok_i);

    scope_pop(ctx);
    int fun_i = fun_add_closure(ctx, scope_i, body_label_i);

    cmd_add_label(ctx, next_label_i, exp->tok_i);
    cmd_add_closure(ctx, fun_i, exp->tok_i);
}

static void gen_let(Ctx *ctx, int exp_i) {
    defexp;
    assert(exp->kind == exp_let);
    int ident_tok_i = exp->int_value;
    const char *ident = tok_text(ctx, ident_tok_i);

    gen_exp(ctx, exp->exp_l);

    int local_i = local_add_var(ctx, ident, ident_tok_i);
    Local *local = local_get(ctx, local_i);

    // 右辺の値、左辺の参照セル、という順番でスタックに積む。
    // 代入式とは逆。swap が必要になる。
    cmd_add_local_var(ctx, local->index, local->scope_i, exp->tok_i);
    cmd_add(ctx, cmd_swap, exp->tok_i);
    cmd_add(ctx, cmd_cell_set, exp->tok_i);
}

static void gen_if(Ctx *ctx, int exp_i) {
    defexp;
    assert(exp->kind == exp_if);
    int tok_i = exp->tok_i;

    int else_label_i = label_add(ctx);
    int end_label_i = label_add(ctx);

    // do cond; if false, goto l_else
    gen_exp(ctx, exp->exp_cond);
    cmd_add_jump_unless(ctx, else_label_i, tok_i);

    // do then_clause; goto l_end
    gen_exp(ctx, exp->exp_l);
    cmd_add_goto(ctx, end_label_i, tok_i);

    // l_else: do else_clause
    cmd_add_label(ctx, else_label_i, tok_i);
    gen_exp(ctx, exp->exp_r);

    // l_end:
    cmd_add_label(ctx, end_label_i, tok_i);
}

// スタックに何らかの値をちょうど1つ積んだ状態で終了するように気をつける。
static void gen_while(Ctx *ctx, int exp_i) {
    defexp;
    assert(exp->kind == exp_while);
    int cond_exp_i = exp->exp_cond;
    int body_exp_i = exp->exp_l;
    int tok_i = exp->tok_i;

    int continue_label_i = label_add(ctx);
    int break_label_i = label_add(ctx);

    loop_push(ctx, break_label_i);

    // l_continue: do cond; if false, break
    cmd_add_label(ctx, continue_label_i, tok_i);

    gen_exp(ctx, cond_exp_i);
    cmd_add_jump_unless(ctx, break_label_i, tok_i);

    // do body; pop; continue
    gen_exp(ctx, body_exp_i);
    cmd_add(ctx, cmd_pop, tok_i);

    cmd_add_goto(ctx, continue_label_i, tok_i);

    // l_break: push null
    cmd_add_label(ctx, break_label_i, tok_i);
    cmd_add_null(ctx, tok_i);

    loop_pop(ctx);
}

static void gen_break(Ctx *ctx, int exp_i) {
    defexp;
    assert(exp->kind == exp_break);

    Loop *loop = loop_current_or_null(ctx);
    if (loop == NULL) {
        cmd_add_err(ctx, "ループの外側では break を使用できません。",
                    exp->tok_i);
        return;
    }

    cmd_add_goto(ctx, loop->break_label_i, exp->tok_i);
}

static void gen_return(Ctx *ctx, int exp_i) {
    defexp;
    assert(exp->kind == exp_return);

    gen_exp(ctx, exp->exp_l);
    cmd_add(ctx, cmd_return, exp->tok_i);
}

static void gen_lval(Ctx *ctx, int exp_i) {
    defexp;

    const bool lval = true;

    switch (exp->kind) {
    case exp_ident:
        return gen_ident(ctx, exp_i, lval);
    case exp_op: {
        if ((OpKind)exp->int_value != op_index) {
            break;
        }
        return gen_op(ctx, exp_i, lval);
    }
    default:
        break;
    }

    cmd_add_err(ctx, "左辺値が必要です。", exp->tok_i);
}

static void gen_exp(Ctx *ctx, int exp_i) {
    defexp;

    const bool lval = false;

    switch (exp->kind) {
    case exp_int: {
        cmd_add_int(ctx, cmd_push_int, exp->int_value, exp->tok_i);
        return;
    }
    case exp_str: {
        cmd_add_str(ctx, cmd_push_str, exp->str_value, exp->tok_i);
        return;
    }
    case exp_ident:
        gen_ident(ctx, exp_i, lval);
        return;
    case exp_array:
        gen_array(ctx, exp_i);
        return;
    case exp_call:
        gen_call(ctx, exp_i);
        return;
    case exp_op:
        gen_op(ctx, exp_i, lval);
        return;
    case exp_fun:
        gen_fun(ctx, exp_i);
        return;
    case exp_let:
        gen_let(ctx, exp_i);
        return;
    case exp_if:
        gen_if(ctx, exp_i);
        return;
    case exp_while:
        gen_while(ctx, exp_i);
        return;
    case exp_break:
        gen_break(ctx, exp_i);
        return;
    case exp_return:
        gen_return(ctx, exp_i);
        return;
    case exp_err: {
        cmd_add_err(ctx, exp->str_value, exp->tok_i);
        return;
    }
    default:
        failwith("Unknown ExpKind");
    }
}

static void gen_resolve_labels(Ctx *ctx) {
    for (int i = 0; i < ctx->cmds.len; i++) {
        const int cmd_i = i;
        Cmd *cmd = &ctx->cmds.data[cmd_i];

        if (cmd->kind == cmd_label) {
            label_set(ctx, cmd->x, cmd_i);
        }
    }

    for (int i = 0; i < ctx->labels.len; i++) {
        if (ctx->labels.data[i].cmd_i < 0) {
            failwith("Unresolved label");
        }
    }
}

static void gen(Ctx *ctx) {
    ctx->scope_i_global = scope_add_global(ctx, ctx->tok_i_eof);
    ctx->scope_i_current = ctx->scope_i_global;

    int main_label_i = label_add(ctx);
    cmd_add_label(ctx, main_label_i, ctx->tok_i_eof);

    ctx->cmd_i_entry = ctx->cmds.len;
    gen_exp(ctx, ctx->exp_i_root);

    ctx->cmd_i_exit = ctx->cmds.len;
    cmd_add(ctx, cmd_exit, ctx->tok_i_eof);

    ctx->fun_i_main = fun_add_closure(ctx, ctx->scope_i_global, main_label_i);

    gen_resolve_labels(ctx);
}

// ###############################################
// 評価
// ###############################################

static void eval_abort(Ctx *ctx, const char *message, int tok_i);

static int eval_current_tok_i(Ctx *ctx);

// -----------------------------------------------
// 参照セルリスト
// -----------------------------------------------

static void cell_initialize(Ctx *ctx) {
    mem_reserve((void **)&ctx->cells.data, 0, sizeof(Cell),
                &ctx->cells.capacity, cell_len_min);
    ctx->cells.len = ctx->cells.capacity;

    ctx->stack_end = 0;
    ctx->heap_end = stack_len_min;
    ctx->gc_threshold = heap_len_min / 2;
}

static Cell *cell_get(Ctx *ctx, int cell_i) {
    assert(0 <= cell_i && cell_i < ctx->cells.len);
    return &ctx->cells.data[cell_i];
}

// -----------------------------------------------
// 参照セルリスト: スタック領域
// -----------------------------------------------

static void stack_push(Ctx *ctx, Cell cell) {
    if (ctx->stack_end >= stack_len_min) {
        eval_abort(ctx, "STACK OVERFLOW", eval_current_tok_i(ctx));
        return;
    }
    ctx->cells.data[ctx->stack_end++] = cell;
}

static Cell stack_pop(Ctx *ctx) {
    assert(ctx->stack_end >= 1);
    ctx->stack_end--;

    return ctx->cells.data[ctx->stack_end];
}

static const Cell s_cell_null = (Cell){.ty = ty_int, .val = 0};

// -----------------------------------------------
// 参照セルリスト: ヒープ領域
// -----------------------------------------------

typedef struct CellIndexPair {
    int cell_l, cell_r;
} CellIndexPair;

static CellIndexPair heap_alloc(Ctx *ctx, int count) {
    if (ctx->heap_end + count >= ctx->cells.len) {
        // FIXME: ヒープを自動で拡張する。
        eval_abort(ctx, "OUT OF MEMORY", eval_current_tok_i(ctx));
        return (CellIndexPair){
            .cell_l = stack_len_min,
            .cell_r = stack_len_min + count,
        };
    }

    int cell_l = ctx->heap_end;
    int cell_r = ctx->heap_end + count;
    ctx->heap_end += count;

    if (ctx->cells.len - ctx->heap_end <= ctx->gc_threshold) {
        ctx->does_gc = true;
    }
    return (CellIndexPair){
        .cell_l = cell_l,
        .cell_r = cell_r,
    };
}

// -----------------------------------------------
// フレームスタック
// -----------------------------------------------

static void frame_push(Ctx *ctx, int cmd_i, int env_i, int tok_i) {
    vec_grow((void **)&ctx->frames.data, ctx->frames.len, &ctx->frames.capacity,
             sizeof(Frame), 1);

    int frame_i = ctx->frames.len++;
    ctx->frames.data[frame_i] = (Frame){
        .cmd_i = cmd_i,
        .env_i = env_i,
        .tok_i = tok_i,
    };
}

static Frame *frame_pop(Ctx *ctx) {
    assert(ctx->frames.len >= 1);
    int frame_i = --ctx->frames.len;
    return &ctx->frames.data[frame_i];
}

static Frame *frame_current(Ctx *ctx) {
    assert(ctx->frames.len >= 1);
    return &ctx->frames.data[ctx->frames.len - 1];
}

// -----------------------------------------------
// 文字列リスト
// -----------------------------------------------

static int str_add(Ctx *ctx, const char *str) {
    vec_grow((void **)&ctx->strs.data, ctx->strs.len, &ctx->strs.capacity,
             sizeof(Str), 1);

    int str_i = ctx->strs.len++;
    int size = strlen(str);
    ctx->strs.data[str_i] = (Str){
        .data = str_slice(str, 0, size),
        .len = size,
        .capacity = size,
    };

    return str_i;
}

static Str *str_get(Ctx *ctx, int str_i) {
    assert(0 <= str_i && str_i < ctx->strs.len);
    return &ctx->strs.data[str_i];
}

// -----------------------------------------------
// 配列リスト
// -----------------------------------------------

static int array_add(Ctx *ctx, int len, int capacity) {
    assert(0 <= len && len <= capacity);

    vec_grow((void **)&ctx->arrays.data, ctx->arrays.len, &ctx->arrays.capacity,
             sizeof(Array), 1);

    CellIndexPair range = heap_alloc(ctx, capacity);

    int array_i = ctx->arrays.len++;
    ctx->arrays.data[array_i] = (Array){
        .cell_l = range.cell_l,
        .cell_r = range.cell_r,
        .len = len,
    };

    return array_i;
}

static Array *array_get(Ctx *ctx, int array_i) {
    assert(0 <= array_i && array_i < ctx->arrays.len);
    return &ctx->arrays.data[array_i];
}

static void array_reserve(Ctx *ctx, int array_i, int new_len) {
    Array *array = array_get(ctx, array_i);
    if (new_len <= array->cell_r - array->cell_l) {
        return;
    }

    assert(array->len <= new_len);

    int new_capacity = array->len + new_len;
    CellIndexPair new_range = heap_alloc(ctx, new_capacity);

    memcpy(ctx->cells.data + new_range.cell_l, ctx->cells.data + array->cell_l,
           (new_range.cell_r - new_range.cell_l) * sizeof(Cell));
}

static int array_ref(Ctx *ctx, int array_i, int index) {
    Array *array = array_get(ctx, array_i);

    if (!(0 <= index && index < array->len)) {
        eval_abort(ctx, "配列の要素番号が無効です。", eval_current_tok_i(ctx));
        return array->cell_l;
    }

    return array->cell_l + index;
}

static Cell array_get_item(Ctx *ctx, int array_i, int index) {
    int cell_i = array_ref(ctx, array_i, index);
    return ctx->cells.data[cell_i];
}

static void array_set_item(Ctx *ctx, int array_i, int index, Cell item) {
    int cell_i = array_ref(ctx, array_i, index);
    ctx->cells.data[cell_i] = item;
}

static void array_push(Ctx *ctx, int array_i, Cell item) {
    Array *array = array_get(ctx, array_i);
    int index = array->len;
    array_reserve(ctx, array_i, index + 1);

    array->len++;
    array_set_item(ctx, array_i, index, item);
}

static void array_pop(Ctx *ctx, int array_i) {
    Array *array = array_get(ctx, array_i);

    if (array->len > 0) {
        array->len--;
    }
}

// -----------------------------------------------
// 環境リスト
// -----------------------------------------------

static int env_add(Ctx *ctx, int parent_env_i, int fun_i) {
    vec_grow((void **)&ctx->envs.data, ctx->envs.len, &ctx->envs.capacity,
             sizeof(Env), 1);

    Fun *fun = fun_get(ctx, fun_i);
    Scope *scope = scope_get(ctx, fun->scope_i);
    int array_i = array_add(ctx, scope->len, scope->len);

    int env_i = ctx->envs.len++;
    ctx->envs.data[env_i] = (Env){
        .parent = parent_env_i,
        .scope_i = fun->scope_i,
        .array_i = array_i,
    };
    return env_i;
}

static Env *env_get(Ctx *ctx, int env_i) {
    assert(0 <= env_i && env_i < ctx->envs.len);
    return &ctx->envs.data[env_i];
}

static int env_find(Ctx *ctx, int source_env_i, int index, int scope_i) {
    int env_i = source_env_i;
    while (env_get(ctx, env_i)->scope_i != scope_i) {
        assert(env_i != env_get(ctx, env_i)->parent);
        assert(env_get(ctx, env_i)->parent >= 0);
        env_i = env_get(ctx, env_i)->parent;
    }

    return array_ref(ctx, env_get(ctx, env_i)->array_i, index);
}

// -----------------------------------------------
// クロージャリスト
// -----------------------------------------------

static int closure_add(Ctx *ctx, int fun_i, int env_i) {
    vec_grow((void **)&ctx->closures.data, ctx->closures.len,
             &ctx->closures.capacity, sizeof(Closure), 1);

    int closure_i = ctx->closures.len++;
    ctx->closures.data[closure_i] = (Closure){
        .fun_i = fun_i,
        .env_i = env_i,
    };
    return closure_i;
}

static Closure *closure_get(Ctx *ctx, int closure_i) {
    assert(0 <= closure_i && closure_i < ctx->closures.len);
    return &ctx->closures.data[closure_i];
}

// -----------------------------------------------
// 外部関数フレーム
// -----------------------------------------------

static void extern_frame_activate(Ctx *ctx, int arg_array_i,
                                  int result_cell_i) {
    assert(!ctx->extern_calling);

    ctx->extern_calling = true;
    ctx->extern_frame = (ExternFrame){
        .err = false,
        .arg_array_i = arg_array_i,
        .result_cell_i = result_cell_i,
    };
}

static void extern_frame_deactivate(Ctx *ctx) {
    ctx->extern_calling = false;
    ctx->extern_frame = (ExternFrame){};
}

static void extern_frame_resolve(Ctx *ctx, Cell result) {
    assert(ctx->extern_calling);
    *cell_get(ctx, ctx->extern_frame.result_cell_i) = result;
}

static void extern_frame_reject(Ctx *ctx, const char *message) {
    assert(ctx->extern_calling);
    ctx->extern_frame.err = true;
    ctx->extern_frame.err_message = message;
}

static Array *extern_frame_args(Ctx *ctx) {
    assert(ctx->extern_calling);
    return array_get(ctx, ctx->extern_frame.arg_array_i);
}

// -----------------------------------------------
// 評価
// -----------------------------------------------

#define defcmd Cmd *cmd = cmd_get(ctx, cmd_i)

static Cell cell_from_bool(bool value) {
    return value ? (Cell){.ty = ty_int, .val = 1} : s_cell_null;
}

static void eval_abort(Ctx *ctx, const char *message, int tok_i) {
    Tok *tok = tok_get(ctx, tok_i);
    err_add(ctx, message, tok->src_l, tok->src_r);

    ctx->exit_code = 1;
    ctx->stack_end = 0;
    stack_push(ctx, (Cell){.ty = ty_int, .val = 1});
    ctx->pc = ctx->cmd_i_exit;
    ctx->does_gc = false;
}

static int eval_current_tok_i(Ctx *ctx) {
    if (ctx->pc <= 0) {
        return ctx->tok_i_eof;
    }
    return cmd_get(ctx, ctx->pc - 1)->tok_i;
}

static void eval_err(Ctx *ctx, int cmd_i) {
    defcmd;
    assert(cmd->kind == cmd_err);

    eval_abort(ctx, cmd->str, cmd->tok_i);
}

static void eval_push_int(Ctx *ctx, int cmd_i) {
    defcmd;
    assert(cmd->kind == cmd_push_int);
    stack_push(ctx, (Cell){.ty = ty_int, .val = cmd->x});
}

static void eval_push_str(Ctx *ctx, int cmd_i) {
    defcmd;
    assert(cmd->kind == cmd_push_str);

    int str_i = str_add(ctx, cmd->str);
    stack_push(ctx, (Cell){.ty = ty_str, .val = str_i});
}

static void eval_push_array(Ctx *ctx, int cmd_i) {
    defcmd;
    assert(cmd->kind == cmd_push_array);

    int len = cmd->x;
    int array_i = array_add(ctx, 0, len);
    stack_push(ctx, (Cell){.ty = ty_array, .val = array_i});
}

static void eval_push_closure(Ctx *ctx, int cmd_i) {
    defcmd;
    assert(cmd->kind == cmd_push_closure);

    int fun_i = cmd->x;
    int env_i = frame_current(ctx)->env_i;
    int closure_i = closure_add(ctx, fun_i, env_i);
    stack_push(ctx, (Cell){.ty = ty_closure, .val = closure_i});
}

static void eval_push_extern(Ctx *ctx, int cmd_i) {
    defcmd;
    assert(cmd->kind == cmd_push_extern);

    int extern_fun_i = cmd->x;
    stack_push(ctx, (Cell){.ty = ty_extern, .val = extern_fun_i});
}

static void eval_local_var(Ctx *ctx, int cmd_i) {
    defcmd;
    assert(cmd->kind == cmd_local_var);

    int index = cmd->x;
    int env_i = frame_current(ctx)->env_i;
    int cell_i = env_find(ctx, env_i, index, cmd->scope_i);
    stack_push(ctx, (Cell){.ty = ty_cell, .val = cell_i});
}

static void eval_cell_get(Ctx *ctx, int cmd_i) {
    defcmd;
    assert(cmd->kind == cmd_cell_get);

    Cell cell = stack_pop(ctx);
    if (cell.ty != ty_cell) {
        eval_abort(ctx, "左辺値が必要です。", cmd->tok_i);
        return;
    }

    stack_push(ctx, ctx->cells.data[cell.val]);
}

static void eval_cell_set(Ctx *ctx, int cmd_i) {
    defcmd;
    assert(cmd->kind == cmd_cell_set);

    Cell r_cell = stack_pop(ctx);
    Cell l_cell = stack_pop(ctx);

    if (l_cell.ty != ty_cell) {
        eval_abort(ctx, "左辺値が必要です。", cmd->tok_i);
        return;
    }

    ctx->cells.data[l_cell.val] = r_cell;
    stack_push(ctx, r_cell);
}

static void eval_label(Ctx *ctx, int cmd_i) {}

static void eval_jump_unless(Ctx *ctx, int cmd_i) {
    defcmd;
    assert(cmd->kind == cmd_jump_unless);

    int label_i = cmd->x;

    Cell cond = stack_pop(ctx);
    if (cond.ty != ty_int) {
        eval_abort(ctx, "条件は整数でなければいけません。", cmd->tok_i);
        return;
    }

    if (cond.val == 0) {
        ctx->pc = ctx->labels.data[label_i].cmd_i;
        assert(ctx->cmds.data[ctx->pc].kind == cmd_label);
    }
}

static void eval_pop(Ctx *ctx, int cmd_i) {
    defcmd;
    assert(cmd->kind == cmd_pop);

    stack_pop(ctx);
}

static void eval_swap(Ctx *ctx, int cmd_i) {
    defcmd;
    assert(cmd->kind == cmd_swap);

    // HELP: optimize
    Cell first = stack_pop(ctx);
    Cell second = stack_pop(ctx);
    stack_push(ctx, first);
    stack_push(ctx, second);
}

static void eval_dup(Ctx *ctx, int cmd_i) {
    defcmd;
    assert(cmd->kind == cmd_dup);

    // HELP: optimize
    Cell first = stack_pop(ctx);
    stack_push(ctx, first);
    stack_push(ctx, first);
}

static void eval_call(Ctx *ctx, int cmd_i) {
    defcmd;
    assert(cmd->kind == cmd_call);
    int len = cmd->x;

    Cell args[32];
    if (len >= array_len(args)) {
        unimplemented();
    }
    for (int i = len - 1; i >= 0; i--) {
        args[i] = stack_pop(ctx);
    }
    Cell fun = stack_pop(ctx);

    if (fun.ty == ty_closure) {
        int closure_i = fun.val;
        Closure *closure = closure_get(ctx, closure_i);
        assert(fun_get(ctx, closure->fun_i)->kind == fun_kind_closure);

        int body_label_i = fun_get(ctx, closure->fun_i)->label_i;

        // ローカル環境を生成する。
        int env_i = env_add(ctx, closure->env_i, closure->fun_i);

        Env *env = env_get(ctx, env_i);
        for (int i = 0; i < len; i++) {
            array_set_item(ctx, env->array_i, i, args[i]);
        }

        frame_push(ctx, ctx->pc, env_i, cmd->tok_i);
        ctx->pc = label_get(ctx, body_label_i)->cmd_i;
        return;
    }

    if (fun.ty == ty_extern) {
        int extern_fun_i = fun.val;

        int array_i = array_add(ctx, len, len);

        CellIndexPair result_cell_range = heap_alloc(ctx, 1);
        int result_cell_i = result_cell_range.cell_l;

        for (int i = 0; i < len; i++) {
            array_set_item(ctx, array_i, i, args[i]);
        }

        extern_frame_activate(ctx, array_i, result_cell_i);
        extern_fun_get(ctx, extern_fun_i)->fun(ctx, len);

        if (ctx->extern_frame.err) {
            eval_abort(ctx, ctx->extern_frame.err_message, cmd->tok_i);
        } else {
            stack_push(ctx, *cell_get(ctx, result_cell_i));
        }

        extern_frame_deactivate(ctx);
        return;
    }

    eval_abort(ctx, "型エラー", cmd->tok_i);
}

static void eval_return(Ctx *ctx, int cmd_i) {
    defcmd;
    assert(cmd->kind == cmd_return);

    ctx->pc = frame_pop(ctx)->cmd_i;
}

static void eval_op(Ctx *ctx, int cmd_i) {
    defcmd;
    assert(cmd->kind == cmd_op);
    OpKind op = (OpKind)cmd->x;

    assert(op != op_semi);
    assert(op != op_ne);
    assert(op != op_le);
    assert(op != op_gt);
    assert(op != op_ge);

    Cell r_cell = stack_pop(ctx);
    Cell l_cell = stack_pop(ctx);
    int ty = l_cell.ty;
    int val = l_cell.val;

    if (op == op_eq) {
        if (ty != r_cell.ty) {
            stack_push(ctx, s_cell_null);
            return;
        }
        if (ty == ty_int) {
            stack_push(ctx, cell_from_bool(val == r_cell.val));
            return;
        }
        if (ty == ty_str) {
            bool cmp =
                strcmp(str_get(ctx, val)->data, str_get(ctx, r_cell.val)->data);
            stack_push(ctx, cell_from_bool(cmp == 0));
            return;
        }
        eval_abort(ctx, "型エラー", cmd->tok_i);
        return;
    }

    if (op == op_lt) {
        if (ty != r_cell.ty) {
            stack_push(ctx, cell_from_bool(ty < r_cell.ty));
            return;
        }
        if (ty == ty_int) {
            stack_push(ctx, cell_from_bool(val < r_cell.val));
            return;
        }
        if (ty == ty_str) {
            int cmp =
                strcmp(str_get(ctx, val)->data, str_get(ctx, r_cell.val)->data);
            stack_push(ctx, cell_from_bool(cmp < 0));
            return;
        }
        eval_abort(ctx, "型エラー", cmd->tok_i);
        return;
    }

    if (op == op_index) {
        if (ty == ty_str && r_cell.ty == ty_int) {
            int i = r_cell.val;
            const Str *str = str_get(ctx, val);
            char c = 0 <= i && i < str->len ? str->data[i] : '\0';
            stack_push(ctx, (Cell){.ty = ty_int, .val = (int)c});
            return;
        }
        if (ty == ty_array && r_cell.ty == ty_int) {
            Cell item = array_get_item(ctx, val, r_cell.val);
            stack_push(ctx, item);
            return;
        }
        eval_abort(ctx, "型エラー", cmd->tok_i);
        return;
    }
    if (op == op_index_ref) {
        if (ty == ty_array && r_cell.ty == ty_int) {
            int cell_i = array_ref(ctx, val, r_cell.val);
            stack_push(ctx, (Cell){.ty = ty_cell, .val = cell_i});
            return;
        }
        eval_abort(ctx, "型エラー", cmd->tok_i);
    }
    if (op == op_array_push) {
        assert(ty == ty_array);
        array_push(ctx, val, r_cell);
        stack_push(ctx, l_cell);
        return;
    }

    if (ty != r_cell.ty) {
        eval_abort(ctx, "型エラー", cmd->tok_i);
        return;
    }

    if (op == op_add) {
        if (ty == ty_int) {
            stack_push(ctx, (Cell){.ty = ty_int, .val = val + r_cell.val});
            return;
        }
        if (ty == ty_str) {
            StringBuilder *sb = sb_new();
            sb_append(sb, str_get(ctx, val)->data);
            sb_append(sb, str_get(ctx, r_cell.val)->data);
            int str_i = str_add(ctx, sb_to_str(sb));
            stack_push(ctx, (Cell){.ty = ty_str, .val = str_i});
            return;
        }
        eval_abort(ctx, "演算子 + をサポートしていません。", cmd->tok_i);
        return;
    }

    if (op == op_sub) {
        if (ty == ty_int) {
            stack_push(ctx, (Cell){.ty = ty_int, .val = val - r_cell.val});
            return;
        }
        eval_abort(ctx, "型エラー", cmd->tok_i);
        return;
    }
    if (op == op_mul) {
        if (ty == ty_int) {
            stack_push(ctx, (Cell){.ty = ty_int, .val = val * r_cell.val});
            return;
        }
        eval_abort(ctx, "型エラー", cmd->tok_i);
        return;
    }
    if (op == op_div) {
        if (ty == ty_int) {
            stack_push(ctx, (Cell){.ty = ty_int, .val = val / r_cell.val});
            return;
        }
        eval_abort(ctx, "型エラー", cmd->tok_i);
        return;
    }
    if (op == op_mod) {
        if (ty == ty_int) {
            stack_push(ctx, (Cell){.ty = ty_int, .val = val % r_cell.val});
            return;
        }
        eval_abort(ctx, "型エラー", cmd->tok_i);
        return;
    }

    failwith("Unknown OpKind");
}

static void eval_cmds(Ctx *ctx) {
    while (true) {
        int cmd_i = ctx->pc++;
        switch ((CmdKind)cmd_get(ctx, cmd_i)->kind) {
        case cmd_push_int:
            eval_push_int(ctx, cmd_i);
            continue;
        case cmd_push_str:
            eval_push_str(ctx, cmd_i);
            continue;
        case cmd_push_array:
            eval_push_array(ctx, cmd_i);
            continue;
        case cmd_push_closure:
            eval_push_closure(ctx, cmd_i);
            continue;
        case cmd_push_extern:
            eval_push_extern(ctx, cmd_i);
            continue;
        case cmd_local_var:
            eval_local_var(ctx, cmd_i);
            continue;
        case cmd_cell_get:
            eval_cell_get(ctx, cmd_i);
            continue;
        case cmd_cell_set:
            eval_cell_set(ctx, cmd_i);
            continue;
        case cmd_label:
            eval_label(ctx, cmd_i);
            continue;
        case cmd_jump_unless:
            eval_jump_unless(ctx, cmd_i);
            continue;
        case cmd_pop:
            eval_pop(ctx, cmd_i);
            continue;
        case cmd_swap:
            eval_swap(ctx, cmd_i);
            continue;
        case cmd_dup:
            eval_dup(ctx, cmd_i);
            continue;
        case cmd_call:
            eval_call(ctx, cmd_i);
            continue;
        case cmd_return:
            eval_return(ctx, cmd_i);
            continue;
        case cmd_op:
            eval_op(ctx, cmd_i);
            continue;
        case cmd_err:
            eval_err(ctx, cmd_i);
            continue;
        case cmd_exit: {
            Cell cell = stack_pop(ctx);
            if (cell.ty != ty_int) {
                eval_abort(ctx, "終了コードは整数値でなければいけません。",
                           eval_current_tok_i(ctx));
                continue;
            }
            ctx->exit_code = cell.val;
            return;
        }
        default:
            failwith("Unknown CmdKind");
        }
    }
}

static void eval(Ctx *ctx) {
    ctx->pc = ctx->cmd_i_entry;
    ctx->does_gc = false;
    ctx->exit_code = 1;

    cell_initialize(ctx);

    // グローバル環境を生成する。
    int env_i_global = env_add(ctx, -1, ctx->fun_i_main);
    frame_push(ctx, ctx->cmd_i_exit, env_i_global, ctx->tok_i_eof);

    eval_cmds(ctx);
}

// ###############################################
// 組み込み関数
// ###############################################

#define xargs extern_frame_args(ctx)
#define xarg_nth(i) (cell_get(ctx, xargs->cell_l + (i)))
#define xarg_ty(i) (xarg_nth(i)->ty)
#define xarg_val(i) (xarg_nth(i)->val)

static void builtin_array_len(Ctx *ctx, int argc) {
    if (argc != 1 || xarg_ty(0) != ty_array) {
        extern_frame_reject(ctx, "array_len error");
        return;
    }

    int len = array_get(ctx, xarg_val(0))->len;
    extern_frame_resolve(ctx, (Cell){.ty = ty_int, .val = len});
}

static void builtin_array_push(Ctx *ctx, int argc) {
    if (argc != 2 || xarg_ty(0) != ty_array) {
        extern_frame_reject(ctx, "array_push error");
        return;
    }

    array_push(ctx, xarg_val(0), *xarg_nth(1));
}

static void builtin_array_pop(Ctx *ctx, int argc) {
    if (argc != 1 || xarg_ty(0) != ty_array) {
        extern_frame_reject(ctx, "array_pop error");
        return;
    }

    array_pop(ctx, xarg_val(0));
}

static void extern_fun_builtin(Ctx *ctx) {
    extern_fun_add(ctx, "array_len", builtin_array_len);
    extern_fun_add(ctx, "array_push", builtin_array_push);
    extern_fun_add(ctx, "array_pop", builtin_array_pop);
}

// ###############################################
// テスト
// ###############################################

Ctx *ctx_new(const char *src) {
    Ctx *ctx = mem_alloc(1, sizeof(Ctx));

    *ctx = (Ctx){};

    extern_fun_builtin(ctx);
    src_initialize(ctx, src);
    return ctx;
}

void negi_lang_test_util() {
    StringBuilder *sb = sb_new();
    sb_append(sb, "Hello");
    sb_append(sb, ", world!");
    assert(strcmp(sb_to_str(sb), "Hello, world!") == 0);
}

const char *negi_lang_tokenize_dump(const char *src) {
    Ctx *ctx = ctx_new(src);

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
    Ctx *ctx = ctx_new(src);

    tokenize(ctx);
    parse(ctx);

    StringBuilder *sb = sb_new();
    dump_exp(ctx, ctx->exp_i_root, sb);
    return sb_to_str(sb);
}

const char *negi_lang_gen_dump(const char *src) {
    Ctx *ctx = ctx_new(src);

    tokenize(ctx);
    parse(ctx);
    gen(ctx);

    StringBuilder *sb = sb_new();
    for (int i = 0; i < ctx->cmds.len; i++) {
        const Cmd *cmd = &ctx->cmds.data[i];

        const char *text = tok_text(ctx, cmd->tok_i);
        if (strcmp(text, "") != 0) {
            sb_append(sb, str_format("// %s\n", text));
        }

        switch (cmd->kind) {
        case cmd_err:
            sb_append(sb, str_format("  err \"%s\"\n", cmd->str));
            break;
        case cmd_label:
            sb_append(sb, str_format("%d:\n", cmd->x));
            break;
        default:
            if (cmd->str != NULL) {
                sb_append(sb, str_format("  %d \"%s\"\n", cmd->kind, cmd->str));
                break;
            }
            sb_append(sb, str_format("  %d %d\n", cmd->kind, cmd->x));
            break;
        }
    }
    return sb_to_str(sb);
}

void negi_lang_eval_for_testing(const char *src, int *exit_code,
                                const char **output) {
    Ctx *ctx = ctx_new(src);

    tokenize(ctx);
    parse(ctx);
    gen(ctx);
    eval(ctx);

    *exit_code = ctx->exit_code;
    *output = err_summary(ctx);
}
