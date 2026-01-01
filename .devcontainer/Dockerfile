FROM ubuntu:24.04

# Avoid interactive prompts during build
ENV DEBIAN_FRONTEND=noninteractive

# Install minimal packages required for C99 development
RUN apt update && apt install -y --no-install-recommends \
    ca-certificates \
    git \
    python3 \
    ruby \
    gcc \
    make \
    cmake \
    libc6-dev \
    && apt clean \
    && rm -rf /var/lib/apt/lists/*

# Prefer gcc as default
RUN update-alternatives --install /usr/bin/cc cc /usr/bin/gcc 100
