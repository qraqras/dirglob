# プロジェクト概要
このプロジェクト(rbcglob)はRuby4.0.0のDir.globをC99で完全再現するライブラリです。
Linux/Mac/Windowsのクロスプラットフォームで動作します。
高速軽量を目指し、外部依存はlibcのみとします。
Amalgamation形式で提供され、組み込みも容易です。

# 仕様
- globの仕様はRuby4.0のDir.globに準拠します
  - Documentation: https://docs.ruby-lang.org/en/4.0/Dir.html#method-c-glob
- fnmatchの仕様はRuby4.0のFile.fnmatchに準拠します
  - Documentation: https://docs.ruby-lang.org/en/4.0/File.html#method-c-fnmatch

# 実装
- プレフィックスはrbc_*です(内部実装にも使用します)
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
