#include "utils.h"
#include <assert.h>
#include <inttypes.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// ###############################################
// 汎用: デバッグ用
// ###############################################

__attribute__((noreturn)) void
do_failwith(const char *file_name, int line, const char *message) {
    fprintf(stderr, "FATAL ERROR at %s:%d\n%s\n", file_name, line, message);
    abort();
}

void do_trace(const char *file_name, int line, const char *message) {
    fprintf(stderr, "[%s:%04d] %s\n", file_name, line, message);
}

// ###############################################
// 汎用: メモリ
// ###############################################

void *mem_alloc(int count, int unit) {
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
void mem_reserve(void **data, int count, int unit, int *capacity,
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

void vec_grow(void **data, int len, int *capacity, int unit,
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

char *string_slice(const char *str, int l, int r) {
    assert(str != NULL && 0 <= l && l <= r);

    char *slice = (char *)mem_alloc(r - l + 1, sizeof(char));

    strncpy(slice, str + l, r - l);
    slice[r - l] = '\0';
    return slice;
}

char *string_format(const char *fmt, ...) {
    char buffer[4096];

    va_list ap;
    va_start(ap, fmt);
    int size = vsnprintf(buffer, sizeof(buffer), fmt, ap);
    va_end(ap);

    if (size < 0) {
        failwith("FATAL ERROR string_format");
    }

    return string_slice(buffer, 0, size);
}

// ###############################################
// 汎用: 文字列ビルダー
// ###############################################

void sb_reserve(StringBuilder *sb, int new_capacity) {
    assert(sb != NULL && new_capacity >= 0);

    if (sb->capacity >= new_capacity) {
        return;
    }

    char *new_data = (char *)mem_alloc(new_capacity + 1, sizeof(char));
    strcpy(new_data, sb->data);

    sb->data = new_data;
    sb->capacity = new_capacity;
}

void sb_append(StringBuilder *sb, const char *src) {
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

void sb_format(StringBuilder *sb, const char *fmt, ...) {
    char buffer[4096];

    va_list ap;
    va_start(ap, fmt);
    int size = vsnprintf(buffer, sizeof(buffer), fmt, ap);
    va_end(ap);

    if (size < 0) {
        failwith("FATAL ERROR sb_format");
    }

    sb_append(sb, buffer);
}

StringBuilder *sb_new() {
    StringBuilder *sb = mem_alloc(1, sizeof(StringBuilder));
    *sb = (StringBuilder){
        .data = "",
        .size = 0,
        .capacity = 0,
    };
    return sb;
}

const char *sb_to_str(const StringBuilder *sb) { return sb->data; }

// ###############################################
// 汎用: 整数のベクタ
// ###############################################

VecInt *vec_int_new() {
    VecInt *vec = mem_alloc(1, sizeof(VecInt));
    *vec = (VecInt){};
    return vec;
}

void vec_int_push(VecInt *vec, int value) {
    vec_grow((void **)&vec->data, vec->len, &vec->capacity, sizeof(int), 1);
    assert(vec->len + 1 <= vec->capacity);

    vec->data[vec->len++] = value;
}
