#include "PluginEditor.h"
#include "BinaryData.h"

using namespace skatom;

namespace
{
juce::WebBrowserComponent::Options webOptions (SkatomEditor* ed,
                                               std::function<void()> onReady,
                                               std::function<void (const juce::var&)> onSet,
                                               std::function<void (const juce::var&)> onSnaps,
                                               std::function<void()> onDrop,
                                               std::function<void (const juce::String&, const juce::var&)> onMisc,
                                               juce::WebBrowserComponent::ResourceProvider provider)
{
    juce::ignoreUnused (ed);
    using O = juce::WebBrowserComponent::Options;
    return O {}
        .withBackend (O::Backend::webview2)
        .withWinWebView2Options (O::WinWebView2 {}
                                     .withUserDataFolder (juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("SKATOM-WebView2"))
                                     .withBackgroundColour (juce::Colour (0xff060708)))
        .withNativeIntegrationEnabled()
        .withKeepPageLoadedWhenBrowserIsHidden() // minimising the window must not blank and reload the screen
        .withResourceProvider (std::move (provider))
        .withEventListener ("ready", [onReady] (const juce::var&) { onReady(); })
        .withEventListener ("set", [onSet] (const juce::var& v) { onSet (v); })
        .withEventListener ("snaps", [onSnaps] (const juce::var& v) { onSnaps (v); })
        .withEventListener ("drop", [onDrop] (const juce::var&) { onDrop(); })
        .withEventListener ("gesture", [onMisc] (const juce::var& v) { onMisc ("gesture", v); })
        .withEventListener ("savePreset", [onMisc] (const juce::var& v) { onMisc ("savePreset", v); })
        .withEventListener ("deletePreset", [onMisc] (const juce::var& v) { onMisc ("deletePreset", v); });
}

const char* mimeFor (const juce::String& name)
{
    if (name.endsWith (".html")) return "text/html";
    if (name.endsWith (".css")) return "text/css";
    if (name.endsWith (".js")) return "text/javascript";
    if (name.endsWith (".png")) return "image/png";
    return "application/octet-stream";
}
}

SkatomEditor::SkatomEditor (SkatomProcessor& p)
    : AudioProcessorEditor (p), proc (p),
      web (webOptions (this,
                       [this] { sendInit(); },
                       [this] (const juce::var& v) { applyFromUi (v); },
                       [this] (const juce::var& v) { proc.setSnaps (v["snaps"].toString()); },
                       [this] { proc.dropRequest = true; },
                       [this] (const juce::String& type, const juce::var& v) { onMisc (type, v); },
                       [this] (const juce::String& url) { return getResource (url); }))
{
    addAndMakeVisible (web);
    web.goToURL (juce::WebBrowserComponent::getResourceProviderRoot());
    setResizable (true, true);
    // same aspect as the web chassis (1400 x 1065) so the UI fills the window with no bars
    setResizeLimits (700, 533, 2100, 1598);
    if (auto* c = getConstrainer()) c->setFixedAspectRatio (1400.0 / 1065.0);
    setSize (1120, 852);
    startTimerHz (30);
}

std::optional<juce::WebBrowserComponent::Resource> SkatomEditor::getResource (const juce::String& url)
{
    auto name = url.fromLastOccurrenceOf ("/", false, false).upToFirstOccurrenceOf ("?", false, false);
    if (name.isEmpty()) name = "index.html";
    for (int i = 0; i < BinaryData::namedResourceListSize; ++i)
    {
        if (name != BinaryData::originalFilenames[i]) continue;
        int size = 0;
        auto* data = BinaryData::getNamedResource (BinaryData::namedResourceList[i], size);
        std::vector<std::byte> bytes ((size_t) size);
        std::memcpy (bytes.data(), data, (size_t) size);
        return juce::WebBrowserComponent::Resource { std::move (bytes), mimeFor (name) };
    }
    return std::nullopt;
}

void SkatomEditor::sendInit()
{
    auto* o = new juce::DynamicObject();
    auto* params = new juce::DynamicObject();
    juce::Array<juce::var> defList;
    for (int i = 0; i < NUM_PARAMS; ++i)
    {
        const auto& d = defs()[(size_t) i];
        lastSent[i] = proc.apvts.getRawParameterValue (d.id)->load();
        params->setProperty (d.id, lastSent[i]);
        defList.add (juce::Array<juce::var> { d.id, d.min, d.max });
    }
    o->setProperty ("snaps", proc.getSnaps());
    o->setProperty ("params", params);
    o->setProperty ("defs", defList);
    o->setProperty ("userPresets", userPresets());
    web.emitEventIfBrowserIsVisible ("init", juce::var (o));
    ready = true;
}

void SkatomEditor::applyFromUi (const juce::var& v)
{
    if (auto* obj = v.getDynamicObject())
        for (auto& prop : obj->getProperties())
            if (auto* param = proc.apvts.getParameter (prop.name.toString()))
            {
                // during a knob/pad drag the gesture is already open (one undo step per drag, not per frame)
                const bool held = gestures.contains (prop.name.toString());
                if (! held) param->beginChangeGesture();
                param->setValueNotifyingHost (param->convertTo0to1 ((float) prop.value));
                if (! held) param->endChangeGesture();
                for (int i = 0; i < NUM_PARAMS; ++i) // the UI already shows it — don't echo it back
                    if (prop.name.toString() == defs()[(size_t) i].id) lastSent[i] = proc.apvts.getRawParameterValue (defs()[(size_t) i].id)->load();
            }
}

juce::File SkatomEditor::presetDir()
{
    return juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile ("SKATOM").getChildFile ("Presets");
}

juce::var SkatomEditor::userPresets()
{
    juce::Array<juce::var> list;
    for (auto& f : presetDir().findChildFiles (juce::File::findFiles, false, "*.skatom"))
    {
        auto* o = new juce::DynamicObject();
        o->setProperty ("name", f.getFileNameWithoutExtension());
        o->setProperty ("json", f.loadFileAsString());
        list.add (juce::var (o));
    }
    return list;
}

void SkatomEditor::onMisc (const juce::String& type, const juce::var& v)
{
    if (type == "gesture")
    {
        const auto id = v["id"].toString();
        auto* param = proc.apvts.getParameter (id);
        if (param == nullptr) return;
        if ((bool) v["on"] && ! gestures.contains (id)) { gestures.add (id); param->beginChangeGesture(); }
        else if (! (bool) v["on"] && gestures.contains (id)) { gestures.removeString (id); param->endChangeGesture(); }
        return;
    }
    const auto name = juce::File::createLegalFileName (v["name"].toString().trim()).substring (0, 64);
    if (name.isEmpty()) return;
    auto f = presetDir().getChildFile (name + ".skatom");
    if (type == "savePreset")
    {
        presetDir().createDirectory();
        f.replaceWithText (v["json"].toString());
    }
    else if (type == "deletePreset") f.deleteFile();
    auto* o = new juce::DynamicObject();
    o->setProperty ("list", userPresets());
    web.emitEventIfBrowserIsVisible ("userPresets", juce::var (o));
}

juce::var SkatomEditor::bands()
{
    // same view as Web Audio's AnalyserNode (2048 FFT, smoothing 0.7, -100..-30 dB), folded into 40 log bands
    constexpr int N = 2048;
    const int w = proc.scopeWrite.load();
    for (int i = 0; i < N; ++i)
    {
        const float hann = 0.5f - 0.5f * std::cos (juce::MathConstants<float>::twoPi * i / N);
        fftBuf[(size_t) i] = proc.scope[(w - N + i + SkatomProcessor::scopeSize) % SkatomProcessor::scopeSize] * hann;
    }
    std::fill (fftBuf.begin() + N, fftBuf.end(), 0.0f);
    fft.performFrequencyOnlyForwardTransform (fftBuf.data());
    for (int k = 0; k < N / 2; ++k) smooth[(size_t) k] = 0.7f * smooth[(size_t) k] + 0.3f * fftBuf[(size_t) k] / N;
    juce::Array<juce::var> out;
    const double hz = proc.getSr() / N;
    for (int b = 0; b < 40; ++b)
    {
        const double f0 = 30 * std::pow (16000.0 / 30, b / 40.0), f1 = 30 * std::pow (16000.0 / 30, (b + 1) / 40.0);
        float v = 0;
        for (int k = (int) (f0 / hz); k <= (int) std::ceil (f1 / hz) && k < N / 2; ++k)
            v = std::max (v, juce::jlimit (0.0f, 1.0f, (20 * std::log10 (smooth[(size_t) k] + 1e-12f) + 100) / 70));
        out.add (v);
    }
    return out;
}

void SkatomEditor::timerCallback()
{
    {
        bool up = false;
        for (auto* c : getChildren()) // the update banner, not the resize corner or border JUCE adds
            if (c != &web && c->isVisible() && dynamic_cast<juce::ResizableCornerComponent*> (c) == nullptr && dynamic_cast<juce::ResizableBorderComponent*> (c) == nullptr) up = true;
        if (up != bannerUp) { bannerUp = up; resized(); }
    }
    if (! ready) return;
    // host-side changes (automation, presets, undo) → UI
    auto* changed = new juce::DynamicObject();
    bool any = false;
    for (int i = 0; i < NUM_PARAMS; ++i)
    {
        const float v = proc.apvts.getRawParameterValue (defs()[(size_t) i].id)->load();
        if (v != lastSent[i]) { lastSent[i] = v; changed->setProperty (defs()[(size_t) i].id, v); any = true; }
    }
    if (any) web.emitEventIfBrowserIsVisible ("params", juce::var (changed));
    else delete changed;

    auto& t = proc.tele;
    auto* o = new juce::DynamicObject();
    o->setProperty ("ppq", t.ppq.load()); o->setProperty ("bpm", t.bpm.load()); o->setProperty ("playing", t.playing.load());
    o->setProperty ("env", t.env.load()); o->setProperty ("rms", t.rms.load()); o->setProperty ("gr", t.gr.load());
    o->setProperty ("mode", t.mode.load()); o->setProperty ("pos", t.pos.load()); o->setProperty ("len", t.len.load());
    o->setProperty ("rate", t.rate.load()); o->setProperty ("gl", t.glActive.load()); o->setProperty ("drops", t.drops.load());
    o->setProperty ("macroEff", t.macroEff.load()); o->setProperty ("riseProg", t.riseProg.load()); o->setProperty ("riseDone", t.riseDone.load()); o->setProperty ("activeBuild", t.activeBuild.load());
    o->setProperty ("sr", proc.getSr());
    o->setProperty ("bands", bands());
    web.emitEventIfBrowserIsVisible ("telemetry", juce::var (o));
}
