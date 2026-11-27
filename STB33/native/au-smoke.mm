// In-process AU check without installing: register the built component,
// render a MIDI note, open the Cocoa editor and drive one knob and the
// RUN/STOP button through the page's own message path.
#import <Cocoa/Cocoa.h>
#import <WebKit/WebKit.h>
#import <AudioUnit/AUCocoaUIView.h>
#include <AudioToolbox/AudioToolbox.h>
#include <dlfcn.h>
#include "identity.h"
#include <array>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <string>

static void require(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
static void pump(double seconds) {
    NSDate* end = [NSDate dateWithTimeIntervalSinceNow:seconds];
    while (end.timeIntervalSinceNow > 0) [[NSRunLoop currentRunLoop] runMode:NSDefaultRunLoopMode beforeDate:end];
}
static WKWebView* findWeb(NSView* view) {
    if ([view isKindOfClass:WKWebView.class]) return static_cast<WKWebView*>(view);
    for (NSView* child in view.subviews) if (auto* web = findWeb(child)) return web;
    return nil;
}
static bool truthy(WKWebView* web, NSString* script) {
    __block bool done = false, value = false;
    [web evaluateJavaScript:script completionHandler:^(id result, NSError* error) { value = !error && [result boolValue]; done = true; }];
    for (int i = 0; i < 100 && !done; ++i) pump(.02);
    return value;
}
static float render(AudioUnit unit, int blocks, double& time) {
    std::array<float, 256> left{}, right{};
    struct { UInt32 count; AudioBuffer buffers[2]; } output{2, {{1, sizeof(left), left.data()}, {1, sizeof(right), right.data()}}};
    float peak = 0;
    for (int b = 0; b < blocks; ++b) {
        AudioTimeStamp stamp{}; stamp.mFlags = kAudioTimeStampSampleTimeValid; stamp.mSampleTime = time; time += 256;
        AudioUnitRenderActionFlags flags = 0;
        require(AudioUnitRender(unit, &flags, &stamp, 0, 256, reinterpret_cast<AudioBufferList*>(&output)) == noErr, "render");
        for (size_t i = 0; i < 256; ++i) { require(std::isfinite(left[i]) && left[i] == right[i], "finite mono-compatible output"); peak = std::max(peak, std::fabs(left[i])); }
    }
    return peak;
}

int main(int argc, char** argv) {
    @autoreleasepool { try {
        require(argc == 2, "usage: dw303-au-smoke <DW-303.component>");
        [NSApplication sharedApplication];
        const auto path = std::string(argv[1]) + "/Contents/MacOS/" DW303_NAME;
        void* binary = dlopen(path.c_str(), RTLD_LOCAL | RTLD_NOW); require(binary, "load AU binary");
        auto factory = reinterpret_cast<AudioComponentFactoryFunction>(dlsym(binary, "DW303AUFactory")); require(factory, "AU factory");
        AudioComponentDescription description{'aumu', DW303_PRODUCT_CODE, DW303_MANUFACTURER_CODE, 0, 0};
        auto component = AudioComponentRegister(&description, CFSTR(DW303_NAME), DW303_VERSION_INT, factory);
        AudioUnit unit = nullptr; require(AudioComponentInstanceNew(component, &unit) == noErr, "create");
        AudioStreamBasicDescription format{48000, kAudioFormatLinearPCM, kAudioFormatFlagsNativeFloatPacked | kAudioFormatFlagIsNonInterleaved, 4, 1, 4, 2, 32, 0};
        require(AudioUnitSetProperty(unit, kAudioUnitProperty_StreamFormat, kAudioUnitScope_Output, 0, &format, sizeof(format)) == noErr, "stereo output");
        require(AudioUnitInitialize(unit) == noErr, "initialize");
        double time = 0;
        require(render(unit, 8, time) == 0, "silent before a note");
        require(MusicDeviceMIDIEvent(unit, 0x90, 48, 110, 0) == noErr, "note on");
        const float peak = render(unit, 40, time);
        require(peak > 1e-3f, "note renders audio");
        require(MusicDeviceMIDIEvent(unit, 0x80, 48, 0, 0) == noErr, "note off");
        render(unit, 8, time);

        AudioUnitCocoaViewInfo info{}; UInt32 bytes = sizeof(info);
        require(AudioUnitGetProperty(unit, kAudioUnitProperty_CocoaUI, kAudioUnitScope_Global, 0, &info, &bytes) == noErr, "Cocoa view property");
        id<AUCocoaUIBase> cocoa = [[NSClassFromString((__bridge NSString*)info.mCocoaAUViewClass[0]) alloc] init];
        CFRelease(info.mCocoaAUViewClass[0]); CFRelease(info.mCocoaAUViewBundleLocation);
        NSView* view = [cocoa uiViewForAudioUnit:unit withSize:NSZeroSize]; require(view, "editor view");
        NSWindow* window = [[NSWindow alloc] initWithContentRect:view.frame styleMask:NSWindowStyleMaskTitled backing:NSBackingStoreBuffered defer:NO];
        window.contentView = view;
        WKWebView* web = findWeb(view); require(web, "web editor");
        bool ready = false;
        for (int i = 0; i < 100 && !ready; ++i) { ready = truthy(web, @"typeof post==='function'&&!!document.getElementById('param-2')"); pump(.02); }
        require(ready, "editor page ready");
        require(truthy(web, @"post({type:'edit',id:2,value:0.9})"), "post cutoff edit");
        float cutoff = 0; bool reached = false;
        for (int i = 0; i < 100 && !reached; ++i) {
            render(unit, 1, time); pump(.01);
            reached = AudioUnitGetParameter(unit, 2, kAudioUnitScope_Global, 0, &cutoff) == noErr && std::fabs(cutoff - 0.9f) < 1e-6f;
        }
        require(reached, "editor knob reached the AU parameter");
        // Internal sync, then RUN/STOP from the panel starts the sequencer.
        require(AudioUnitSetParameter(unit, 10, kAudioUnitScope_Global, 0, 0, 0) == noErr, "internal sync");
        render(unit, 2, time);
        require(truthy(web, @"window.panel({action:'button',key:'run-stop',edge:'press'})") && truthy(web, @"window.panel({action:'button',key:'run-stop',edge:'release'})"), "post run/stop");
        float run = 0; reached = false;
        for (int i = 0; i < 100 && !reached; ++i) {
            render(unit, 1, time); pump(.01);
            reached = AudioUnitGetParameter(unit, 12, kAudioUnitScope_Global, 0, &run) == noErr && run == 1;
        }
        require(reached, "panel RUN/STOP reached the Run parameter");
        require(render(unit, 200, time) > 1e-3f, "sequencer plays the factory pattern");

        // State round trip carries parameters and pattern memory.
        CFPropertyListRef state = nullptr; UInt32 size = sizeof(state);
        require(AudioUnitGetProperty(unit, kAudioUnitProperty_ClassInfo, kAudioUnitScope_Global, 0, &state, &size) == noErr && state, "save state");
        require(AudioUnitSetParameter(unit, 2, kAudioUnitScope_Global, 0, 0.1f, 0) == noErr, "change cutoff");
        require(AudioUnitSetProperty(unit, kAudioUnitProperty_ClassInfo, kAudioUnitScope_Global, 0, &state, sizeof(state)) == noErr, "restore state");
        CFRelease(state);
        require(AudioUnitGetParameter(unit, 2, kAudioUnitScope_Global, 0, &cutoff) == noErr && std::fabs(cutoff - 0.9f) < 1e-6f, "restored cutoff");

        window = nil; view = nil; cocoa = nil; pump(.1);
        require(AudioUnitUninitialize(unit) == noErr && AudioComponentInstanceDispose(unit) == noErr, "dispose");
        std::cout << "AU: note peak " << peak << "; editor knob and RUN/STOP reached the unit; sequencer played; state restored\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << "FAILED: " << error.what() << '\n'; return 1; } }
}
