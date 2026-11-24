#include "PluginEditor.h"
#include "BinaryData.h"

#if JucePlugin_Build_Standalone
 #include <juce_audio_devices/juce_audio_devices.h>
 #include <juce_audio_utils/juce_audio_utils.h>
 #include <juce_audio_plugin_client/Standalone/juce_StandaloneFilterWindow.h>
#endif

namespace
{
juce::String b64 (const void* data, size_t bytes) { return juce::Base64::toBase64 (data, bytes); }
}

SkzlEditor::SkzlEditor (SkzlProcessor& p)
    : AudioProcessorEditor (p), proc (p),
      web (juce::WebBrowserComponent::Options {}
               .withBackend (juce::WebBrowserComponent::Options::Backend::webview2)
               .withWinWebView2Options (juce::WebBrowserComponent::Options::WinWebView2 {}
                                            .withUserDataFolder (juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("SKAZILAYZER-WebView2"))
                                            .withBackgroundColour (juce::Colour (0xff06070b)))
               .withNativeIntegrationEnabled()
               .withKeepPageLoadedWhenBrowserIsHidden()
               .withResourceProvider ([this] (const juce::String& url) { return getResource (url); })
               .withEventListener ("ready", [this] (const juce::var&) { sendInit(); })
               .withEventListener ("state", [this] (const juce::var& v) { proc.setUiState (v["json"].toString()); })
               .withEventListener ("resetLoud", [this] (const juce::var&) { proc.loud.requestReset(); })
               .withEventListener ("fft", [this] (const juce::var& v)
                                   {
                                       const int size = (int) v["size"];
                                       for (int o = 12; o <= 15; ++o) if (size == 1 << o) setFftOrder (o);
                                   }))
{
    setFftOrder (14);

   #if JucePlugin_Build_Standalone
    // the standalone is a meter: it must hear its input (it never plays anything back, see processBlock)
    if (proc.wrapperType == juce::AudioProcessor::wrapperType_Standalone)
        if (auto* h = juce::StandalonePluginHolder::getInstance())
            h->getMuteInputValue().setValue (false);
   #endif

    // old history blocks from while the window was closed are not wanted
    proc.loud.fifo.reset();

    addAndMakeVisible (web);
    web.goToURL (juce::WebBrowserComponent::getResourceProviderRoot());
    setResizable (true, true);
    setResizeLimits (820, 600, 2600, 1800);
    setSize (1280, 900);
    startTimerHz (30);
}

void SkzlEditor::setFftOrder (int order)
{
    // message thread only (UI event / constructor), same thread as timerCallback
    N = 1 << order;
    fft = std::make_unique<juce::dsp::FFT> (order);
    // Blackman window, same as Web Audio's AnalyserNode (so the browser preview and the plugin read alike)
    window.resize ((size_t) N);
    for (int i = 0; i < N; ++i)
    {
        const double x = juce::MathConstants<double>::twoPi * i / N;
        window[(size_t) i] = (float) (0.42 - 0.5 * std::cos (x) + 0.08 * std::cos (2 * x));
    }
    fftBuf.assign ((size_t) N * 2, 0.0f);
}

std::optional<juce::WebBrowserComponent::Resource> SkzlEditor::getResource (const juce::String& url)
{
    auto name = url.fromLastOccurrenceOf ("/", false, false).upToFirstOccurrenceOf ("?", false, false);
    if (name.isEmpty()) name = "index.html";
    if (name != "index.html") return std::nullopt;
    std::vector<std::byte> bytes ((size_t) BinaryData::index_htmlSize);
    std::memcpy (bytes.data(), BinaryData::index_html, bytes.size());
    return juce::WebBrowserComponent::Resource { std::move (bytes), "text/html" };
}

void SkzlEditor::sendInit()
{
    auto* o = new juce::DynamicObject();
    o->setProperty ("ui", proc.getUiState());
    web.emitEventIfBrowserIsVisible ("init", juce::var (o));
    lastRead = proc.written.load (std::memory_order_acquire);
    ready = true;
}

void SkzlEditor::timerCallback()
{
    {
        bool up = false;
        for (auto* c : getChildren()) // the update banner, not the resize corner or border JUCE adds
            if (c != &web && c->isVisible() && dynamic_cast<juce::ResizableCornerComponent*> (c) == nullptr && dynamic_cast<juce::ResizableBorderComponent*> (c) == nullptr) up = true;
        if (up != bannerUp) { bannerUp = up; resized(); }
    }
    if (! ready) return;
    const auto w = proc.written.load (std::memory_order_acquire);
    const auto& L = proc.ringL;
    const auto& R = proc.ringR;
    constexpr uint64_t MASK = SkzlProcessor::RING - 1;
    auto at = [&] (const std::vector<float>& v, uint64_t i) { return v[(size_t) (i & MASK)]; };

    // exact meter stats over every sample since the last frame (capped so the ring can't lap us)
    if (w - lastRead > SkzlProcessor::RING / 2) lastRead = w - SkzlProcessor::RING / 2;
    double pL = 0, pR = 0, sL = 0, sR = 0, sLR = 0, aL = 0, aR = 0, mn = 0, mx = 0;
    for (auto i = lastRead; i < w; ++i)
    {
        const double l = at (L, i), r = at (R, i);
        pL = std::max (pL, std::abs (l)); pR = std::max (pR, std::abs (r));
        sL += l * l; sR += r * r; sLR += l * r; aL += std::abs (l); aR += std::abs (r);
        mn = std::min ({ mn, l, r }); mx = std::max ({ mx, l, r });
    }
    const auto n = (double) (w - lastRead);
    lastRead = w;

    // spectrum: newest N samples, mono, Blackman, calibrated so a 0 dBFS sine reads 0 dB
    for (int i = 0; i < N; ++i)
    {
        const auto idx = w - (uint64_t) N + (uint64_t) i;
        fftBuf[(size_t) i] = 0.5f * (at (L, idx) + at (R, idx)) * window[(size_t) i];
    }
    std::fill (fftBuf.begin() + N, fftBuf.end(), 0.0f);
    fft->performFrequencyOnlyForwardTransform (fftBuf.data(), true);
    const double sr = proc.sampleRate.load(), binHz = sr / N;
    const int K = juce::jmin (N / 2, (int) std::ceil (22600.0 / binHz) + 2);
    spec.resize ((size_t) K);
    const float cal = 2.0f / (N * 0.42f);
    for (int k = 0; k < K; ++k)
    {
        const float db = 20.0f * std::log10 (fftBuf[(size_t) k] * cal + 1e-9f);
        spec[(size_t) k] = (int16_t) juce::jlimit (-15000, 3000, (int) std::lround (db * 100.0f));
    }

    // goniometer: newest 2 * GONIO samples, every other one
    for (int i = 0; i < GONIO; ++i)
    {
        const auto idx = w - (uint64_t) (2 * GONIO) + (uint64_t) (2 * i);
        gonio[(size_t) (2 * i)]     = (int16_t) juce::jlimit (-32767, 32767, (int) (at (L, idx) * 32767.0f));
        gonio[(size_t) (2 * i + 1)] = (int16_t) juce::jlimit (-32767, 32767, (int) (at (R, idx) * 32767.0f));
    }

    // loudness blocks finished since the last frame
    juce::Array<juce::var> lb;
    auto& f = proc.loud.fifo;
    int s1, n1, s2, n2;
    f.prepareToRead (f.getNumReady(), s1, n1, s2, n2);
    for (int i = 0; i < n1; ++i) lb.add (juce::Array<juce::var> { proc.loud.fifoData[(size_t) (s1 + i) * 2], proc.loud.fifoData[(size_t) (s1 + i) * 2 + 1] });
    for (int i = 0; i < n2; ++i) lb.add (juce::Array<juce::var> { proc.loud.fifoData[(size_t) (s2 + i) * 2], proc.loud.fifoData[(size_t) (s2 + i) * 2 + 1] });
    f.finishedRead (n1 + n2);

    auto* o = new juce::DynamicObject();
    o->setProperty ("sr", sr); o->setProperty ("fft", N);
    o->setProperty ("spec", b64 (spec.data(), spec.size() * sizeof (int16_t)));
    o->setProperty ("g", b64 (gonio.data(), sizeof (gonio)));
    o->setProperty ("pL", pL); o->setProperty ("pR", pR);
    o->setProperty ("sL", sL); o->setProperty ("sR", sR); o->setProperty ("sLR", sLR); o->setProperty ("aL", aL); o->setProperty ("aR", aR);
    o->setProperty ("n", n); o->setProperty ("mn", mn); o->setProperty ("mx", mx);
    o->setProperty ("M", proc.loud.M.load()); o->setProperty ("S", proc.loud.S.load());
    o->setProperty ("I", proc.loud.I.load()); o->setProperty ("LRA", proc.loud.LRA.load());
    o->setProperty ("lb", lb);
    web.emitEventIfBrowserIsVisible ("tele", juce::var (o));
}
