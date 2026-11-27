// Build helper: writes manifest.ttl and dw-303.ttl beside the LV2 binary
// from the binary's own parameter table.
#include <dlfcn.h>
#include <cstdio>
#include <string>
int main(int argc, char** argv) {
    if (argc != 2) { std::fprintf(stderr, "usage: dw303-lv2-ttl <bundle>/dw-303.<dylib|so>\n"); return 2; }
    const std::string path = argv[1]; const auto slash = path.rfind('/');
    void* module = dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
    if (!module) { std::fprintf(stderr, "%s\n", dlerror()); return 1; }
    auto* write = reinterpret_cast<int (*)(const char*, const char*)>(dlsym(module, "dw303_lv2_write_ttl"));
    if (!write) return 1;
    return write(slash == std::string::npos ? "." : path.substr(0, slash).c_str(), path.substr(slash + 1).c_str());
}
