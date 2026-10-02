FROM --platform=linux/amd64 node:24-bookworm-slim AS node
FROM --platform=linux/amd64 ghcr.io/astral-sh/uv:latest AS uv
FROM espressif/esp-matter:release-v1.5
COPY --from=node /usr/local/bin/node /usr/local/bin/node
COPY --from=node /usr/local/lib/node_modules /usr/local/lib/node_modules
RUN ln -sf ../lib/node_modules/npm/bin/npm-cli.js /usr/local/bin/npm
COPY --from=uv /uv /uvx /usr/local/bin/
COPY ubuntu.sources /etc/apt/sources.list.d/ubuntu.sources
RUN apt-get update \
    && apt-get install -y --no-install-recommends ccache zip unzip libsdl2-dev \
    && rm -rf /var/lib/apt/lists/*
RUN mkdir -p /work /opt/ccache-bin \
    && for name in xtensa-esp32-elf-gcc xtensa-esp32-elf-g++ xtensa-esp32-elf-cc xtensa-esp32-elf-c++; do ln -s "$(command -v ccache)" "/opt/ccache-bin/$name"; done
ENV CCACHE_DIR=/root/.cache/ccache
