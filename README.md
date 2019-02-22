# ネギ言語

ネギ言語とは、HSP3 でシンプルなスクリプト言語の処理系を書くプロジェクト。

## 機能

- 整数の四則演算と比較
- 文字列の加算と比較
- 計算結果として終了コード (整数) を返す

## 開発環境

ソースコードを utf-8 で保存しているため、標準のスクリプトエディタで表示するとコメントが文字化けしてしまうので注意。

[テキストエディタ Atom](https://atom.io/) をインストールして、以下のパッケージを入れる。

- [editorconfig](https://atom.io/packages/editorconfig)
- [language-hsp3](https://github.com/honobonosun/language-hsp3)
    - リンク先に書かれているように、hspc をインストールする。
    - パッケージの設定から hspc の絶対パスを設定する。
    - コンパイラ引数に `-a` を指定する。例: `-Crdwa, %FILEPATH%`
- [linter-hsp3](https://github.com/honobonosun/linter-hsp3)

## 開発メモ

### 開発: テスト

`negi_lang_tests.hsp` を実行して `結果 成功` が出たらOK。

### 開発: ステージ

ソースコードを処理して実際に計算するまで、以下の4つの工程に分けている。

- 字句解析
    - ソースコードをトークン単位に分割して、トークンのリストを作る。
- 構文解析
    - 再帰下降パーサーによって、トークンのリストを抽象構文木にする。
- コード生成
    - 抽象構文木を辿って、中間言語の命令リストを生成する。
    - 中間言語はよくあるスタックマシンのやつ。
    - この工程のおかげで、評価が1本のループを回すだけでよくなり、評価を途中で終了させるのが簡単になる。
        - 構文木を再帰的に辿るタイプのインタプリタは、エラーが起こったとき、実行を停止するために例外などの大域脱出を使うが、 HSP には大域脱出がないので、それはできない。
- 評価
    - 中間言語の命令リストを順繰りに実行してスタック操作を行うことにより、具体的に計算する。
    - 途中でエラーがある箇所に遭遇したら実行時エラーを起こして評価を終了する。
    - 最終的に終了コード (整数) を返す。(0 なら成功、0 以外なら失敗。)

### 開発: コミットメッセージ

Git のコミットメッセージには以下のプレフィックスをつけるのを推奨している。

- `feat:`
    - 機能を追加・変更・削除するとき
- `refactor:`
    - 機能の変更ではなく、品質の改善を目的として変更するとき
- `fix:`
    - 何らかの誤りを修正するとき
- `docs:`
    - コメントやドキュメントを追加・変更・削除するとき
- `chore:`
    - 開発環境にかかわる変更をするとき

### 開発: 略語リスト

- err: error
- tok: token
- exp: expression (式)
- op: operator (演算子)
- ty: type (型)
- cmd: command (命令)
- eval: evaluation (評価)
- eof: end of file (入力の終わり)
- ident: identifier (識別子)
- bin: binary operator (2項演算子)

### 開発: 構文メモ

- 任意の構文要素を式と呼ぶことにする。
- 丸カッコの中にかける種類の式を term と呼ぶことにする。
    - `x + y` や `if (p) { x } else { y }` は term である。
    - `let x = y` は term でない。
- 単一のトークンや一対のカッコからなる種類の term を atom と呼ぶことにする。
    - `42` や `(x + y)` は atom である。
    - `x + y` は atom でない。
- 丸カッコの中にかけない種類の式を stmt と呼ぶことにする。
    - `let x = y` は stmt である。

```

int = ( '+' / '-' ) [0-9]+

str = ".."

ident = [a-zA-Z_] [a-zA-Z0-9_]*

block = '{' exp '}'

if = 'if' '(' term ')' block ( 'else' ( if / block ) )?

while = 'while' '(' term ')' block

atom
    = '(' term ')'
    / '[' list ']'
    / int / str / ident

suffix = atom ( '[' term ']' )*

bin_mul = suffix ( ( '*' / '/' / '%' ) suffix )*

bin_add = bin_mul ( ( '+' / '-' ) bin_mul )*

bin_cmp = bin_add ( ( '==' / '!=' / '<' / '<=' / '>' / '>=' ) bin_add )*

bin_set = bin_cmp ( '=' term )?

term =
    if
    / bin_set

list = ( term ( ',' term )* )?

let = 'let' ident '=' term

stmt =
    let
    / while / 'break'
    / term

exp = ( ';'* stmt )* ';'*

program = exp

```
