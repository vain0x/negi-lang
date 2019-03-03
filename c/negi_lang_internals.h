#ifndef NEGI_LANG_INTERNALS_H
#define NEGI_LANG_INTERNALS_H

#include <stdbool.h>

typedef struct NegiLangContext Ctx;

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

// ###############################################
// 定数
// ###############################################

// -----------------------------------------------
// トークンの種類
// -----------------------------------------------

typedef enum TokKind {
    // ソースコードに使えないトークン
    tok_err = 1,

    // ソースコードの末尾を表すトークン
    tok_eof,

    // 整数を表すトークン
    tok_int,

    // 文字列を表すトークン
    tok_str,

    // 識別子を表すトークン
    tok_ident,

    // 左丸カッコ
    tok_paren_l,

    // 右丸カッコ
    tok_paren_r,

    // 左角カッコ
    tok_bracket_l,

    // 右角カッコ
    tok_bracket_r,

    // 左波カッコ
    tok_brace_l,

    // 右波カッコ
    tok_brace_r,

    // カンマ
    tok_comma,

    // セミコロン (semicolon)
    tok_semi,

    // 演算子
    tok_op,

    tok_let,

    tok_if,

    tok_else,

    tok_while,

    tok_break,

    tok_fun,

    tok_return,
} TokKind;

// -----------------------------------------------
// 式の種類
// -----------------------------------------------

typedef enum ExpKind {
    exp_err,

    // 整数リテラル
    exp_int,

    // 文字列リテラル
    exp_str,

    // 配列リテラル
    // subexp: 要素
    exp_array,

    // 識別子
    exp_ident,

    // 関数呼び出し
    // exp_l: 関数
    // subexp: 引数
    exp_call,

    // 演算式
    exp_op,

    // let 式
    // int: 定義される識別子のトークン番号
    // exp_r: 初期化式
    exp_let,

    // if 文
    // exp_cond: 条件
    // exp_l, exp_r: then, else
    exp_if,

    // exp_cond: 条件式
    // exp_l: 本体
    exp_while,

    exp_break,

    // ラムダ式
    // subexp: 仮引数リスト
    // exp_l: 本体
    exp_fun,

    exp_return,
} ExpKind;

// -----------------------------------------------
// 無効な式番号
// -----------------------------------------------

enum ExpIndex {
    exp_i_none = 0,
};

// -----------------------------------------------
// 演算子の種類
// -----------------------------------------------

typedef enum OpKind {
    op_err,
    // ; (semicolon)
    op_semi,
    // =
    op_set,
    // +=
    op_set_add,
    // -=
    op_set_sub,
    // *=
    op_set_mul,
    // /=
    op_set_div,
    // %=
    op_set_mod,
    // == (equal)
    op_eq,
    // != (not equal)
    op_ne,
    // < (less than)
    op_lt,
    // <= (less than or equal to)
    op_le,
    // < (greater than)
    op_gt,
    // >= (greater than or equal to)
    op_ge,
    // + (addition)
    op_add,
    // - (subtraction)
    op_sub,
    // * (multiplication)
    op_mul,
    // / (division)
    op_div,
    // % (modulo)
    op_mod,
    // [] (index)
    op_index,

    // 以下の演算子はコンパイラによって生成される。

    // 配列の要素の参照を取得する。
    op_index_ref,

    // 配列の末尾に要素を追加する。
    op_array_push,
} OpKind;

// -----------------------------------------------
// 演算子のレベルの種類
// -----------------------------------------------

typedef enum OpLevel {
    op_level_set,
    // comparison
    op_level_cmp,
    // additive
    op_level_add,
    // multitive
    op_level_mul,
} OpLevel;

// -----------------------------------------------
// スコープ番号
// -----------------------------------------------

enum ScopeIndex {
    scope_i_global = 0,
};

// -----------------------------------------------
// 命令の種類
// -----------------------------------------------

typedef enum CmdKind {
    // エラー
    cmd_err,

    // 終了
    cmd_exit,

    // ラベル
    cmd_label,

    // スタック上の値が false ならジャンプ
    cmd_jump_unless,

    // 整数リテラルをスタックにプッシュ
    cmd_push_int,

    // 文字列リテラルをスタックにプッシュ
    cmd_push_str,

    // 空の配列を生成してプッシュする
    // x: キャパシティ
    cmd_push_array,

    // クロージャを生成してプッシュする
    // x: 関数番号
    cmd_push_closure,

    // 外部関数をプッシュする
    cmd_push_extern,

    // ローカル変数の参照セルをプッシュ
    // x: 何番目の変数か
    // y: 変数が属するスコープ番号
    cmd_local_var,

    // スタックの一番上にある参照セルの値を取得する
    cmd_cell_get,

    // スタックの一番上にある値を、その下にある参照セルに設定する
    cmd_cell_set,

    // スタックの一番上の要素を捨てる
    cmd_pop,

    // スタック上の2つの要素を交換
    cmd_swap,

    // スタックの一番上の要素をもう1つ積む
    cmd_dup,

    // 関数呼び出し
    // x: 引数の個数
    cmd_call,

    // 関数から戻る
    cmd_return,

    // 演算
    cmd_op,
} CmdKind;

// -----------------------------------------------
// 値の型タグ
// -----------------------------------------------

typedef enum TyKind {
    ty_err,

    // 整数。値は整数値そのもの。
    ty_int,

    // 文字列。値は s_strs の要素番号。
    ty_str,

    // 配列。値は s_arrays の要素番号。
    ty_array,

    // クロージャ。値は s_closures の要素番号。
    ty_closure,

    // 外部関数。値は s_extern_funs の要素番号。
    ty_extern,

    // 参照セル。値は s_cells の要素番号。
    ty_cell,
} TyKind;

// -----------------------------------------------
// 関数の種類
// -----------------------------------------------

typedef enum FunKind {
    // クロージャ。fun 式によって生成されるオブジェクト。
    fun_kind_closure,

    // 外部関数。extern 宣言によって生成されるオブジェクト。
    fun_kind_extern,
} FunKind;

// -----------------------------------------------
// メモリ管理
// -----------------------------------------------

enum {
    // 参照セル領域のうちスタックに割り当てられる個数。(1MB)
    stack_len_min = 1024 * 1024 / 4,

    // 参照セル領域のうちヒープに割り当てられる個数の初期値。(1MB)
    heap_len_min = 1024 * 1024 / 4,

    // 参照セル領域の長さの既定値。
    cell_len_min = stack_len_min + heap_len_min,

    s_cell_i_stack_max = stack_len_min,
};

// ###############################################
// エラー
// ###############################################

typedef struct Err {
    const char *message;
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
    const char *str_value;
    int tok_i;
} Exp;

typedef struct Exps {
    Exp *data;
    int len;
    int capacity;
} Exps;

// ###############################################
// コード生成
// ###############################################

// -----------------------------------------------
// ラベルリスト
// -----------------------------------------------

typedef struct Label {
    int cmd_i;
} Label;

typedef struct VecLabel {
    Label *data;
    int len;
    int capacity;
} VecLabel;

// -----------------------------------------------
// スコープリスト
// -----------------------------------------------

typedef struct Scope {
    // 親スコープのスコープ番号 (トップレベルなら -1)
    int parent;

    // 識別子の個数
    int len;

    int tok_i;
} Scope;

typedef struct VecScope {
    Scope *data;
    int len;
    int capacity;
} VecScope;

// -----------------------------------------------
// ローカルリスト
// -----------------------------------------------

// スコープに属する識別子。
typedef struct Local {
    const char *ident;

    // 識別子が属するスコープ。
    int scope_i;

    // スコープ内の何番目の識別子か。
    int index;

    int tok_i;
} Local;

typedef struct VecLocal {
    Local *data;
    int len;
    int capacity;
} VecLocal;

// -----------------------------------------------
// 関数リスト
// -----------------------------------------------

typedef struct Fun {
    FunKind kind;

    const char *name;

    // 関数の本体に対応するスコープ番号 (クロージャのみ)
    int scope_i;

    // 関数の本体を指すラベル番号 (クロージャのみ)
    int label_i;
} Fun;

typedef struct VecFun {
    Fun *data;
    int len;
    int capacity;
} VecFun;

// -----------------------------------------------
// 外部関数リスト
// -----------------------------------------------

typedef void (*extern_fun_t)(Ctx *ctx, int argc);

typedef struct ExternFun {
    const char *name;
    extern_fun_t fun;
} ExternFun;

typedef struct VecExternFun {
    ExternFun *data;
    int len;
    int capacity;
} VecExternFun;

// -----------------------------------------------
// 外部関数フレーム
// -----------------------------------------------

typedef struct ExternFrame {
    bool err;
    const char *err_message;

    int arg_array_i;
    int result_cell_i;
} ExternFrame;

// -----------------------------------------------
// ループスタック
// -----------------------------------------------

typedef struct Loop {
    int break_label_i;
    int tok_i;
} Loop;

typedef struct VecLoop {
    Loop *data;
    int len;
    int capacity;
} VecLoop;

// -----------------------------------------------
// 命令リスト
// -----------------------------------------------

typedef struct Cmd {
    CmdKind kind;
    int x;
    const char *str;
    int scope_i;
    int tok_i;
} Cmd;

typedef struct VecCmd {
    Cmd *data;
    int len;
    int capacity;
} VecCmd;

// ###############################################
// 評価
// ###############################################

// -----------------------------------------------
// 参照セル
// -----------------------------------------------

typedef struct Cell {
    int ty, val;
} Cell;

typedef struct VecCell {
    Cell *data;
    int len, capacity;
} VecCell;

// -----------------------------------------------
// フレーム
// -----------------------------------------------

// クロージャの呼び出しのメタデータを記録する。
typedef struct Frame {
    // return した直後に実行するコマンド番号
    int cmd_i;

    // 実行中の環境番号
    int env_i;

    int tok_i;
} Frame;

typedef struct VecFrame {
    Frame *data;
    int len, capacity;
} VecFrame;

// -----------------------------------------------
// 文字列
// -----------------------------------------------

typedef struct Str {
    char *data;
    int len, capacity;
} Str;

typedef struct VecStr {
    Str *data;
    int len, capacity;
} VecStr;

// -----------------------------------------------
// 配列
// -----------------------------------------------

typedef struct Array {
    // 配列の要素が占める参照セルリストの範囲
    int cell_l, cell_r;

    // 配列の長さ
    int len;
} Array;

typedef struct VecArray {
    Array *data;
    int len, capacity;
} VecArray;

// -----------------------------------------------
// 環境
// -----------------------------------------------

typedef struct Env {
    // 親となる環境番号 (なければ -1)
    int parent;

    int scope_i;

    // 引数やローカル変数を格納する配列番号
    int array_i;
} Env;

typedef struct VecEnv {
    Env *data;
    int len, capacity;
} VecEnv;

// -----------------------------------------------
// クロージャ
// -----------------------------------------------

typedef struct Closure {
    // クロージャに対応する関数番号
    int fun_i;
    // クロージャが生成された環境番号
    int env_i;
} Closure;

typedef struct VecClosure {
    Closure *data;
    int len, capacity;
} VecClosure;

// ###############################################
// コンテクスト
// ###############################################

struct NegiLangContext {
    // ソースコード。
    const char *src;
    int src_len;

    Errs errs;

    Toks toks;
    int tok_i_root;
    int tok_i_eof;

    SubExps subexps;
    Exps exps;
    int exp_i_root;

    VecLabel labels;
    VecScope scopes;
    int scope_i_global;
    int scope_i_current;
    VecLocal locals;
    VecFun funs;
    int fun_i_main;
    VecExternFun extern_funs;
    VecLoop loops;
    VecCmd cmds;
    int cmd_i_entry;
    int cmd_i_exit;

    VecCell cells;
    // 参照セルリストのスタック領域の末尾を指す。
    int stack_end;
    // 参照セルリストのヒープ領域の末尾を指す。
    int heap_end;
    // ガベージコレクションを実行する。
    int gc_threshold;
    // フレームのスタック。
    VecFrame frames;
    VecStr strs;
    VecArray arrays;
    VecEnv envs;
    VecClosure closures;

    bool extern_calling;
    ExternFrame extern_frame;

    // プログラムカウンタ。次に実行するコマンド番号。
    int pc;
    // ガベージコレクションを実行するか。
    bool does_gc;
    int exit_code;
};

extern void negi_lang_test_util();
extern const char *negi_lang_tokenize_dump(const char *src);
extern const char *negi_lang_parse_dump(const char *src);
extern const char *negi_lang_gen_dump(const char *src);
extern void negi_lang_eval_for_testing(const char *src, int * exit_code, const char **output);

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
#define d_trace(X) _trace(__FILE__, __LINE__, X)
#define debug(A) //

extern void _trace(const char *file_name, int line, const char *message);

#else
#define d_trace(A) //
#define debug(A)   //
#endif

#endif
