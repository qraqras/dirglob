require 'mkmf'

# Add include paths
$INCFLAGS << " -I#{File.expand_path('../../include', __dir__)}"

# Add library path and link
$LDFLAGS << " -L#{File.expand_path('../../build/src', __dir__)}"
$libs << " -lrbcglob"

create_makefile('rbcglob/rbcglob')
