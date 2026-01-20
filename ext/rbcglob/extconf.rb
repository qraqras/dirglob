require 'mkmf'

# ライブラリの検索パス
$INCFLAGS << " -I#{File.expand_path('../../include', __dir__)}"
$LDFLAGS << " -L#{File.expand_path('../../build/src', __dir__)}"

# rbc静的ライブラリをリンク
have_library('rbc') or abort 'librbc.a not found'

create_makefile('rbcglob/rbcglob')
