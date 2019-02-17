# ネギ言語

ネギ言語とは、HSP3 でスクリプト言語の処理系を書くプロジェクト。

## 機能

未実装

## 開発環境

ソースコードを utf-8 で保存しているため、標準のスクリプトエディタで表示するとコメントが文字化けしてしまうので注意。

[テキストエディタ Atom](https://atom.io/) をインストールして、以下のパッケージを入れる。

- [editorconfig](https://atom.io/packages/editorconfig)
- [language-hsp3](https://github.com/honobonosun/language-hsp3)
    - リンク先に書かれているように、hspc をインストールする。
    - パッケージの設定から hspc の絶対パスを設定する。
- [linter-hsp3](https://github.com/honobonosun/linter-hsp3)

## 開発メモ

### テスト

`neig_lang_tests.hsp` を実行して `status pass` が出たらOK。
