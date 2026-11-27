# Release baseline for DW-303 Linux binaries: glibc 2.35.
FROM ubuntu:22.04
ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y --no-install-recommends build-essential pkg-config git curl ca-certificates \
    python3.11 python3-pip libgtk-3-dev libwebkit2gtk-4.1-dev libjson-glib-dev libasound2-dev xvfb xauth dbus-x11 \
 && rm -rf /var/lib/apt/lists/* && ln -sf /usr/bin/python3.11 /usr/local/bin/python3 \
 && python3.11 -m pip install --no-cache-dir "cmake>=3.25,<4" ninja
RUN curl -sSf https://sh.rustup.rs | sh -s -- -y --profile minimal --default-toolchain 1.88.0
ENV PATH=/root/.cargo/bin:$PATH
