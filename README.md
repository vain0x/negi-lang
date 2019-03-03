# ネギ言語

ネギ言語とは、HSP3 でシンプルなスクリプト言語の処理系を書くプロジェクト。

## 機能

- 整数の四則演算と比較
- 文字列の加算と比較
- if 文と while 文
- ローカル変数
- クロージャ (ラムダ式)
- 外部関数 (ネギ言語からHSPスクリプトの呼び出し)

## C言語版

- `c` ディレクトリに、C言語に移植したバージョンがある。

## 開発環境

ソースコードを UTF-8 エンコーディングで保存しているため、標準のスクリプトエディタからは実行できないので注意。

`make_sjis_version.hsp` を実行すると `sjis` ディレクトリに SHIFT_JIS エンコーディングに変換されたスクリプトファイルが生成される。これはスクリプトエディタで開いたり実行したりできる。

### 開発環境: 構築

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
- 評価
    - 中間言語の命令リストを順繰りに実行してスタック操作を行うことにより、実際の計算処理を行う。
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
- stmt: statement (文)
- op: operator (演算子)
- ty: type (型)
- cmd: command (命令)
- eval: evaluation (評価)
- eof: end of file (入力の終わり)
- ident: identifier (識別子)
- bin: binary operator (2項演算子)

### 開発: 構文メモ

- 任意の構文要素を式と呼ぶことにする。
- 単一のトークンや一対のカッコからなる種類の式を atom と呼ぶことにする。
    - `42` や `(x + y)` は atom である。
    - `x + y` は atom でない。
- 丸カッコの中に書ける種類の式を term と呼ぶことにする。
    - `x + y` や `p ? x : y` は term である。
    - `let x = y` は term でない。
- 丸カッコの中に書けない種類の式を stmt と呼ぶことにする。
    - `let x = y` は stmt である。

```

int = ( '+' / '-' ) [0-9]+

str = ".."

ident = [a-zA-Z_] [a-zA-Z0-9_]*

atom
    = '(' term ')'
    / '[' list ']'
    / int / str / ident

suffix = atom ( '[' term ']' )*

prefix = '-'? suffix

bin_mul = prefix ( ( '*' / '/' / '%' ) prefix )*

bin_add = bin_mul ( ( '+' / '-' ) bin_mul )*

bin_cmp = bin_add ( ( '==' / '!=' / '<' / '<=' / '>' / '>=' ) bin_add )*

bin_set = bin_cmp ( ( '=' / '+=' / '-=' / '*=' / '/=' / '%=' ) term )?

cond = bin_set ( '?' term ':' term )?

fun = 'fun' '(' ( ident ( ',' ident )* )? ')' ( block / term )

term = fun / cond

list = ( term ( ',' term )* )?

block = '{' exp '}'

let = 'let' ident '=' term

if = 'if' '(' term ')' block ( 'else' ( if / block ) )?

while = 'while' '(' term ')' block

return = 'return' term?

stmt =
    let
    / if / while / 'break'
    / return
    / term

exp = ( ';'* stmt )* ';'*

program = exp

```

### 開発: ガベージコレクション

不要になった配列などのオブジェクトを自動で削除する機能をガベージコレクション (GC) という。

#### GC: トリガー

この GC は HSP 側での処理の途中に実行すると壊れる。実行中のコマンドがないタイミングで実行しなければいけない。

ヒープ領域を割り当てるとき、ヒープ領域内の残りのセル数が一定以下なら、次のコマンドの実行前 GC を行うようにフラグを立てる。

#### GC: マーク

GC はマーク、ムーブ、リライトの3段階で行う。

マーク処理では、生存しているオブジェクトに印をつける。以下のオブジェクトは生存している:

- スタック上のオブジェクト
- 呼び出し中のフレームの環境
- 生存しているオブジェクトに参照されているオブジェクト
    - 例えば生存している配列の要素など

例えば

- 呼び出し中のフレームの1つ目 (トップレベルの呼び出し) の環境をマークする
- この環境が参照している、グローバル変数が入った配列もマークする
- この配列が参照しているグローバル変数のための参照セルをすべてマークする
- マークされた参照セルが配列型なら、その配列もマークする

といった流れになる。

具体的には、マークがついたオブジェクト i に対して `s_nanika_gc_map(i) = true` にする。

なお HSP 側の変数が参照しているオブジェクトがあっても検出できないので、GC はコマンドの実行の途中には行えない。

#### GC: ムーブ

マーク処理が終わった後は、ムーブ処理を行う。この工程では、生存しているオブジェクトに新しい要素番号を割り振って移動させる。

各種類の生存しているオブジェクトのうち、番号が低いものから順に 0, 1, 2, .. と番号を割り当てていく。新しい要素番号に、そのオブジェクトを移動させる。

オブジェクトの移動前後の番号は次のリライト処理で使うので、`s_nanika_gc_map(移動前の番号) = 移動後の番号` と記録しておく。

#### GC: リライト

ムーブ工程でオブジェクトの要素番号が変わってしまったので、古い要素番号が書かれている部分をすべて新しい要素番号に書き換える。これがリライト処理。

例えば参照セルなら、すべてのセルを確認して、型が配列なら配列の要素番号を新しいものに入れ替える。

```hsp
    s_cell_vals(cell_i) = s_array_gc_map(s_cell_vals(cell_i))
```

これをすべてのオブジェクトに対して行う。

以上で GC が完了する。性能はよくないが実装はそれほど難しくない。

#### GC: 未実装機能

いまのところ GC が回収するのは配列とヒープ領域の参照セルに限られる。文字列、クロージャ、環境も GC で回収するように実装しないといけない。

また、マークをつける処理で HSP の命令を再帰呼び出ししているが、スタックオーバーフローのおそれがあるため、ループに変形したほうがいい。
