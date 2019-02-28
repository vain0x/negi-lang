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

struct Err {
    const Str *message;
    int src_l, src_r;
} Err;

struct Errs {
    struct Err *data;
    int len;
    int capacity;
};

// ###############################################
// 字句解析
// ###############################################

struct Tok {
    enum TokKind kind;
    int src_l, src_r;
};

struct Toks {
    struct Tok *data;
    int len;
    int capacity;
};

typedef int TokId;

// ###############################################
// コンテクスト
// ###############################################

struct NegiLangContext {
    Str *src;

    struct Errs errs;
    struct Toks toks;
};

extern void negi_lang_test_util();
extern Str *negi_lang_tokenize_dump(const char *src);

// ###############################################
// デバッグ用
// ###############################################

// -----------------------------------------------
// 致命的なエラー
// -----------------------------------------------

// ネギ言語処理系のバグに起因するエラーを報告して、異常終了する。
// 入力されたプログラムの問題は、これではなく以下にある「エラー」の仕組みを使って報告する。
#define failwith(MSG)                                                          \
    do {                                                                       \
        eprintf("FATAL ERROR: " MSG " \nFILE: " __FILE__ "\nLINE: " __LINE__   \
                "\n");                                                         \
        abort(0);                                                              \
    } while (0)

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
