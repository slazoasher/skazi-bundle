// Minimal LV2 host check: instantiate the built binary, play a MIDI note
// through the atom port, follow a control port and round-trip LV2 state.
#include <lv2/core/lv2.h>
#include <lv2/atom/atom.h>
#include <lv2/atom/util.h>
#include <lv2/midi/midi.h>
#include <lv2/state/state.h>
#include <lv2/urid/urid.h>
#include <dlfcn.h>
#include <array>
#include <cmath>
#include <cstring>
#include <iostream>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>

static void require(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
static std::map<std::string, LV2_URID> uris;
static LV2_URID mapUri(LV2_URID_Map_Handle, const char* uri) {
    auto found = uris.find(uri);
    if (found != uris.end()) return found->second;
    const auto id = LV2_URID(uris.size() + 1);
    uris.emplace(uri, id);
    return id;
}
struct Saved { uint32_t key = 0, type = 0; std::vector<uint8_t> value; };
static LV2_State_Status store(LV2_State_Handle h, uint32_t key, const void* value, size_t size, uint32_t type, uint32_t) {
    auto* s = static_cast<Saved*>(h); s->key = key; s->type = type;
    s->value.assign(static_cast<const uint8_t*>(value), static_cast<const uint8_t*>(value) + size);
    return LV2_STATE_SUCCESS;
}
static const void* retrieve(LV2_State_Handle h, uint32_t key, size_t* size, uint32_t* type, uint32_t* flags) {
    auto* s = static_cast<Saved*>(h);
    if (key != s->key) return nullptr;
    *size = s->value.size(); *type = s->type; *flags = LV2_STATE_IS_POD;
    return s->value.data();
}

#if defined(__APPLE__)
static const char* const binaryName = "/dw-303.dylib";
#else
static const char* const binaryName = "/dw-303.so";
#endif
int main(int argc, char** argv) {
    try {
        require(argc == 2, "usage: dw303-lv2-smoke <dw-303.lv2>");
        void* module = dlopen((std::string(argv[1]) + binaryName).c_str(), RTLD_NOW | RTLD_LOCAL); require(module, "load binary");
        auto entry = reinterpret_cast<const LV2_Descriptor* (*)(uint32_t)>(dlsym(module, "lv2_descriptor")); require(entry, "lv2_descriptor");
        const LV2_Descriptor* d = entry(0); require(d && !entry(1), "one descriptor");
        LV2_URID_Map map{nullptr, mapUri};
        LV2_Feature mapFeature{LV2_URID__map, &map};
        const LV2_Feature* features[] = {&mapFeature, nullptr};
        LV2_Handle plugin = d->instantiate(d, 48000, argv[1], features); require(plugin, "instantiate");

        constexpr uint32_t frames = 512, controls = 12;
        std::array<float, frames> left{}, right{};
        std::array<float, controls> values{0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.7f, 120, 0, 1, 1, 0};
        alignas(8) std::array<uint8_t, 1024> buffer{};
        auto* sequence = reinterpret_cast<LV2_Atom_Sequence*>(buffer.data());
        auto reset = [&] { sequence->atom.type = mapUri(nullptr, LV2_ATOM__Sequence); sequence->atom.size = sizeof(LV2_Atom_Sequence_Body); sequence->body = {0, 0}; };
        auto note = [&](uint8_t status, uint8_t key, uint8_t velocity) {
            auto* event = lv2_atom_sequence_end(&sequence->body, sequence->atom.size);
            event->time.frames = 0; event->body.type = mapUri(nullptr, LV2_MIDI__MidiEvent); event->body.size = 3;
            uint8_t bytes[3] = {status, key, velocity}; std::memcpy(event + 1, bytes, 3);
            sequence->atom.size += uint32_t(lv2_atom_pad_size(sizeof(LV2_Atom_Event) + 3));
        };
        d->connect_port(plugin, 0, sequence); d->connect_port(plugin, 1, left.data()); d->connect_port(plugin, 2, right.data());
        for (uint32_t i = 0; i < controls; ++i) d->connect_port(plugin, 3 + i, &values[i]);
        d->activate(plugin);
        float peak = 0;
        reset(); note(0x90, 48, 110);
        for (int block = 0; block < 40; ++block) {
            d->run(plugin, frames); reset();
            for (uint32_t i = 0; i < frames; ++i) { require(std::isfinite(left[i]) && left[i] == right[i], "finite output"); peak = std::max(peak, std::fabs(left[i])); }
        }
        require(peak > 1e-3f, "note renders audio");

        auto* state = static_cast<const LV2_State_Interface*>(d->extension_data(LV2_STATE__interface)); require(state, "state interface");
        values[1] = 0.9f; d->run(plugin, frames);
        Saved saved; require(state->save(plugin, store, &saved, 0, nullptr) == LV2_STATE_SUCCESS && !saved.value.empty(), "save state");
        require(state->restore(plugin, retrieve, &saved, 0, nullptr) == LV2_STATE_SUCCESS, "restore state");
        saved.value.pop_back();
        require(state->restore(plugin, retrieve, &saved, 0, nullptr) != LV2_STATE_SUCCESS, "reject truncated state");
        d->deactivate ? d->deactivate(plugin) : void();
        d->cleanup(plugin);
        std::cout << "LV2: note peak " << peak << "; state saved (" << saved.value.size() + 1 << " bytes) and restored\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << "FAILED: " << error.what() << '\n'; return 1; }
}
