#include "PluginEditor.h"
#include "BinaryData.h"

using namespace skyz;

namespace
{
    juce::WebBrowserComponent::Options options (std::function<void (const juce::String&, const juce::var&)> on, juce::WebBrowserComponent::ResourceProvider provider)
    {
        using O = juce::WebBrowserComponent::Options;
        auto o = O {}
            .withBackend (O::Backend::webview2)
            .withWinWebView2Options (O::WinWebView2 {}
                                         .withUserDataFolder (juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("SKYZ-WebView2"))
                                         .withBackgroundColour (juce::Colour (0xff04060e)))
            .withNativeIntegrationEnabled()
            .withKeepPageLoadedWhenBrowserIsHidden() // minimising the window must not blank and reload the screen
            .withResourceProvider (std::move (provider));
        for (auto* type : { "ready", "set", "gesture", "preset", "random", "mutate", "save", "ab", "note", "wheel", "lanes", "chain", "dragMidi" })
            o = o.withEventListener (type, [on, t = juce::String (type)] (const juce::var& v) { on (t, v); });
        return o;
    }
    const char* mime (const juce::String& n)
    {
        if (n.endsWith (".html")) return "text/html";
        if (n.endsWith (".png")) return "image/png";
        if (n.endsWith (".js")) return "text/javascript";
        if (n.endsWith (".css")) return "text/css";
        return "application/octet-stream";
    }
}

SkyzEditor::SkyzEditor (SkyzProcessor& p)
    : AudioProcessorEditor (p), proc (p),
      web (options ([this] (const juce::String& t, const juce::var& v) { onEvent (t, v); }, [this] (const juce::String& u) { return resource (u); }))
{
    addAndMakeVisible (web);
    web.goToURL (juce::WebBrowserComponent::getResourceProviderRoot());
    setResizable (true, true);
    setResizeLimits (620, 450, 2480, 1800);
    if (auto* c = getConstrainer()) c->setFixedAspectRatio (1240.0 / 900.0);
    setSize (1240, 900);
    startTimerHz (30);
}

SkyzEditor::~SkyzEditor()
{
    stopTimer();
    for (auto& id : gestures) if (auto* prm = proc.apvts.getParameter (id)) prm->endChangeGesture();
}

std::optional<juce::WebBrowserComponent::Resource> SkyzEditor::resource (const juce::String& url)
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
        return juce::WebBrowserComponent::Resource { std::move (bytes), mime (name) };
    }
    return std::nullopt;
}

void SkyzEditor::sendInit()
{
    auto* o = new juce::DynamicObject();
    auto* params = new juce::DynamicObject();
    for (int i = 0; i < NUM_PARAMS; ++i)
    {
        lastSent[i] = proc.apvts.getRawParameterValue (paramDefs[i].id)->load();
        params->setProperty (paramDefs[i].id, lastSent[i]);
    }
    o->setProperty ("params", params);
    o->setProperty ("lanes", proc.lanesString());
    o->setProperty ("chain", proc.chainString());
    juce::Array<juce::var> list; for (auto& n : proc.presetNames()) list.add (n);
    o->setProperty ("presets", list);
    o->setProperty ("preset", proc.getCurrentProgram());
    o->setProperty ("presetName", proc.currentPresetName());
    web.emitEventIfBrowserIsVisible ("init", juce::var (o));
    lastName = proc.currentPresetName();
    proc.lanesChanged = false;
    ready = true;
}

void SkyzEditor::sendPreset()
{
    auto* o = new juce::DynamicObject();
    o->setProperty ("index", proc.getCurrentProgram());
    o->setProperty ("name", proc.currentPresetName());
    juce::Array<juce::var> list; for (auto& n : proc.presetNames()) list.add (n);
    o->setProperty ("list", list);
    web.emitEventIfBrowserIsVisible ("preset", juce::var (o));
    lastName = proc.currentPresetName();
}

void SkyzEditor::onEvent (const juce::String& type, const juce::var& v)
{
    if (type == "ready") { sendInit(); return; }
    if (type == "set")
    {
        if (auto* obj = v.getDynamicObject())
            for (auto& prop : obj->getProperties())
                if (auto* prm = proc.apvts.getParameter (prop.name.toString()))
                {
                    const bool held = gestures.contains (prop.name.toString());
                    if (! held) prm->beginChangeGesture();
                    prm->setValueNotifyingHost ((float) prop.value);
                    if (! held) prm->endChangeGesture();
                    for (int i = 0; i < NUM_PARAMS; ++i) // the UI already shows it, don't echo
                        if (prop.name.toString() == paramDefs[i].id) { lastSent[i] = proc.apvts.getRawParameterValue (paramDefs[i].id)->load(); break; }
                }
        return;
    }
    if (type == "gesture")
    {
        const auto id = v["id"].toString();
        auto* prm = proc.apvts.getParameter (id);
        if (prm == nullptr) return;
        if ((bool) v["on"] && ! gestures.contains (id)) { gestures.add (id); prm->beginChangeGesture(); }
        else if (! (bool) v["on"] && gestures.contains (id)) { gestures.removeString (id); prm->endChangeGesture(); }
        return;
    }
    if (type == "preset") { proc.setCurrentProgram ((int) v["index"]); sendPreset(); return; }
    if (type == "random") { proc.randomPreset(); sendPreset(); return; }
    if (type == "mutate") { proc.mutate(); sendPreset(); return; }
    if (type == "ab") { proc.abSwap(); sendPreset(); return; }
    if (type == "save") { proc.saveUserPreset (v["name"].toString()); sendPreset(); return; }
    if (type == "note")
    {
        const int n = juce::jlimit (0, 127, (int) v["n"]);
        if ((bool) v["on"]) proc.keyboard.noteOn (1, n, juce::jlimit (0.05f, 1.0f, (float) (int) v["vel"] / 127.0f));
        else proc.keyboard.noteOff (1, n, 0.0f);
        return;
    }
    if (type == "wheel")
    {
        const float x = juce::jlimit (0.0f, 1.0f, (float) v["v"]);
        if (v["which"].toString() == "pitch") proc.uiPitch = juce::jlimit (0, 16383, (int) std::lround (x * 16383.0f));
        else proc.uiMod = juce::jlimit (0, 127, (int) std::lround (x * 127.0f));
        return;
    }
    if (type == "lanes") { proc.setLanes (v["data"].toString()); return; }
    if (type == "chain") { proc.setChain (v["order"].toString()); return; }
    if (type == "dragMidi")
    {
        const auto f = proc.writeArpMidi();
        if (f.existsAsFile()) juce::DragAndDropContainer::performExternalDragDropOfFiles ({ f.getFullPathName() }, false, this);
    }
}

void SkyzEditor::timerCallback()
{
    {
        bool up = false;
        for (auto* c : getChildren()) // the update banner, not the resize corner or border JUCE adds
            if (c != &web && c->isVisible() && dynamic_cast<juce::ResizableCornerComponent*> (c) == nullptr && dynamic_cast<juce::ResizableBorderComponent*> (c) == nullptr) up = true;
        if (up != bannerUp) { bannerUp = up; resized(); }
    }
    if (! ready) return;
    auto* changed = new juce::DynamicObject();
    bool any = false;
    for (int i = 0; i < NUM_PARAMS; ++i)
    {
        const float x = proc.apvts.getRawParameterValue (paramDefs[i].id)->load();
        if (x != lastSent[i]) { lastSent[i] = x; changed->setProperty (paramDefs[i].id, x); any = true; }
    }
    if (any) web.emitEventIfBrowserIsVisible ("params", juce::var (changed));
    else delete changed;

    if (proc.lanesChanged.exchange (false))
    {
        auto* o = new juce::DynamicObject(); o->setProperty ("data", proc.lanesString());
        web.emitEventIfBrowserIsVisible ("lanes", juce::var (o));
    }
    if (proc.currentPresetName() != lastName) sendPreset();

    auto* t = new juce::DynamicObject();
    t->setProperty ("step", proc.arpStep.load());
    t->setProperty ("l", proc.peakL.load());
    t->setProperty ("r", proc.peakR.load());
    juce::Array<juce::var> lfo; for (auto& x : proc.lfoPhase) lfo.add (x.load());
    t->setProperty ("lfo", lfo);
    t->setProperty ("voices", proc.voices.load());
    t->setProperty ("ppq", proc.ppqNow.load());
    t->setProperty ("bpm", proc.bpmNow.load());
    t->setProperty ("wheelP", proc.wheelP.load());
    t->setProperty ("wheelM", proc.wheelM.load());
    t->setProperty ("hit", juce::jmin (1.0f, juce::jmax (proc.peakL.load(), proc.peakR.load())));
    web.emitEventIfBrowserIsVisible ("telemetry", juce::var (t));
}
