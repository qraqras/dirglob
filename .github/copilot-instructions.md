# プロジェクト概要
このプロジェクト(rbcglob)はRuby4.0.0のDir.globをC99で完全再現するライブラリです。
列挙順までは完全互換を目指しません(当面の間)。
同じくFile.fnmatchなどの関連機能も提供します。
Linux/Mac/Windowsのクロスプラットフォームで動作します。
高速軽量を目指し、依存ライブラリなしが理想です。
Rubyやglob(3)で実装されていない最適化も取り込みます。
将来的には高速な並列化オプションも予定しています(並列時の列挙順互換は保証しません)。

# 仕様
- globの仕様はRuby4.0のDir.globに準拠します
  - Documentation: https://docs.ruby-lang.org/en/4.0/Dir.html#method-c-glob
- fnmatchの仕様はRuby4.0のFile.fnmatchに準拠します
  - Documentation: https://docs.ruby-lang.org/en/4.0/File.html#method-c-fnmatch
- joinの仕様はRuby4.0のFile.joinに準拠します
  - Documentation: https://docs.ruby-lang.org/en/4.0/File.html#method-c-join
- expand_pathの仕様はRuby4.0のFile.expand_pathに準拠します
  - Documentation: https://docs.ruby-lang.org/en/4.0/File.html#method-c-expand_path

# 実装
- プレフィックスはrbcg_*です(内部実装にも使用します)
- ヒューリスティックな動作の再現ではなくMRIのロジックを忠実に再現してください
- 戻り値や引数はなるべくbool型やsize_t型を使います
- Ruby互換APIは引数名まで一致させてください
- Ruby(MRI)のDir.glob/File.fnmatchのロジックを参考にします
- Rustのglobクレートの実装を参考にします

# 最適化
- Rustのようにglobパターンをプリコンパイルする方式を採用します
  - Rustの参照実装: https://github.com/rust-lang/glob/blob/master/src/lib.rs
- NFAベースのマッチングエンジンを採用します
  - ブレース展開時の列挙順はRuby互換にするかどうか検討中です
- micromatchの最適化技術を参考にします
  - 参照実装: https://github.com/micromatch/micromatch
