#ifndef NEGI_LANG_INTERNALS_H
#define NEGI_LANG_INTERNALS_H

typedef struct NegiLangContext Ctx;

// ###############################################
// 汎用
// ###############################################

typedef struct Vec {
    void *data;
    int size;
    int capacity;
} Vec;

typedef struct Str {
    char *data;
    // Excluding the final zero byte.
    int size;
    int capacity;
} Str;

typedef struct TextPos {
    int y, x;
} TextPos;

// ###############################################
// エラー
// ###############################################

typedef struct Err {
    const Str *message;
    int src_l, src_r;
} Err;

typedef struct Errs {
    Err *data;
    int len;
    int capacity;
} Errs;

// ###############################################
// 字句解析
// ###############################################

typedef struct Tok {
    enum TokKind kind;
    int src_l, src_r;
} Tok;

typedef struct Toks {
    Tok *data;
    int len;
    int capacity;
} Toks;

typedef int TokId;

// ###############################################
// 構文解析
// ###############################################

// -----------------------------------------------
// 部分式リスト
// -----------------------------------------------

typedef struct SubExp {
    int exp_i;
} SubExp;

typedef struct SubExps {
    SubExp *data;
    int len;
    int capacity;
} SubExps;

// -----------------------------------------------
// 式リスト
// -----------------------------------------------

// 式 (抽象構文木のノード)
typedef struct Exp {
    enum ExpKind kind;

    int exp_cond, exp_l, exp_r;
    int subexp_l, subexp_r;
    int int_value;
    Str *str_value;
    int tok_i;
} Exp;

typedef struct Exps {
    Exp *data;
    int len;
    int capacity;
} Exps;

// ###############################################
// コンテクスト
// ###############################################

struct NegiLangContext {
    Str *src;

    Errs errs;
    Toks toks;
    SubExps subexps;
    Exps exps;

    int exp_i_root;
};

extern void negi_lang_test_util();
extern Str *negi_lang_tokenize_dump(const char *src);
extern Str *negi_lang_parse_dump(const char *src);

// ###############################################
// デバッグ用
// ###############################################

// -----------------------------------------------
// 致命的なエラー
// -----------------------------------------------

// ネギ言語処理系のバグに起因するエラーを報告して、異常終了する。
// 入力されたプログラムの問題は、これではなく以下にある「エラー」の仕組みを使って報告する。
#define failwith(message) do_failwith(__FILE__, __LINE__, message)

// -----------------------------------------------
// デバッグ用
// -----------------------------------------------

#ifdef _DEBUG
#define d_trace(X) _trace(__FILE__, __LINE__, X)
#define debug(A) //

extern void _trace(const char *file_name, int line, const char *message);

#else
#define d_trace(A) //
#define debug(A)   //
#endif

#endif
