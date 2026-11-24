// Checks Loudness.h against the EBU Tech 3341 / 3342 reference cases (1 kHz stereo sine segments).
#include <juce_core/juce_core.h>
#include "Loudness.h"
#include <vector>
#include <cstdio>

struct Seg { double seconds, dbfs; };

static void run (double sr, std::initializer_list<Seg> segs, Loudness& l)
{
    l.prepare (sr);
    std::vector<float> buf (512);
    double ph = 0;
    for (auto s : segs)
    {
        const double a = std::pow (10.0, s.dbfs / 20.0);
        long long left = (long long) std::llround (s.seconds * sr);
        while (left > 0)
        {
            const int n = (int) std::min<long long> (512, left);
            for (int i = 0; i < n; ++i) { buf[(size_t) i] = (float) (a * std::sin (ph)); ph += juce::MathConstants<double>::twoPi * 1000.0 / sr; }
            const float* ch[2] = { buf.data(), buf.data() };
            l.process (ch, 2, n);
            left -= n;
        }
    }
}

int main()
{
    int fails = 0;
    auto check = [&] (const char* name, double got, double want, double tol)
    {
        const bool ok = std::abs (got - want) <= tol;
        std::printf ("%-44s got %8.2f want %7.2f  %s\n", name, got, want, ok ? "ok" : "FAIL");
        if (! ok) ++fails;
    };

    for (double sr : { 44100.0, 48000.0, 96000.0 })
    {
        std::printf ("--- %.0f Hz\n", sr);
        { Loudness l; run (sr, { { 20, -23 } }, l);
          check ("3341-1  -23 dBFS: I", l.I.load(), -23, 0.1);
          check ("3341-1  -23 dBFS: M", l.M.load(), -23, 0.1);
          check ("3341-1  -23 dBFS: S", l.S.load(), -23, 0.1); }
        { Loudness l; run (sr, { { 20, -33 } }, l);
          check ("3341-2  -33 dBFS: I", l.I.load(), -33, 0.1); }
        { Loudness l; run (sr, { { 10, -36 }, { 60, -23 }, { 10, -36 } }, l);
          check ("3341-3  rel gate: I", l.I.load(), -23, 0.1); }
        { Loudness l; run (sr, { { 10, -72 }, { 10, -36 }, { 60, -23 }, { 10, -36 }, { 10, -72 } }, l);
          check ("3341-4  abs+rel gate: I", l.I.load(), -23, 0.1); }
        { Loudness l; run (sr, { { 20, -20 }, { 20, -30 } }, l);
          check ("3342-1  LRA 10", l.LRA.load(), 10, 1); }
        { Loudness l; run (sr, { { 20, -20 }, { 20, -15 } }, l);
          check ("3342-2  LRA 5", l.LRA.load(), 5, 1); }
        { Loudness l; run (sr, { { 20, -40 }, { 20, -20 } }, l);
          check ("3342-3  LRA 20", l.LRA.load(), 20, 1); }
        { Loudness l; run (sr, { { 5, -20 } }, l); l.requestReset();
          const float* ch[2] = { nullptr, nullptr }; l.process (ch, 0, 0);
          check ("reset clears I", l.I.load(), -150, 0); }
    }
    std::printf (fails ? "\n%d FAILED\n" : "\nALL PASSED\n", fails);
    return fails ? 1 : 0;
}
