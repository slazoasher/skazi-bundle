// VST3 instrument: one component that is also its own edit controller, so the
// editor reaches the same engine instance as the processor. Host parameter
// values are normalized; the engine receives physical values.
#include "dw303.h"
#include "identity.h"
#include "public.sdk/source/vst/vstsinglecomponenteffect.h"
#include "public.sdk/source/common/pluginview.h"
#include "public.sdk/source/main/pluginfactory.h"
#include "pluginterfaces/base/ibstream.h"
#include "pluginterfaces/vst/ivstevents.h"
#include "pluginterfaces/vst/ivstparameterchanges.h"
#include "pluginterfaces/vst/ivstprocesscontext.h"
#include "base/source/fstring.h"
#include "pluginterfaces/gui/iplugviewcontentscalesupport.h"
#if defined(__linux__)
#include "pluginterfaces/gui/iplugview.h"
#endif
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <vector>

using namespace Steinberg;
using namespace Steinberg::Vst;

static const FUID componentID(0x5A7B3331, 0xA5E14B33, 0x9C2D5157, 0x15A20033);
constexpr uint32 maximumEvents = 1024;
constexpr size_t parameterCount = 22;

static int32 indexOf(ParamID id) {
    Dw303ParamInfo info;
    for (uint32 i = 0; i < dw303_param_count(); ++i)
        if (dw303_param_info(i, &info) && info.id == id) return int32(i);
    return -1;
}

#if defined(__APPLE__)
static const FIDString platformType = kPlatformTypeNSView;
#elif defined(_WIN32)
static const FIDString platformType = kPlatformTypeHWND;
#else
static const FIDString platformType = kPlatformTypeX11EmbedWindowID;
#endif

#if defined(__linux__)
// Linux editors run GTK work from the host's run loop (Linux::IRunLoop).
class IdleTimer final : public FObject, public Linux::ITimerHandler {
public:
    void PLUGIN_API onTimer() override { dw303_editor_idle(); }
    OBJ_METHODS(IdleTimer, FObject)
    DEFINE_INTERFACES
        DEF_INTERFACE(Linux::ITimerHandler)
    END_DEFINE_INTERFACES(FObject)
    REFCOUNT_METHODS(FObject)
};
#endif

class DW303Vst3;
// Sizes are points on macOS and physical pixels on Windows/Linux, where the
// host's content scale widens the permitted panel range.
class DW303View final : public CPluginView, public IPlugViewContentScaleSupport {
public:
    explicit DW303View(DW303Vst3* owner);
    ~DW303View() override { close(); }
    tresult PLUGIN_API isPlatformTypeSupported(FIDString type) override {
        return type && std::strcmp(type, platformType) == 0 ? kResultTrue : kResultFalse;
    }
    tresult PLUGIN_API attached(void* parent, FIDString type) override;
    tresult PLUGIN_API removed() override { close(); return CPluginView::removed(); }
    tresult PLUGIN_API canResize() override { return kResultTrue; }
    tresult PLUGIN_API checkSizeConstraint(ViewRect* r) override {
        if (!r) return kInvalidArgument;
        uint32_t w = uint32_t(std::max(0, r->getWidth())), h = uint32_t(std::max(0, r->getHeight()));
        dw303_editor_constrain_scaled(scale, &w, &h);
        r->right = r->left + int32(w); r->bottom = r->top + int32(h);
        return kResultTrue;
    }
    tresult PLUGIN_API onSize(ViewRect* r) override {
        if (!r) return kInvalidArgument;
        if (editor && !dw303_editor_set_size(editor, uint32_t(r->getWidth()), uint32_t(r->getHeight()))) return kResultFalse;
        return CPluginView::onSize(r);
    }
    tresult PLUGIN_API setContentScaleFactor(ScaleFactor factor) override {
#if defined(__APPLE__)
        (void)factor;  // Cocoa sizes are already logical points.
        return kResultTrue;
#else
        if (!std::isfinite(factor) || factor <= 0) return kInvalidArgument;
        const double next = std::clamp(double(factor), 1.0, 4.0);
        if (next == scale) return kResultTrue;
        uint32_t w = uint32_t(std::lround(rect.getWidth() / scale * next)), h = uint32_t(std::lround(rect.getHeight() / scale * next));
        scale = next;
        if (editor) dw303_editor_set_scale(editor, scale);
        dw303_editor_constrain_scaled(scale, &w, &h);
        ViewRect size(0, 0, int32(w), int32(h));
        if (plugFrame) plugFrame->resizeView(this, &size); else rect = size;
        return kResultTrue;
#endif
    }
    OBJ_METHODS(DW303View, CPluginView)
    DEFINE_INTERFACES
        DEF_INTERFACE(IPlugViewContentScaleSupport)
    END_DEFINE_INTERFACES(CPluginView)
    REFCOUNT_METHODS(CPluginView)
private:
    void close() {
#if defined(__linux__)
        if (runLoop && timer) runLoop->unregisterTimer(timer);
        timer = nullptr; runLoop = nullptr;
#endif
        if (editor) { dw303_editor_destroy(editor); editor = nullptr; }
    }
    IPtr<DW303Vst3> owner;
    Dw303Editor* editor = nullptr;
    double scale = 1.0;
#if defined(__linux__)
    IPtr<Linux::IRunLoop> runLoop;
    IPtr<IdleTimer> timer;
#endif
};

class DW303Vst3 final : public SingleComponentEffect {
public:
    DW303Vst3() : dsp(dw303_create()) {}
    ~DW303Vst3() override { dw303_destroy(dsp); }
    static FUnknown* create(void*) { return static_cast<IAudioProcessor*>(new DW303Vst3); }

    tresult PLUGIN_API initialize(FUnknown* context) override {
        auto result = SingleComponentEffect::initialize(context);
        if (result != kResultOk) return result;
        if (!dsp) return kResultFalse;
        addAudioOutput(STR16("Output"), SpeakerArr::kStereo);
        addEventInput(STR16("MIDI input"), 1);
        for (uint32 i = 0; i < dw303_param_count(); ++i) {
            Dw303ParamInfo info;
            if (!dw303_param_info(i, &info)) return kResultFalse;
            String title(info.name), units(info.unit);
            int32 flags = info.automatable ? ParameterInfo::kCanAutomate : 0;
            if (info.steps) flags |= ParameterInfo::kIsList;
            parameters.addParameter(title, units, int32(info.steps), dw303_param_normalize(i, info.initial), flags, info.id);
            reported[i] = dw303_param_normalize(i, info.initial);
        }
        return kResultOk;
    }
    tresult PLUGIN_API setBusArrangements(SpeakerArrangement*, int32 inputs, SpeakerArrangement* outputs, int32 count) override {
        if (inputs != 0 || count != 1 || outputs[0] != SpeakerArr::kStereo) return kResultFalse;
        return SingleComponentEffect::setBusArrangements(nullptr, 0, outputs, count);
    }
    tresult PLUGIN_API canProcessSampleSize(int32 size) override { return size == kSample32 ? kResultTrue : kResultFalse; }
    uint32 PLUGIN_API getTailSamples() override { return kInfiniteTail; }
    tresult PLUGIN_API setupProcessing(ProcessSetup& setup) override {
        if (!std::isfinite(setup.sampleRate) || setup.sampleRate < 8000 || setup.sampleRate > 384000
            || setup.maxSamplesPerBlock < 1 || setup.maxSamplesPerBlock > 65536 || setup.symbolicSampleSize != kSample32)
            return kResultFalse;
        return SingleComponentEffect::setupProcessing(setup);
    }
    tresult PLUGIN_API setActive(TBool active) override {
        if (active) {
            if (!dw303_prepare(dsp, processSetup.sampleRate, uint32_t(processSetup.maxSamplesPerBlock))) return kResultFalse;
        } else {
            dw303_deactivate(dsp);
        }
        return SingleComponentEffect::setActive(active);
    }
    tresult PLUGIN_API setProcessing(TBool state) override { if (state) dw303_reset(dsp); return kResultOk; }

    tresult PLUGIN_API process(ProcessData& data) override {
        size_t valueCount = 0, midiCount = 0;
        if (auto* changes = data.inputParameterChanges) {
            for (int32 q = 0; q < changes->getParameterCount(); ++q) {
                auto* queue = changes->getParameterData(q);
                const int32 index = queue ? indexOf(queue->getParameterId()) : -1;
                if (index < 0) continue;
                for (int32 p = 0; p < queue->getPointCount() && valueCount < maximumEvents; ++p) {
                    int32 offset = 0; ParamValue normalized = 0;
                    if (queue->getPoint(p, offset, normalized) != kResultOk || !std::isfinite(normalized)) continue;
                    values[valueCount++] = {uint32_t(std::max(0, offset)), uint32_t(index), dw303_param_denormalize(uint32_t(index), float(normalized))};
                    reported[size_t(index)] = normalized;
                }
            }
            // Queues are each ordered; merge them by frame without allocating.
            for (size_t i = 1; i < valueCount; ++i)
                for (size_t j = i; j > 0 && values[j - 1].frame > values[j].frame; --j) std::swap(values[j - 1], values[j]);
        }
        if (auto* events = data.inputEvents) {
            for (int32 i = 0; i < events->getEventCount() && midiCount < maximumEvents; ++i) {
                Event e{};
                if (events->getEvent(i, e) != kResultOk) continue;
                const uint32_t frame = uint32_t(std::max(0, e.sampleOffset));
                if (e.type == Event::kNoteOnEvent) {
                    const auto velocity = uint8_t(std::clamp(int(std::lround(e.noteOn.velocity * 127)), 1, 127));
                    midi[midiCount++] = {frame, 3, {uint8_t(0x90 | (e.noteOn.channel & 15)), uint8_t(e.noteOn.pitch & 127), velocity, 0}};
                } else if (e.type == Event::kNoteOffEvent) {
                    const auto velocity = uint8_t(std::clamp(int(std::lround(e.noteOff.velocity * 127)), 0, 127));
                    midi[midiCount++] = {frame, 3, {uint8_t(0x80 | (e.noteOff.channel & 15)), uint8_t(e.noteOff.pitch & 127), velocity, 0}};
                }
            }
        }
        Dw303Transport transport{};
        if (auto* c = data.processContext) {
            transport.flags |= DW303_TRANSPORT_PLAYING_VALID;
            if (c->state & ProcessContext::kPlaying) transport.flags |= DW303_TRANSPORT_PLAYING;
            if (c->state & ProcessContext::kTempoValid) { transport.flags |= DW303_TRANSPORT_TEMPO; transport.tempo = c->tempo; }
            if (c->state & ProcessContext::kProjectTimeMusicValid) { transport.flags |= DW303_TRANSPORT_BEATS; transport.beats = c->projectTimeMusic; }
            transport.flags |= DW303_TRANSPORT_SAMPLE; transport.sample = c->projectTimeSamples;
        }
        if (data.numSamples > 0) {
            if (data.numOutputs != 1 || data.outputs[0].numChannels != 2 || !data.outputs[0].channelBuffers32) return kResultFalse;
            auto** out = data.outputs[0].channelBuffers32;
            dw303_process(dsp, out[0], out[1], uint32_t(data.numSamples), midi.data(), midiCount, values.data(), valueCount, &transport);
            data.outputs[0].silenceFlags = 0;
        } else {
            // Parameter flush without audio: queue the values for the next block.
            for (size_t i = 0; i < valueCount; ++i) dw303_set_value(dsp, values[i].index, values[i].value);
        }
        // Panel and sequencer changes made by the engine go back to the host.
        if (auto* out = data.outputParameterChanges) {
            for (uint32 i = 0; i < parameterCount; ++i) {
                const ParamValue now = dw303_param_normalize(i, dw303_value(dsp, i));
                if (std::fabs(now - reported[i]) < 1e-6) continue;
                Dw303ParamInfo info; dw303_param_info(i, &info);
                int32 slot = 0;
                if (auto* queue = out->addParameterData(info.id, slot)) { int32 point = 0; queue->addPoint(0, now, point); }
                reported[i] = now;
            }
        }
        return kResultOk;
    }

    tresult PLUGIN_API setState(IBStream* stream) override {
        std::vector<uint8_t> bytes; uint8_t buffer[4096]; int32 count = 0;
        while (bytes.size() <= 65536) {
            if (stream->read(buffer, sizeof(buffer), &count) != kResultOk || count <= 0) break;
            bytes.insert(bytes.end(), buffer, buffer + count);
        }
        if (!dw303_state_load(dsp, bytes.data(), bytes.size())) return kResultFalse;
        for (uint32 i = 0; i < parameterCount; ++i) {
            Dw303ParamInfo info; dw303_param_info(i, &info);
            EditController::setParamNormalized(info.id, dw303_param_normalize(i, dw303_value(dsp, i)));
        }
        return kResultOk;
    }
    tresult PLUGIN_API getState(IBStream* stream) override {
        size_t size = 0; auto* bytes = dw303_state_save(dsp, &size);
        if (!bytes) return kResultFalse;
        int32 written = 0; auto result = stream->write(bytes, int32(size), &written);
        dw303_bytes_free(bytes, size);
        return result == kResultOk && size_t(written) == size ? kResultOk : kResultFalse;
    }

    ParamValue PLUGIN_API getParamNormalized(ParamID id) override {
        const int32 index = indexOf(id);
        return index < 0 ? SingleComponentEffect::getParamNormalized(id) : dw303_param_normalize(uint32_t(index), dw303_value(dsp, uint32_t(index)));
    }
    // Hosts deliver values to process() only while processing; queue them as
    // well so a stopped host saves and displays the edit at once.
    tresult PLUGIN_API setParamNormalized(ParamID id, ParamValue value) override {
        auto result = SingleComponentEffect::setParamNormalized(id, value);
        const int32 index = indexOf(id);
        if (result == kResultOk && index >= 0 && std::isfinite(value)) dw303_set_value(dsp, uint32_t(index), dw303_param_denormalize(uint32_t(index), float(value)));
        return result;
    }
    tresult PLUGIN_API getParamStringByValue(ParamID id, ParamValue normalized, String128 text) override {
        const int32 index = indexOf(id); char buffer[64];
        if (index < 0 || !dw303_param_text(uint32_t(index), dw303_param_denormalize(uint32_t(index), float(normalized)), buffer, sizeof(buffer)))
            return SingleComponentEffect::getParamStringByValue(id, normalized, text);
        String(buffer).copyTo(text, 0, 127);
        return kResultOk;
    }
    tresult PLUGIN_API getParamValueByString(ParamID id, TChar* text, ParamValue& normalized) override {
        const int32 index = indexOf(id); String value(text); value.toMultiByte(kCP_Utf8); float physical = 0;
        if (index < 0 || !dw303_param_parse(uint32_t(index), value.text8(), &physical)) return kResultFalse;
        normalized = dw303_param_normalize(uint32_t(index), physical);
        return kResultOk;
    }
    IPlugView* PLUGIN_API createView(FIDString name) override {
        return name && std::strcmp(name, ViewType::kEditor) == 0 ? new DW303View(this) : nullptr;
    }

    Dw303Editor* openEditor() {
        Dw303EditorHost host{this,
            [](void* c, uint32_t i) { static_cast<DW303Vst3*>(c)->gesture(i, 0, 0); },
            [](void* c, uint32_t i, float v) { static_cast<DW303Vst3*>(c)->gesture(i, 1, v); },
            [](void* c, uint32_t i) { static_cast<DW303Vst3*>(c)->gesture(i, 2, 0); }};
        return dw303_editor_create(dsp, host);
    }
private:
    void gesture(uint32_t index, int kind, float value) {
        Dw303ParamInfo info; if (!dw303_param_info(index, &info)) return;
        if (kind == 0) beginEdit(info.id);
        else if (kind == 2) endEdit(info.id);
        else {
            const ParamValue normalized = dw303_param_normalize(index, value);
            EditController::setParamNormalized(info.id, normalized);
            performEdit(info.id, normalized);
        }
    }
    Dw303* dsp;
    std::array<Dw303Param, maximumEvents> values{};
    std::array<Dw303Midi, maximumEvents> midi{};
    std::array<ParamValue, parameterCount> reported{};
};

DW303View::DW303View(DW303Vst3* controller) : owner(controller) {
    uint32_t w = 0, h = 0; dw303_editor_default_size(&w, &h);
    rect = ViewRect(0, 0, int32(w), int32(h));
}
tresult DW303View::attached(void* parent, FIDString type) {
    if (!parent || editor || isPlatformTypeSupported(type) != kResultTrue) return kResultFalse;
#if defined(__linux__)
    // Without a host run loop GTK never runs; decline so the host falls back.
    FUnknownPtr<Linux::IRunLoop> loop(plugFrame);
    if (!loop) return kResultFalse;
    runLoop = loop;
    timer = owned(new IdleTimer);
    if (runLoop->registerTimer(timer, 10) != kResultOk) { close(); return kResultFalse; }
#endif
    editor = owner->openEditor();
    if (!editor || !dw303_editor_set_scale(editor, scale) || !dw303_editor_attach(editor, parent)) { close(); return kResultFalse; }
    dw303_editor_set_size(editor, uint32_t(rect.getWidth()), uint32_t(rect.getHeight()));
    return CPluginView::attached(parent, type);
}

bool InitModule() { return true; }
bool DeinitModule() { return true; }
BEGIN_FACTORY_DEF(DW303_COMPANY, "https://asherswissa.com", "")
DEF_CLASS2(INLINE_UID_FROM_FUID(componentID), PClassInfo::kManyInstances, kVstAudioEffectClass, DW303_NAME,
           0, "Instrument|Synth", DW303_VERSION, kVstVersionString, DW303Vst3::create)
END_FACTORY
