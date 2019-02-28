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

static void str_reserve(Str *str, int new_capacity) {
    assert(str != NULL && new_capacity >= 0);

    if (str->capacity >= new_capacity) {
        return;
    }

    char *new_data = (char *)mem_alloc(new_capacity + 1, sizeof(char));
    strcpy(new_data, str->data);

    str->data = new_data;
    str->capacity = new_capacity;
}

static void str_append_raw(Str *str, const char *src) {
    int src_size = strlen(src);

    int capacity = str->size + src_size;
    if (capacity > str->capacity) {
        capacity += str->capacity;
        str_reserve(str, capacity);
    }

    assert(str->data != NULL && str->capacity >= capacity);

    strcpy(str->data + str->size, src);
    str->size += src_size;
}

static void str_append(Str *str, const Str *src) {
    // HELP: We could optimize this.
    str_append_raw(str, src->data);
}

static Str *str_from_raw(const char *src) {
    assert(src != NULL);

    int src_len = strlen(src);

    Str *str = mem_alloc(1, sizeof(Str));
    *str = (Str){
        .data = (char *)mem_alloc(src_len + 1, sizeof(char)),
        .size = src_len,
        .capacity = src_len,
    };
    strcpy(str->data, src);
    return str;
}

static Str *str_slice(const Str *str, int l, int r) {
    assert(str != NULL && 0 <= l && l <= r && r <= str->size);

    Str *slice = str_from_raw("");
    str_reserve(slice, r - l);

    assert(slice->capacity >= r - l);
    strncpy(slice->data, str->data + l, r - l);
    slice->data[r - l] = '\0';
    return slice;
}

static Str *str_format(const char *fmt, ...) {
    char buffer[4096];

    va_list ap;
    va_start(ap, fmt);
    int size = vsnprintf(buffer, sizeof(buffer), fmt, ap);
    va_end(ap);

    if (size < 0) {
        fprintf(stderr, "FATAL ERROR str_format\n");
        abort();
    }

    return str_from_raw(buffer);
}

// ###############################################
// ソースコード
// ###############################################

static void src_initialize(Ctx *ctx, const char *src) {
    ctx->src = str_from_raw(src);
}

static Str *src_slice(Ctx *ctx, int l, int r) {
    assert(0 <= l && l <= r && r <= ctx->src->size);
    return str_slice(ctx->src, l, r);
}

// ###############################################
// エラー
// ###############################################

// 入力されたプログラムの問題は実行時エラーとして報告する。(failwith
// は使わない。) エラーの報告には、そのエラーが発生した箇所
// (ソースコードのどのあたりか) を含める。

static void err_add(Ctx *ctx, const Str *message, int src_l, int src_r) {
    if (ctx->errs.capacity == ctx->errs.len) {
        mem_reserve((void **)&ctx->errs.data, ctx->errs.len, sizeof(struct Err),
                    &ctx->errs.capacity, ctx->errs.capacity * 2);
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
    int y = 0;
    int x = 0;

    for (int i = 0; i < src_i; i++) {
        if (ctx->src->data[i] == '\n') {
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
static Str *err_summary(Ctx *ctx) {
    Str *summary = str_from_raw("");

    for (int err_i = 0; err_i < ctx->errs.len; err_i++) {
        struct Err *err = &ctx->errs.data[err_i];

        struct TextPos pos_l = find_pos(ctx, err->src_l);
        struct TextPos pos_r = find_pos(ctx, err->src_r);

        Str *text = src_slice(ctx, err->src_l, err->src_r);

        str_append(summary, str_format("%d:%d..%d%d", 1 + pos_l.y, 1 + pos_l.x,
                                       1 + pos_r.y, 1 + pos_l.x));
        str_append_raw(summary, " near '");
        str_append(summary, text);
        str_append_raw(summary, "'\n  ");
        str_append(summary, err->message);
        str_append_raw(summary, "\n");
    }

    return summary;
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

static Str *tok_text(Ctx *ctx, int tok_i) {
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

    while (r < ctx->src->size) {
        // 1回のループで、位置 l から始まる1つのトークンを切り出す。
        // l の次の文字 c からトークンの種類を特定して、
        // そのトークンが広がる範囲の右端まで r を進める。
        // 最終的に、切り出されるトークンは位置 l から r までになるようにする。

        int l = r;

        // 次の文字を変数に入れておく。(先読み)
        char c = ctx->src->data[r];

        // 空白と改行は無視する。
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
            r++;
            continue;
        }

        if (is_digit(c)) {
            while (r < ctx->src->size && is_digit(ctx->src->data[r])) {
                r++;
            }

            tok_add(ctx, tok_int, l, r);
            continue;
        }

        if (c == '"') {
            r++;

            while (r < ctx->src->size) {
                char c = ctx->src->data[r];

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
            while (r < ctx->src->size && is_ident_char(ctx->src->data[r])) {
                r++;
            }

            Str *text = src_slice(ctx, l, r);
            enum TokKind kind = tok_text_to_kind(text->data);
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
            while (r < ctx->src->size && is_op_char(ctx->src->data[r])) {
                r++;
            }
            tok_add(ctx, tok_op, l, r);
            continue;
        }

        // このとき、文字 c
        // はトークンとして不正なもの。エラーを表すトークンとして追加する。
        d_trace(str_format("tok_err char = %c", c)->data);
        r++;
        tok_add(ctx, tok_err, l, r);
    }

    assert(r == ctx->src->size);
    tok_add(ctx, tok_eof, r, r);
    return;
}

// ###############################################
// 実行
// ###############################################

void negi_lang_test_util() {
    Str *str = str_from_raw("Hello");
    str_append_raw(str, ", world!");
    assert(strcmp(str->data, "Hello, world!") == 0);
}

Str *negi_lang_tokenize_dump(const char *src) {
    Ctx *ctx = mem_alloc(1, sizeof(Ctx));

    *ctx = (Ctx){};

    src_initialize(ctx, src);
    tokenize(ctx);

    Str *str = str_from_raw("");
    for (int i = 0; i < ctx->toks.len; i++) {
        str_append(str, tok_text(ctx, i));
        str_append_raw(str, ",");
    }

    return str;
}

void _trace(const char *file_name, int line, const char *message) {
    fprintf(stderr, "[%s:%04d] %s\n", file_name, line, message);
}
