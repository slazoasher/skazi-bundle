// AUv2 music device. AU parameters are physical values in the host's own
// parameter store; render forwards host changes to the engine and copies
// engine-originated panel/sequencer changes back, notified from a main-thread
// timer. MIDI (including clock) is collected with sample offsets.
#include "dw303.h"
#include "identity.h"
#include <AudioUnitSDK/MusicDeviceBase.h>
#include <AudioUnitSDK/AUPlugInDispatch.h>
#import <Cocoa/Cocoa.h>
#import <AudioUnit/AUCocoaUIView.h>
#include <AudioToolbox/AudioUnitUtilities.h>
#include <array>
#include <atomic>
#include <cmath>
#include <cstring>
#include <memory>

using namespace ausdk;
constexpr AudioUnitPropertyID editorContextProperty = 'D303';
constexpr size_t parameterCount = 12;
constexpr UInt32 maximumMidi = 1024;
static CFStringRef stateKey = CFSTR("dw303State");

static AudioUnitParameterID parameterID(size_t index) {
    Dw303ParamInfo info; dw303_param_info(uint32_t(index), &info); return info.id;
}
static int indexOf(AudioUnitParameterID id) {
    for (size_t i = 0; i < parameterCount; ++i) if (parameterID(i) == id) return int(i);
    return -1;
}

// Engine-originated value changes, announced to listeners on the main thread.
struct Notifier {
    AudioComponentInstance unit;
    std::array<std::atomic<bool>, parameterCount> pending{};
    void announce() {
        for (size_t i = 0; i < parameterCount; ++i) if (pending[i].exchange(false)) {
            AudioUnitEvent event{};
            event.mEventType = kAudioUnitEvent_ParameterValueChange;
            event.mArgument.mParameter = {unit, parameterID(i), kAudioUnitScope_Global, 0};
            AUEventListenerNotify(nullptr, nullptr, &event);
        }
    }
};

class DW303AU final : public MusicDeviceBase {
public:
    explicit DW303AU(AudioComponentInstance instance) : MusicDeviceBase(instance, 0, 1), dsp(dw303_create()) {
        CreateElements();
        for (size_t i = 0; i < parameterCount; ++i) {
            Dw303ParamInfo info; dw303_param_info(uint32_t(i), &info);
            Globals()->SetParameter(info.id, info.initial);
            consumed[i] = info.initial;
        }
        notifier->unit = instance;
        auto shared = notifier;
        timer = CFRunLoopTimerCreateWithHandler(nullptr, CFAbsoluteTimeGetCurrent(), 1.0 / 30, 0, 0, ^(CFRunLoopTimerRef) { shared->announce(); });
        CFRunLoopAddTimer(CFRunLoopGetMain(), timer, kCFRunLoopCommonModes);
    }
    ~DW303AU() override { CFRunLoopTimerInvalidate(timer); CFRelease(timer); life.reset(); dw303_destroy(dsp); }

    OSStatus Initialize() override {
        auto result = MusicDeviceBase::Initialize();
        if (result != noErr) return result;
        if (Output(0).GetStreamFormat().mChannelsPerFrame != 2) return kAudioUnitErr_FormatNotSupported;
        return dw303_prepare(dsp, Output(0).GetStreamFormat().mSampleRate, GetMaxFramesPerSlice()) ? OSStatus(noErr) : OSStatus(kAudioUnitErr_FormatNotSupported);
    }
    void Cleanup() override { dw303_deactivate(dsp); MusicDeviceBase::Cleanup(); }
    OSStatus Reset(AudioUnitScope, AudioUnitElement) override { dw303_reset(dsp); midiCount = 0; return noErr; }
    bool StreamFormatWritable(AudioUnitScope, AudioUnitElement) override { return IsInitialized() ? false : true; }
    bool CanScheduleParameters() const override { return false; }
    bool SupportsTail() override { return true; }
    Float64 GetTailTime() override { return 0; }
    UInt32 SupportedNumChannels(const AUChannelInfo** out) override {
        static const AUChannelInfo channels[] = {{0, 2}}; if (out) *out = channels; return 1;
    }

    OSStatus MIDIEvent(UInt32 status, UInt32 data1, UInt32 data2, UInt32 offset) override {
        if (midiCount >= maximumMidi) return noErr;
        const uint8_t byte = uint8_t(status);
        // System real-time (clock, start, continue, stop) are single bytes.
        const uint32_t size = byte >= 0xF8 ? 1 : (byte & 0xE0) == 0xC0 ? 2 : 3;
        midi[midiCount++] = {offset, size, {byte, uint8_t(data1 & 127), uint8_t(data2 & 127), 0}};
        return noErr;
    }

    OSStatus GetParameterList(AudioUnitScope scope, AudioUnitParameterID* out, UInt32& count) override {
        if (scope != kAudioUnitScope_Global) { count = 0; return noErr; }
        count = UInt32(parameterCount);
        if (out) for (size_t i = 0; i < parameterCount; ++i) out[i] = parameterID(i);
        return noErr;
    }
    OSStatus GetParameterInfo(AudioUnitScope scope, AudioUnitParameterID id, AudioUnitParameterInfo& info) override {
        const int index = indexOf(id);
        if (scope != kAudioUnitScope_Global || index < 0) return kAudioUnitErr_InvalidParameter;
        Dw303ParamInfo p; dw303_param_info(uint32_t(index), &p);
        info = {};
        info.minValue = p.minimum; info.maxValue = p.maximum; info.defaultValue = p.initial;
        info.unit = p.steps ? kAudioUnitParameterUnit_Indexed : std::strcmp(p.unit, "BPM") == 0 ? kAudioUnitParameterUnit_BPM : kAudioUnitParameterUnit_Generic;
        info.flags = kAudioUnitParameterFlag_IsReadable | kAudioUnitParameterFlag_IsWritable | kAudioUnitParameterFlag_HasCFNameString
            | kAudioUnitParameterFlag_ValuesHaveStrings | kAudioUnitParameterFlag_IsHighResolution;
        if (!p.automatable) info.flags |= kAudioUnitParameterFlag_NonRealTime;
        FillInParameterName(info, CFStringCreateWithCString(nullptr, p.name, kCFStringEncodingUTF8), true);
        return noErr;
    }
    OSStatus GetParameterValueStrings(AudioUnitScope scope, AudioUnitParameterID id, CFArrayRef* strings) override {
        const int index = indexOf(id);
        Dw303ParamInfo p;
        if (scope != kAudioUnitScope_Global || index < 0 || !dw303_param_info(uint32_t(index), &p) || !p.steps) return kAudioUnitErr_InvalidProperty;
        if (strings) {
            auto array = CFArrayCreateMutable(nullptr, 0, &kCFTypeArrayCallBacks);
            for (uint32_t step = 0; step <= p.steps; ++step) {
                char text[64]; dw303_param_text(uint32_t(index), p.minimum + float(step), text, sizeof(text));
                auto string = CFStringCreateWithCString(nullptr, text, kCFStringEncodingUTF8);
                CFArrayAppendValue(array, string); CFRelease(string);
            }
            *strings = array;
        }
        return noErr;
    }

    OSStatus Render(AudioUnitRenderActionFlags& flags, const AudioTimeStamp&, UInt32 frames) override {
        auto& buffers = Output(0).GetBufferList();
        if (buffers.mNumberBuffers != 2 || buffers.mBuffers[0].mDataByteSize < frames * sizeof(float)
            || buffers.mBuffers[1].mDataByteSize < frames * sizeof(float)) { midiCount = 0; return kAudioUnitErr_FormatNotSupported; }
        std::array<Dw303Param, parameterCount> values; size_t valueCount = 0;
        for (size_t i = 0; i < parameterCount; ++i) {
            const float host = Globals()->GetParameter(parameterID(i));
            if (host != consumed[i]) { values[valueCount++] = {0, uint32_t(i), host}; consumed[i] = host; }
        }
        Dw303Transport transport{};
        Float64 beat = 0, tempo = 0;
        if (CallHostBeatAndTempo(&beat, &tempo) == noErr) {
            transport.flags |= DW303_TRANSPORT_TEMPO | DW303_TRANSPORT_BEATS; transport.tempo = tempo; transport.beats = beat;
        }
        Boolean playing = false, changed = false, looping = false; Float64 sample = 0, loopStart = 0, loopEnd = 0;
        if (CallHostTransportState(&playing, &changed, &sample, &looping, &loopStart, &loopEnd) == noErr) {
            transport.flags |= DW303_TRANSPORT_PLAYING_VALID | DW303_TRANSPORT_SAMPLE | (playing ? DW303_TRANSPORT_PLAYING : 0);
            transport.sample = int64_t(sample);
        }
        // MIDIEvent offsets arrive in call order; keep them ordered by frame.
        for (UInt32 i = 1; i < midiCount; ++i)
            for (UInt32 j = i; j > 0 && midi[j - 1].frame > midi[j].frame; --j) std::swap(midi[j - 1], midi[j]);
        dw303_process(dsp, static_cast<float*>(buffers.mBuffers[0].mData), static_cast<float*>(buffers.mBuffers[1].mData), frames,
                      midi.data(), midiCount, values.data(), valueCount, &transport);
        midiCount = 0;
        for (size_t i = 0; i < parameterCount; ++i) {
            const float now = dw303_value(dsp, uint32_t(i));
            if (now != consumed[i]) { Globals()->SetParameter(parameterID(i), now); consumed[i] = now; notifier->pending[i].store(true); }
        }
        flags &= ~kAudioUnitRenderAction_OutputIsSilence;
        return noErr;
    }

    OSStatus SaveState(CFPropertyListRef* result) override {
        auto status = MusicDeviceBase::SaveState(result);
        if (status != noErr) return status;
        size_t size = 0; auto* bytes = dw303_state_save(dsp, &size);
        if (!bytes) return kAudioUnitErr_InvalidPropertyValue;
        auto data = CFDataCreate(nullptr, bytes, CFIndex(size)); dw303_bytes_free(bytes, size);
        CFDictionarySetValue(static_cast<CFMutableDictionaryRef>(const_cast<void*>(*result)), stateKey, data);
        CFRelease(data);
        return noErr;
    }
    OSStatus RestoreState(CFPropertyListRef state) override {
        if (!state || CFGetTypeID(state) != CFDictionaryGetTypeID()) return kAudioUnitErr_InvalidPropertyValue;
        auto data = static_cast<CFDataRef>(CFDictionaryGetValue(static_cast<CFDictionaryRef>(state), stateKey));
        if (data && (CFGetTypeID(data) != CFDataGetTypeID() || !dw303_state_load(dsp, CFDataGetBytePtr(data), size_t(CFDataGetLength(data)))))
            return kAudioUnitErr_InvalidPropertyValue;
        auto status = MusicDeviceBase::RestoreState(state);
        if (status != noErr) return status;
        // The record is authoritative; match the host store to it.
        if (data) for (size_t i = 0; i < parameterCount; ++i) {
            const float value = dw303_value(dsp, uint32_t(i));
            Globals()->SetParameter(parameterID(i), value); consumed[i] = value; notifier->pending[i].store(true);
        }
        return noErr;
    }

    OSStatus GetPropertyInfo(AudioUnitPropertyID id, AudioUnitScope scope, AudioUnitElement element, UInt32& size, bool& writable) override {
        if (scope == kAudioUnitScope_Global) {
            if (id == kAudioUnitProperty_CocoaUI) { size = sizeof(AudioUnitCocoaViewInfo); writable = false; return noErr; }
            if (id == editorContextProperty) { size = sizeof(DW303AU*); writable = false; return noErr; }
            if (id == kAudioUnitProperty_ParameterStringFromValue) { size = sizeof(AudioUnitParameterStringFromValue); writable = false; return noErr; }
            if (id == kAudioUnitProperty_ParameterValueFromString) { size = sizeof(AudioUnitParameterValueFromString); writable = false; return noErr; }
        }
        return MusicDeviceBase::GetPropertyInfo(id, scope, element, size, writable);
    }
    OSStatus GetProperty(AudioUnitPropertyID id, AudioUnitScope scope, AudioUnitElement element, void* data) override;

    // Main-thread editor gesture, published like a host edit.
    void gesture(uint32_t index, int kind, float value) {
        AudioUnitEvent event{};
        event.mArgument.mParameter = {GetComponentInstance(), parameterID(index), kAudioUnitScope_Global, 0};
        event.mEventType = kind == 0 ? kAudioUnitEvent_BeginParameterChangeGesture
                         : kind == 2 ? kAudioUnitEvent_EndParameterChangeGesture : kAudioUnitEvent_ParameterValueChange;
        if (kind == 1) Globals()->SetParameter(parameterID(index), value);
        AUEventListenerNotify(nullptr, nullptr, &event);
    }
    Dw303* engine() const { return dsp; }
    std::shared_ptr<int> life = std::make_shared<int>(0);
private:
    Dw303* dsp;
    std::shared_ptr<Notifier> notifier = std::make_shared<Notifier>();
    CFRunLoopTimerRef timer = nullptr;
    std::array<float, parameterCount> consumed{};
    std::array<Dw303Midi, maximumMidi> midi{};
    UInt32 midiCount = 0;
};

// Editor gestures reach the unit only while it is alive.
struct DW303AUContext { DW303AU* unit; std::weak_ptr<int> life; };
@interface DW303AUContainer : NSView { @public Dw303Editor* editor; std::unique_ptr<DW303AUContext> context; }
@end
@implementation DW303AUContainer
- (void)dealloc { if (editor) dw303_editor_destroy(editor); }
- (void)setFrameSize:(NSSize)size {
    uint32_t w = uint32_t(std::max(0.0, size.width)), h = uint32_t(std::max(0.0, size.height));
    dw303_editor_constrain(&w, &h);
    [super setFrameSize:NSMakeSize(w, h)];
    if (editor) dw303_editor_set_size(editor, w, h);
}
@end
@interface DW303AUViewFactory : NSObject <AUCocoaUIBase>
@end
@implementation DW303AUViewFactory
- (unsigned)interfaceVersion { return 0; }
- (NSString*)description { return @DW303_NAME; }
- (NSView*)uiViewForAudioUnit:(AudioUnit)unit withSize:(NSSize)size {
    (void)size; DW303AU* instance = nullptr; UInt32 bytes = sizeof(instance);
    if (AudioUnitGetProperty(unit, editorContextProperty, kAudioUnitScope_Global, 0, &instance, &bytes) != noErr || !instance) return nil;
    uint32_t w = 0, h = 0; dw303_editor_default_size(&w, &h);
    auto view = [[DW303AUContainer alloc] initWithFrame:NSMakeRect(0, 0, w, h)];
    view->context.reset(new DW303AUContext{instance, instance->life});
    using Context = DW303AUContext;
    Dw303EditorHost host{view->context.get(),
        [](void* c, uint32_t i) { auto* x = static_cast<Context*>(c); if (!x->life.expired()) x->unit->gesture(i, 0, 0); },
        [](void* c, uint32_t i, float v) { auto* x = static_cast<Context*>(c); if (!x->life.expired()) x->unit->gesture(i, 1, v); },
        [](void* c, uint32_t i) { auto* x = static_cast<Context*>(c); if (!x->life.expired()) x->unit->gesture(i, 2, 0); }};
    view->editor = dw303_editor_create(instance->engine(), host);
    if (!view->editor || !dw303_editor_attach(view->editor, (__bridge void*)view)) return nil;
    return view;
}
@end

OSStatus DW303AU::GetProperty(AudioUnitPropertyID id, AudioUnitScope scope, AudioUnitElement element, void* data) {
    if (scope == kAudioUnitScope_Global) {
        if (id == editorContextProperty) { *static_cast<DW303AU**>(data) = this; return noErr; }
        if (id == kAudioUnitProperty_CocoaUI) {
            auto* info = static_cast<AudioUnitCocoaViewInfo*>(data);
            info->mCocoaAUViewClass[0] = static_cast<CFStringRef>(CFBridgingRetain(NSStringFromClass(DW303AUViewFactory.class)));
            info->mCocoaAUViewBundleLocation = static_cast<CFURLRef>(CFBridgingRetain([NSURL fileURLWithPath:[NSBundle bundleForClass:DW303AUViewFactory.class].bundlePath]));
            return noErr;
        }
        if (id == kAudioUnitProperty_ParameterStringFromValue) {
            auto* request = static_cast<AudioUnitParameterStringFromValue*>(data);
            const int index = indexOf(request->inParamID);
            if (index < 0) return kAudioUnitErr_InvalidParameter;
            const float value = request->inValue ? *request->inValue : Globals()->GetParameter(request->inParamID);
            char text[64]; if (!dw303_param_text(uint32_t(index), value, text, sizeof(text))) return kAudioUnitErr_InvalidPropertyValue;
            request->outString = CFStringCreateWithCString(nullptr, text, kCFStringEncodingUTF8);
            return noErr;
        }
        if (id == kAudioUnitProperty_ParameterValueFromString) {
            auto* request = static_cast<AudioUnitParameterValueFromString*>(data);
            const int index = indexOf(request->inParamID); char text[256]; float value = 0;
            if (index < 0 || !request->inString || !CFStringGetCString(request->inString, text, sizeof(text), kCFStringEncodingUTF8)
                || !dw303_param_parse(uint32_t(index), text, &value)) return kAudioUnitErr_InvalidPropertyValue;
            request->outValue = value;
            return noErr;
        }
    }
    return MusicDeviceBase::GetProperty(id, scope, element, data);
}

extern "C" __attribute__((visibility("default"))) void* DW303AUFactory(const AudioComponentDescription* description) {
    return AUMusicDeviceFactory<DW303AU>::Factory(description);
}
