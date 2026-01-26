# プロジェクト概要
このプロジェクト(rbcglob)はRuby4.0.0のDir.glob/File.fnmatch仕様互換のC99ライブラリです。
Linux/Mac/Windowsのクロスプラットフォームで動作します。
外部依存はlibcのみとします。
シングルヘッダで提供します。
開発中であり未リリース状態です。

# 仕様準拠
- globの動作仕様はRuby4.0のDir.globに準拠します
  - Documentation: https://docs.ruby-lang.org/en/4.0/Dir.html#method-c-glob
- fnmatchの動作仕様はRuby4.0のFile.fnmatchに準拠します
  - Documentation: https://docs.ruby-lang.org/en/4.0/File.html#method-c-fnmatch
- **重要**: 公式ドキュメントとテストケースのみを参照し、MRI実装コードは参照しないこと（ライセンス上の理由）

# 実装方針
- プレフィックスはrbc_*です(内部実装にも使用します)
- **独自アルゴリズムで実装**し、仕様に準拠した動作を実現してください
- 戻り値や引数はなるべくbool型やsize_t型を使います
- Ruby互換APIは引数名を一致させてください（公開仕様のため問題なし）

# 参考実装（ライセンス確認済み）
- POSIX glob/fnmatch標準（パブリックドメイン仕様）
- musl libc glob実装（MIT License）
- BSD libc glob実装（BSD License）
- wildmatch (Git) - アルゴリズムのアイデアのみ参考
