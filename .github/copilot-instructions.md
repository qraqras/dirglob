# プロジェクト概要
このプロジェクト(rbcglob)はRuby4.0.0のDir.globをC99で完全再現するライブラリです。
Linux/Mac/Windowsのクロスプラットフォームで動作します。
外部依存はlibcのみとします。
シングルヘッダで提供します。

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

# 最適化
- wildmatch(https://github.com/git/git/blob/master/wildmatch.c)を参考にします
