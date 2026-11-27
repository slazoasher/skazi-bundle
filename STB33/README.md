# DW-303

A circuit-modelled bass line synthesizer and step sequencer, built on RAVET.
Formats: CLAP, AU, VST3, LV2, AAX and standalone.

![DW-303 standalone app](docs/images/dw-303-standalone.png)

Installers for macOS, Windows and Linux are on the
[DW-303 page at djwaudio.co](https://djwaudio.co/#dw-303).

| Platform | Formats | Installer |
|---|---|---|
| macOS 11+ (Apple Silicon, Intel) | VST3, AU, AAX, LV2, CLAP, standalone | signed, notarised `.dmg` |
| Windows 10/11 x64 | VST3, CLAP, standalone | `.exe` (not yet code-signed) |
| Linux x86_64 | VST3, LV2, CLAP, standalone | `.tar.gz` with `install.sh` |

The panel editor needs the Microsoft Edge WebView2 Runtime on Windows, and
GTK 3 with WebKitGTK 4.1 on Linux (X11 or XWayland). Without them the
plug-ins still load and play, with the host's generic controls; Flatpak hosts
(for example Bitwig Studio's Flatpak) cannot see system WebKitGTK and show
generic controls.

## Building from source

DW-303 is built on [RAVET](https://github.com/mo0kid/ravet), which must be
checked out next to this repository. You need Rust 1.88 or later, Python
3.11+, CMake 3.25+ and Ninja on every platform.

```sh
git clone https://github.com/mo0kid/ravet.git
git clone https://github.com/mo0kid/DW-303.git
cd DW-303
cargo test --workspace
```

The VST3, AU, AAX and LV2 adapters also need the format SDKs in
`target/sdks`; [docs/plugins.md](docs/plugins.md) lists them per platform.
`scripts/bundle-plugins.py` builds every format for the platform it runs on
and stages it under `target/plugins`; `--check` also runs the VST3 validator
and the AU/LV2 smoke hosts.

### macOS

Xcode command-line tools (`xcode-select --install`), then:

```sh
rustup target add aarch64-apple-darwin x86_64-apple-darwin
python3 scripts/bundle-plugins.py --universal --check
installer/build-mac-installer.sh       # signed .pkg inside a .dmg
```

`scripts/bundle-clap.py` and `scripts/bundle-app.py` build just the CLAP
bundle or the standalone `DW-303.app`.

### Windows

Visual Studio 2022 Build Tools ("Desktop development with C++"), Rust with the
MSVC toolchain, and [Inno Setup 6.3+](https://jrsoftware.org/isinfo.php) for the
installer. Extract the `Microsoft.Web.WebView2` NuGet package into
`target/sdks/WebView2`, then from a Developer PowerShell for VS 2022:

```powershell
py -3 scripts\bundle-plugins.py --check
powershell -ExecutionPolicy Bypass -File installer\build-windows-installer.ps1 -SkipBuild
```

The released installer is cross-built on Linux with MinGW-w64 and Inno Setup
under Wine (`python3 scripts/bundle-plugins.py --cross-windows`, then
`installer/build-windows-installer.sh`); see [docs/plugins.md](docs/plugins.md)
for the Docker image.

### Linux

```sh
sudo apt install build-essential cmake ninja-build pkg-config \
  libgtk-3-dev libwebkit2gtk-4.1-dev libjson-glib-dev libasound2-dev
python3 scripts/bundle-plugins.py --check
installer/build-linux-tarball.sh --skip-build
```

Release tarballs are built in `installer/linux/release.Dockerfile` (Ubuntu
22.04) so they run on any distribution with glibc 2.35 or later.

Fedora: `gcc-c++ cmake ninja-build pkgconf-pkg-config gtk3-devel
webkit2gtk4.1-devel json-glib-devel alsa-lib-devel`. Arch: `base-devel cmake
ninja gtk3 webkit2gtk-4.1 json-glib alsa-lib`.

See [docs/standalone.md](docs/standalone.md), [docs/editor.md](docs/editor.md)
and [docs/sequencer.md](docs/sequencer.md) for details, and
[docs/circuit](docs/circuit) for the circuit models and how they were fitted.

## License

The source code is licensed under the GNU General Public License v3.0 or later.
See [LICENSE](LICENSE).

The DW-303 name, DJW Audio name and logo, and the panel artwork are not covered
by the code license. Forks and redistributed builds must use a different name
and artwork.

TB-303 and Roland are trademarks of Roland Corporation. DW-303 is an independent
project and is not affiliated with or endorsed by Roland.
