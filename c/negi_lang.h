#ifndef NEIG_LANG_H
#define NEGI_LANG_H

// ネギ言語処理系 ヘッダー

struct NegiLangContext;

// ###############################################
// 汎用
// ###############################################

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

#endif
