// LV2 instrument and Cocoa (macOS) or X11 (Linux) UI in one binary. Ports: atom input (MIDI and
// time:Position), stereo audio output, then one control port per parameter in
// physical units. The complete record (parameters and pattern memory) is LV2
// state. The UI uses instance-access to attach the panel to this engine.
#include "dw303.h"
#include "identity.h"
#include <lv2/core/lv2.h>
#include <lv2/atom/atom.h>
#include <lv2/atom/util.h>
#include <lv2/midi/midi.h>
#include <lv2/state/state.h>
#include <lv2/time/time.h>
#include <lv2/urid/urid.h>
#include <lv2/ui/ui.h>
#include <lv2/instance-access/instance-access.h>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>

namespace {
enum Port : uint32_t { Events, OutputLeft, OutputRight, FirstControl };
constexpr size_t parameterCount = 12;
constexpr uint32_t maximumFrames = 8192, maximumMidi = 1024;

struct Urids {
    LV2_URID midi, position, bar, barBeat, beatsPerBar, beatsPerMinute, speed, frame, chunk, state, object, blank;
    LV2_URID floatType, doubleType, intType, longType;
};

struct DW303LV2 {
    const LV2_Atom_Sequence* events = nullptr;
    float* outputs[2]{};
    const float* controls[parameterCount]{};
    std::array<float, parameterCount> consumed{};
    std::array<Dw303Midi, maximumMidi> midi{};
    Dw303* dsp = nullptr;
    Urids urid{};
    Dw303Transport transport{};
    ~DW303LV2() { dw303_destroy(dsp); }
};

LV2_Handle instantiate(const LV2_Descriptor*, double rate, const char*, const LV2_Feature* const* features) {
    LV2_URID_Map* map = nullptr;
    for (auto* const* f = features; f && *f; ++f) if (!std::strcmp((*f)->URI, LV2_URID__map)) map = static_cast<LV2_URID_Map*>((*f)->data);
    if (!map || !std::isfinite(rate)) return nullptr;
    auto plugin = std::make_unique<DW303LV2>();
    plugin->dsp = dw303_create();
    if (!plugin->dsp || !dw303_prepare(plugin->dsp, rate, maximumFrames)) return nullptr;
    auto m = [&](const char* uri) { return map->map(map->handle, uri); };
    plugin->urid = {m(LV2_MIDI__MidiEvent), m(LV2_TIME__Position), m(LV2_TIME__bar), m(LV2_TIME__barBeat), m(LV2_TIME__beatsPerBar),
                    m(LV2_TIME__beatsPerMinute), m(LV2_TIME__speed), m(LV2_TIME__frame), m(LV2_ATOM__Chunk), m(DW303_LV2_URI "#state"),
                    m(LV2_ATOM__Object), m(LV2_ATOM__Blank), m(LV2_ATOM__Float), m(LV2_ATOM__Double), m(LV2_ATOM__Int), m(LV2_ATOM__Long)};
    for (size_t i = 0; i < parameterCount; ++i) plugin->consumed[i] = dw303_value(plugin->dsp, uint32_t(i));
    return plugin.release();
}
void connectPort(LV2_Handle handle, uint32_t port, void* data) {
    auto* s = static_cast<DW303LV2*>(handle);
    if (port == Events) s->events = static_cast<const LV2_Atom_Sequence*>(data);
    else if (port == OutputLeft || port == OutputRight) s->outputs[port - OutputLeft] = static_cast<float*>(data);
    else if (port - FirstControl < parameterCount) s->controls[port - FirstControl] = static_cast<const float*>(data);
}
void activate(LV2_Handle handle) { dw303_reset(static_cast<DW303LV2*>(handle)->dsp); }

void readPosition(DW303LV2& s, const LV2_Atom_Object* object) {
    const LV2_Atom *bar = nullptr, *barBeat = nullptr, *perBar = nullptr, *bpm = nullptr, *speed = nullptr, *frame = nullptr;
    lv2_atom_object_get(object, s.urid.bar, &bar, s.urid.barBeat, &barBeat, s.urid.beatsPerBar, &perBar,
                        s.urid.beatsPerMinute, &bpm, s.urid.speed, &speed, s.urid.frame, &frame, 0);
    const auto& u = s.urid;
    auto number = [&u](const LV2_Atom* a, double& out) {
        if (!a) return false;
        if (a->type == u.floatType) out = reinterpret_cast<const LV2_Atom_Float*>(a)->body;
        else if (a->type == u.doubleType) out = reinterpret_cast<const LV2_Atom_Double*>(a)->body;
        else if (a->type == u.intType) out = reinterpret_cast<const LV2_Atom_Int*>(a)->body;
        else if (a->type == u.longType) out = double(reinterpret_cast<const LV2_Atom_Long*>(a)->body);
        else return false;
        return std::isfinite(out);
    };
    double value = 0, beats = 0, beatsPerBar = 0;
    auto& t = s.transport;
    if (number(speed, value)) { t.flags |= DW303_TRANSPORT_PLAYING_VALID; t.flags = value != 0 ? t.flags | DW303_TRANSPORT_PLAYING : t.flags & ~DW303_TRANSPORT_PLAYING; }
    if (number(bpm, value)) { t.flags |= DW303_TRANSPORT_TEMPO; t.tempo = value; }
    if (number(bar, value) && number(perBar, beatsPerBar) && number(barBeat, beats)) { t.flags |= DW303_TRANSPORT_BEATS; t.beats = value * beatsPerBar + beats; }
    if (number(frame, value)) { t.flags |= DW303_TRANSPORT_SAMPLE; t.sample = int64_t(value); }
}

void run(LV2_Handle handle, uint32_t frames) {
    auto* s = static_cast<DW303LV2*>(handle);
    if (!s->outputs[0] || !s->outputs[1]) return;
    std::array<Dw303Param, parameterCount> values; size_t valueCount = 0, midiCount = 0;
    for (size_t i = 0; i < parameterCount; ++i)
        if (s->controls[i] && std::isfinite(*s->controls[i]) && *s->controls[i] != s->consumed[i]) {
            values[valueCount++] = {0, uint32_t(i), *s->controls[i]};
            s->consumed[i] = *s->controls[i];
        }
    Dw303Transport start = s->transport;
    if (s->events) {
        LV2_ATOM_SEQUENCE_FOREACH(s->events, event) {
            if (event->body.type == s->urid.midi && event->body.size >= 1 && event->body.size <= 3 && midiCount < maximumMidi) {
                const auto* bytes = reinterpret_cast<const uint8_t*>(event + 1);
                Dw303Midi m{uint32_t(std::max<int64_t>(0, event->time.frames)), event->body.size, {}};
                std::memcpy(m.bytes, bytes, event->body.size);
                s->midi[midiCount++] = m;
            } else if (event->body.type == s->urid.object || event->body.type == s->urid.blank) {
                const auto* object = reinterpret_cast<const LV2_Atom_Object*>(&event->body);
                if (object->body.otype == s->urid.position) { readPosition(*s, object); if (event->time.frames == 0) start = s->transport; }
            }
        }
    }
    dw303_process(s->dsp, s->outputs[0], s->outputs[1], frames, s->midi.data(), midiCount, values.data(), valueCount, &start);
    // Host position only arrives on change; advance it across this block.
    if ((s->transport.flags & DW303_TRANSPORT_PLAYING) && (s->transport.flags & DW303_TRANSPORT_SAMPLE)) s->transport.sample += frames;
}
void cleanup(LV2_Handle handle) { delete static_cast<DW303LV2*>(handle); }

LV2_State_Status save(LV2_Handle handle, LV2_State_Store_Function store, LV2_State_Handle state, uint32_t, const LV2_Feature* const*) {
    auto* s = static_cast<DW303LV2*>(handle);
    size_t size = 0; auto* bytes = dw303_state_save(s->dsp, &size);
    if (!bytes) return LV2_STATE_ERR_UNKNOWN;
    auto status = store(state, s->urid.state, bytes, size, s->urid.chunk, LV2_STATE_IS_POD | LV2_STATE_IS_PORTABLE);
    dw303_bytes_free(bytes, size);
    return status;
}
LV2_State_Status restore(LV2_Handle handle, LV2_State_Retrieve_Function retrieve, LV2_State_Handle state, uint32_t, const LV2_Feature* const*) {
    auto* s = static_cast<DW303LV2*>(handle);
    size_t size = 0; uint32_t type = 0, flags = 0;
    const void* bytes = retrieve(state, s->urid.state, &size, &type, &flags);
    if (!bytes || type != s->urid.chunk) return LV2_STATE_ERR_NO_PROPERTY;
    return dw303_state_load(s->dsp, static_cast<const uint8_t*>(bytes), size) ? LV2_STATE_SUCCESS : LV2_STATE_ERR_BAD_TYPE;
}
const void* extensionData(const char* uri) {
    static const LV2_State_Interface state{save, restore};
    return uri && !std::strcmp(uri, LV2_STATE__interface) ? &state : nullptr;
}
const LV2_Descriptor descriptor{DW303_LV2_URI, instantiate, connectPort, activate, run, nullptr, cleanup, extensionData};

// UI -------------------------------------------------------------------------
struct DW303LV2UI {
    LV2UI_Write_Function write = nullptr;
    LV2UI_Controller controller = nullptr;
    const LV2UI_Touch* touch = nullptr;
    Dw303Editor* editor = nullptr;
    ~DW303LV2UI() { if (editor) dw303_editor_destroy(editor); }
};

LV2UI_Handle uiInstantiate(const LV2UI_Descriptor*, const char*, const char*, LV2UI_Write_Function write, LV2UI_Controller controller,
                           LV2UI_Widget* widget, const LV2_Feature* const* features) {
    void* parent = nullptr; DW303LV2* plugin = nullptr; const LV2UI_Touch* touch = nullptr; const LV2UI_Resize* resize = nullptr;
    for (auto* const* f = features; f && *f; ++f) {
        if (!std::strcmp((*f)->URI, LV2_UI__parent)) parent = (*f)->data;
        else if (!std::strcmp((*f)->URI, LV2_UI__resize)) resize = static_cast<const LV2UI_Resize*>((*f)->data);
        else if (!std::strcmp((*f)->URI, LV2_INSTANCE_ACCESS_URI)) plugin = static_cast<DW303LV2*>((*f)->data);
        else if (!std::strcmp((*f)->URI, LV2_UI__touch)) touch = static_cast<const LV2UI_Touch*>((*f)->data);
    }
    if (!parent || !plugin) return nullptr;
    auto ui = std::make_unique<DW303LV2UI>();
    ui->write = write; ui->controller = controller; ui->touch = touch;
    Dw303EditorHost host{ui.get(),
        [](void* c, uint32_t i) { auto* u = static_cast<DW303LV2UI*>(c); if (u->touch) u->touch->touch(u->touch->handle, FirstControl + i, true); },
        [](void* c, uint32_t i, float v) { auto* u = static_cast<DW303LV2UI*>(c); u->write(u->controller, FirstControl + i, sizeof(float), 0, &v); },
        [](void* c, uint32_t i) { auto* u = static_cast<DW303LV2UI*>(c); if (u->touch) u->touch->touch(u->touch->handle, FirstControl + i, false); }};
    ui->editor = dw303_editor_create(plugin->dsp, host);
    if (!ui->editor || !dw303_editor_attach(ui->editor, parent)) return nullptr;
    uint32_t width = 0, height = 0; dw303_editor_default_size(&width, &height);
#if !defined(__APPLE__)
    // X11 parents are sized by the host; ask for the panel's default size.
    dw303_editor_set_size(ui->editor, width, height);
#endif
    if (resize) resize->ui_resize(resize->handle, int(width), int(height));
    *widget = parent;
    return ui.release();
}
void uiCleanup(LV2UI_Handle handle) { delete static_cast<DW303LV2UI*>(handle); }
// The panel reads values from the engine; port echoes need no handling.
void uiPortEvent(LV2UI_Handle, uint32_t, uint32_t, uint32_t, const void*) {}
// X11 UIs run GTK work from the host's idle callback.
const void* uiExtensionData(const char* uri) {
#if defined(__APPLE__)
    (void)uri;
    return nullptr;
#else
    static const LV2UI_Idle_Interface idle{[](LV2UI_Handle) { dw303_editor_idle(); return 0; }};
    return uri && !std::strcmp(uri, LV2_UI__idleInterface) ? &idle : nullptr;
#endif
}
const LV2UI_Descriptor uiDescriptor{DW303_LV2_URI "#ui", uiInstantiate, uiCleanup, uiPortEvent, uiExtensionData};

std::string turtleNumber(float value) { char text[32]; std::snprintf(text, sizeof(text), "%.6g", double(value)); return text; }
} // namespace

extern "C" {
LV2_SYMBOL_EXPORT const LV2_Descriptor* lv2_descriptor(uint32_t index) { return index == 0 ? &descriptor : nullptr; }
LV2_SYMBOL_EXPORT const LV2UI_Descriptor* lv2ui_descriptor(uint32_t index) { return index == 0 ? &uiDescriptor : nullptr; }

// Build helper: write manifest.ttl and dw-303.ttl from the parameter table.
LV2_SYMBOL_EXPORT int dw303_lv2_write_ttl(const char* directory, const char* binary) {
    const std::string dir(directory);
#if defined(__APPLE__)
    const char* uiClass = "ui:CocoaUI"; const char* uiExtras = "";
#else
    const char* uiClass = "ui:X11UI";
    const char* uiExtras = " ;\n    lv2:requiredFeature ui:idleInterface ;\n    lv2:extensionData ui:idleInterface";
#endif
    FILE* manifest = std::fopen((dir + "/manifest.ttl").c_str(), "w");
    if (!manifest) return 1;
    std::fprintf(manifest,
        "@prefix lv2: <http://lv2plug.in/ns/lv2core#> .\n@prefix rdfs: <http://www.w3.org/2000/01/rdf-schema#> .\n"
        "@prefix ui: <http://lv2plug.in/ns/extensions/ui#> .\n\n"
        "<%s> a lv2:Plugin, lv2:InstrumentPlugin ;\n    lv2:binary <%s> ;\n    rdfs:seeAlso <dw-303.ttl> .\n\n"
        "<%s#ui> a %s ;\n    ui:binary <%s> ;\n    lv2:requiredFeature <http://lv2plug.in/ns/ext/instance-access> ;\n"
        "    lv2:optionalFeature ui:resize%s .\n",
        DW303_LV2_URI, binary, DW303_LV2_URI, uiClass, binary, uiExtras);
    std::fclose(manifest);
    FILE* ttl = std::fopen((dir + "/dw-303.ttl").c_str(), "w");
    if (!ttl) return 1;
    std::fprintf(ttl,
        "@prefix atom: <http://lv2plug.in/ns/ext/atom#> .\n@prefix doap: <http://usefulinc.com/ns/doap#> .\n"
        "@prefix lv2: <http://lv2plug.in/ns/lv2core#> .\n@prefix midi: <http://lv2plug.in/ns/ext/midi#> .\n"
        "@prefix state: <http://lv2plug.in/ns/ext/state#> .\n@prefix time: <http://lv2plug.in/ns/ext/time#> .\n"
        "@prefix rdf: <http://www.w3.org/1999/02/22-rdf-syntax-ns#> .\n@prefix rdfs: <http://www.w3.org/2000/01/rdf-schema#> .\n"
        "@prefix ui: <http://lv2plug.in/ns/extensions/ui#> .\n@prefix urid: <http://lv2plug.in/ns/ext/urid#> .\n\n"
        "<%s>\n    doap:name \"%s\" ;\n    doap:license <https://spdx.org/licenses/GPL-3.0-or-later> ;\n"
        "    lv2:requiredFeature urid:map ;\n    lv2:optionalFeature lv2:hardRTCapable ;\n    lv2:extensionData state:interface ;\n"
        "    ui:ui <%s#ui> ;\n    lv2:port [\n        a lv2:InputPort, atom:AtomPort ;\n        atom:bufferType atom:Sequence ;\n"
        "        atom:supports midi:MidiEvent, time:Position ;\n        lv2:designation lv2:control ;\n"
        "        lv2:index 0 ;\n        lv2:symbol \"events\" ;\n        lv2:name \"MIDI input\"\n    ] , [\n"
        "        a lv2:OutputPort, lv2:AudioPort ;\n        lv2:index 1 ;\n        lv2:symbol \"out_left\" ;\n        lv2:name \"Left\"\n    ] , [\n"
        "        a lv2:OutputPort, lv2:AudioPort ;\n        lv2:index 2 ;\n        lv2:symbol \"out_right\" ;\n        lv2:name \"Right\"\n    ]",
        DW303_LV2_URI, DW303_NAME, DW303_LV2_URI);
    for (uint32_t i = 0; i < dw303_param_count(); ++i) {
        Dw303ParamInfo p; dw303_param_info(i, &p);
        std::string symbol;
        for (const char* c = p.name; *c; ++c) symbol += std::isalnum(static_cast<unsigned char>(*c)) ? char(std::tolower(*c)) : '_';
        std::fprintf(ttl, " , [\n        a lv2:InputPort, lv2:ControlPort ;\n        lv2:index %u ;\n        lv2:symbol \"%s\" ;\n"
                          "        lv2:name \"%s\" ;\n        lv2:default %s ;\n        lv2:minimum %s ;\n        lv2:maximum %s%s",
                     FirstControl + i, symbol.c_str(), p.name, turtleNumber(p.initial).c_str(), turtleNumber(p.minimum).c_str(),
                     turtleNumber(p.maximum).c_str(), p.steps ? " ;\n        lv2:portProperty lv2:integer, lv2:enumeration" : "");
        for (uint32_t step = 0; p.steps && step <= p.steps; ++step) {
            char label[64]; dw303_param_text(i, p.minimum + float(step), label, sizeof(label));
            std::fprintf(ttl, " ;\n        lv2:scalePoint [ rdfs:label \"%s\" ; rdf:value %u ]", label, unsigned(p.minimum) + step);
        }
        std::fprintf(ttl, "\n    ]");
    }
    std::fprintf(ttl, " .\n");
    std::fclose(ttl);
    return 0;
}
}
