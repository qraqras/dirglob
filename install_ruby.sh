#!/bin/bash
set -e
eval "$(~/.rbenv/bin/rbenv init -)"
~/.rbenv/bin/rbenv install 4.0.0
~/.rbenv/bin/rbenv global 4.0.0
ruby -v
