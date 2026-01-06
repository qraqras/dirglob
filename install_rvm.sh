#!/bin/bash

# Install RVM (Ruby Version Manager)ruby
apt-add-repository -y ppa:rael-gc/rvm
apt update
apt install -y rvm

# Load RVM scripts
source /usr/share/rvm/scripts/rvm

echo 'source "/usr/share/rvm/scripts/rvm"' >> ~/.bashrc

# Install Ruby
rvm install ruby-4.0.0
