# プロジェクト概要
このプロジェクトはRuby4.0のDir.globをC99で再現するライブラリです。
Linux/Mac/Windowsのクロスプラットフォームで動作します。

# 仕様
仕様はRuby4.0のDir.globに準拠します。
Documentation: https://docs.ruby-lang.org/en/4.0/Dir.html#method-c-glob

# 実装
ロジックはMRIを参考に、実装はCPythonを参考にしてください(OS依存をなるべく少なく自前実装の正規表現を使う)。
