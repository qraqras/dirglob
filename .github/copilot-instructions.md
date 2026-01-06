# プロジェクト概要
このプロジェクト(rbcglob)はRuby4.0.0のDir.globをC99で完全再現するライブラリです。
Linux/Mac/Windowsのクロスプラットフォームで動作します。
高速軽量を目指し、依存ライブラリを持たずに実装します。
Rubyやglob(3)で実装されていない最適化も取り込みます。

# 仕様
- 仕様はRuby4.0のDir.globに準拠します
  - Documentation: https://docs.ruby-lang.org/en/4.0/Dir.html#method-c-glob

# 実装
- プレフィックスはrbcglob_*です(内部実装にも使用します)
- Ruby(MRI)のDir.glob/File.fnmatchのロジックを参考にします
- Rustのglobクレートの実装を参考にします

# 最適化
- Rustのようにglobパターンをプリコンパイルする方式を採用します
  - Rustの参照実装: https://github.com/rust-lang/glob/blob/master/src/lib.rs
