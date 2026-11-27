# Windows x64 cross build of DW-303 on Linux: MinGW-w64, Rust, CMake/Ninja, and
# Wine with Inno Setup to run the VST3 validator and compile the installer.
FROM ubuntu:24.04
ENV DEBIAN_FRONTEND=noninteractive
RUN dpkg --add-architecture i386 && apt-get update && apt-get install -y --no-install-recommends \
    build-essential cmake ninja-build pkg-config git curl ca-certificates python3 unzip xvfb xauth \
    g++-mingw-w64-x86-64-posix gcc-mingw-w64-x86-64-posix wine wine64 wine32:i386 \
 && rm -rf /var/lib/apt/lists/* \
 && update-alternatives --set x86_64-w64-mingw32-g++ /usr/bin/x86_64-w64-mingw32-g++-posix \
 && update-alternatives --set x86_64-w64-mingw32-gcc /usr/bin/x86_64-w64-mingw32-gcc-posix
RUN curl -sSf https://sh.rustup.rs | sh -s -- -y --profile minimal --default-toolchain 1.88.0 \
 && /root/.cargo/bin/rustup target add x86_64-pc-windows-gnu
ENV PATH=/root/.cargo/bin:$PATH WINEDEBUG=-all WINEPREFIX=/opt/wine
ARG INNO=6.7.4
ARG INNO_TAG=is-6_7_4
RUN curl -sSfL -o /tmp/is.exe "https://github.com/jrsoftware/issrc/releases/download/${INNO_TAG}/innosetup-${INNO}.exe" \
 && xvfb-run -a wine /tmp/is.exe /VERYSILENT /SUPPRESSMSGBOXES /NORESTART /CURRENTUSER /DIR="C:\\InnoSetup" \
 && rm /tmp/is.exe && test -f "$WINEPREFIX/drive_c/InnoSetup/ISCC.exe"
