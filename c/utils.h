#ifndef NEGI_LANG_UTILS_H
#define NEGI_LANG_UTILS_H

#define array_len(X) (sizeof(X) / sizeof(*X))

// ###############################################
// 汎用
// ###############################################

typedef struct Vec {
    void *data;
    int size;
    int capacity;
} Vec;

typedef struct StringBuilder {
    char *data;
    // Excluding the final zero byte.
    int size;
    int capacity;
} StringBuilder;

typedef struct TextPos {
    int y, x;
} TextPos;

typedef struct VecInt {
    int *data;
    int len;
    int capacity;
} VecInt;

// data をサイズ unit の要素の配列へのポインタとみなして、領域を拡張する。
// いまのキャパシティ (最大の要素数) が *capacity で、そのうち count
// 個が使用中であるとする。 これをキャパシティが new_capacity
// 以上になるように必要なら再確保する。縮めることはない。
extern void mem_reserve(void **data, int count, int unit, int *capacity,
                 int new_capacity);
extern void *mem_alloc(int count, int unit);

extern void vec_grow(void **data, int len, int *capacity, int unit, int grow_size);

extern char *string_slice(const char *str, int l, int r);
extern char *string_format(const char *fmt, ...);

extern StringBuilder *sb_new();
extern void sb_reserve(StringBuilder *sb, int new_capacity);
extern void sb_append(StringBuilder *sb, const char *src);
extern void sb_format(StringBuilder *sb, const char *fmt, ...);
extern const char *sb_to_str(const StringBuilder *sb);

extern VecInt *vec_int_new();
extern void vec_int_push(VecInt *vec, int value);

// ###############################################
// デバッグ用
// ###############################################

// -----------------------------------------------
// 致命的なエラー
// -----------------------------------------------

// ネギ言語処理系のバグに起因するエラーを報告して、異常終了する。
// 入力されたプログラムの問題は、これではなく以下にある「エラー」の仕組みを使って報告する。
#define failwith(message) do_failwith(__FILE__, __LINE__, message)

#define unimplemented() failwith("unimplemented")

// -----------------------------------------------
// デバッグ用
// -----------------------------------------------

#ifdef _DEBUG
#define trace(X) do_trace(__FILE__, __LINE__, X)
#define debug(A) //

#else
#define trace(A) //
#define debug(A) //
#endif

extern __attribute__((noreturn)) void do_failwith(const char *file_name, int line,
                                           const char *message);

extern void do_trace(const char *file_name, int line, const char *message);

#endif
