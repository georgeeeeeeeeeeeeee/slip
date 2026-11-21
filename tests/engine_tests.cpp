#include "engine/SlipEngine.h"

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <functional>
#include <random>
#include <vector>

using namespace slip;

namespace
{
constexpr double kSr = 48000.0, kBpm = 120.0, kSpb = kSr * 60.0 / kBpm;   // 24000 samples a beat
constexpr int kBlock = 256;

std::vector<float> noise (int n, unsigned seed = 1)
{
    std::mt19937 r (seed);
    std::uniform_real_distribution<float> d (-0.5f, 0.5f);
    std::vector<float> x ((size_t) n);
    for (auto& v : x) v = d (r);
    return x;
}

std::vector<float> sine (int n, double hz = 440.0)
{
    std::vector<float> x ((size_t) n);
    for (int i = 0; i < n; ++i) x[(size_t) i] = 0.5f * (float) std::sin (2.0 * 3.14159265358979 * hz * i / kSr);
    return x;
}

struct Run
{
    std::vector<float> out;
    std::vector<int> state;   // per block, after it
    std::vector<double> lag;  // per block, samples
};

// Plays x through the engine (both channels the same), the transport running from ppq 0 at 120 BPM.
// `settings (firstSample)` gives each block's settings; `fireAt` fires once at that block.
Run play (const std::vector<float>& x, std::function<Settings (int)> settings, int fireAt = -1, bool playing = true)
{
    Engine e;
    e.prepare (kSr, 2);
    Run r;
    r.out.resize (x.size());
    std::vector<float> l (kBlock), rr (kBlock);
    for (int at = 0; at < (int) x.size(); at += kBlock)
    {
        const int n = std::min (kBlock, (int) x.size() - at);
        std::copy (x.begin() + at, x.begin() + at + n, l.begin());
        std::copy (x.begin() + at, x.begin() + at + n, rr.begin());
        float* ch[2] { l.data(), rr.data() };
        Clock c;
        c.playing = playing;
        c.bpm = kBpm;
        c.ppq = at / kSpb;
        if (at == fireAt) e.fire();
        e.process (ch, 2, n, settings (at), c);
        REQUIRE (l[0] == rr[0]);
        std::copy (l.begin(), l.begin() + n, r.out.begin() + at);
        r.state.push_back ((int) e.state());
        r.lag.push_back (e.lagSamples());
    }
    return r;
}

// out[i] is the input kLatency samples earlier: the track where it is.
bool isLive (const Run& r, const std::vector<float>& x, int from, int to)
{
    for (int i = std::max (from, Engine::kLatency); i < to; ++i)
        if (r.out[(size_t) i] != x[(size_t) (i - Engine::kLatency)]) return false;
    return true;
}

Settings base (Snap snap = Snap::instant)
{
    Settings s;
    s.snap = snap;
    s.inertiaMs = 200.0;
    return s;
}
} // namespace

TEST_CASE ("Live: the track passes through exactly, kLatency samples late")
{
    const auto x = noise ((int) kSr);
    const auto r = play (x, [] (int) { return base(); });
    CHECK (isLive (r, x, 0, (int) x.size()));
}

TEST_CASE ("Hold, then let go: it snaps straight back to where the track is now")
{
    const auto x = noise (3 * (int) kSr);
    const int on = 40 * kBlock, off = on + (int) (0.5 * kSr) / kBlock * kBlock;
    const auto r = play (x, [=] (int at) { auto s = base(); s.hold = at >= on && at < off; return s; });
    CHECK (isLive (r, x, 0, on));
    CHECK (! isLive (r, x, on + 2000, off));          // slipping: not the track
    CHECK (r.lag[(size_t) (off / kBlock - 1)] > 0.3 * kSr);   // the head fell well behind
    CHECK (isLive (r, x, off + 400, (int) x.size())); // after the 6 ms fade: exactly the track again
}

TEST_CASE ("Snap on the beat: let go mid-beat, and it comes back on the next beat line")
{
    const auto x = noise (4 * (int) kSpb);
    const int on = (int) (1.25 * kSpb) / kBlock * kBlock, off = (int) (2.3 * kSpb) / kBlock * kBlock;
    const auto r = play (x, [=] (int at) { auto s = base (Snap::beat); s.hold = at >= on && at < off; return s; });
    const int line = 3 * (int) kSpb;   // beat 4 starts at ppq 3
    CHECK (! isLive (r, x, line - 2000, line - 10));   // still displaced right up to the line
    CHECK (isLive (r, x, line + 400, (int) x.size()));  // and back on it
    // The return is played at the track's speed: the head is back up to 1x before the line.
    CHECK (r.state[(size_t) ((line - 2000) / kBlock)] == (int) State::returning);
}

TEST_CASE ("Snap on the bar waits for the next bar line")
{
    const auto x = noise (9 * (int) kSpb);
    const int on = (int) (1.0 * kSpb) / kBlock * kBlock, off = (int) (2.5 * kSpb) / kBlock * kBlock;
    const auto r = play (x, [=] (int at) { auto s = base (Snap::bar); s.reverse = at >= on && at < off; return s; });
    const int bar = 4 * (int) kSpb;
    CHECK (! isLive (r, x, bar - 3000, bar - 10));
    CHECK (isLive (r, x, bar + 400, (int) x.size()));
}

TEST_CASE ("Reverse plays backwards: the head falls behind twice as fast as time passes")
{
    const auto x = noise (2 * (int) kSr);
    auto s = base();
    s.inertiaMs = 20.0;
    s.reverse = true;
    const auto r = play (x, [=] (int) { return s; });
    const double lag = r.lag.back() / kSr, elapsed = (double) x.size() / kSr;
    CHECK (lag > 2.0 * elapsed - 0.05);
    CHECK (lag < 2.0 * elapsed + 0.01);
}

TEST_CASE ("Jogging forward can't run ahead of the track: it catches up and stays there")
{
    const auto x = noise ((int) kSr);
    const auto r = play (x, [] (int) { auto s = base(); s.jog = 1.0f; return s; });
    for (auto l : r.lag) { CHECK (l >= 0.0); CHECK (l < 1.0); }
}

TEST_CASE ("Jog back then forward: slower, then faster, the head comes back to live by itself")
{
    const auto x = noise (3 * (int) kSr);
    const int half = (int) kSr / kBlock * kBlock;
    const auto r = play (x, [=] (int at) { auto s = base(); s.jog = at < half ? -1.0f : 1.0f; return s; });
    const double behind = r.lag[(size_t) (half / kBlock - 1)];
    CHECK (behind > 0.1 * kSr);   // -2 semitones for a second: about 0.11 s behind
    CHECK (r.lag.back() < 1.0);   // +2 for two seconds catches up
}

TEST_CASE ("Fire: Brake for 1 beat, snapping on the beat, comes back on the line after it")
{
    const auto x = noise (4 * (int) kSpb);
    const int fireAt = (int) (0.5 * kSpb) / kBlock * kBlock;   // ppq ~0.5: 1 beat on is ~1.5, so the line at 2
    const auto r = play (x, [] (int) { auto s = base (Snap::beat); s.gesture = Gesture::brake; s.lengthBeats = 1.0; return s; }, fireAt);
    const int line = 2 * (int) kSpb;
    CHECK (isLive (r, x, 0, fireAt));
    CHECK (! isLive (r, x, fireAt + 3000, line - 10));
    CHECK (isLive (r, x, line + 400, (int) x.size()));
}

TEST_CASE ("Fire: Backspin and Rewind go backwards; Half speed falls behind half as fast")
{
    const auto x = noise (2 * (int) kSpb);
    for (auto g : { Gesture::backspin, Gesture::rewind, Gesture::half })
    {
        const auto r = play (x, [g] (int) { auto s = base(); s.gesture = g; s.lengthBeats = 1.0; s.inertiaMs = 20.0; return s; }, 0);
        const double lagAtEnd = r.lag[(size_t) ((int) kSpb / kBlock - 2)];
        if (g == Gesture::half) { CHECK (lagAtEnd > 0.45 * kSpb); CHECK (lagAtEnd < 0.55 * kSpb); }
        else CHECK (lagAtEnd > 1.0 * kSpb);
        CHECK (r.state.back() == (int) State::live);   // and back, with the transport stopped too
    }
}

TEST_CASE ("The Scratch knob moves the head by its range; still for a moment, it lets go and snaps")
{
    const auto x = noise ((int) kSr);
    const int moveAt = 40 * kBlock;
    const auto r = play (x, [=] (int at) {
        auto s = base();
        s.scratchBeats = 1.0;
        s.scratch = at >= moveAt ? 0.25f : 0.5f;   // a quarter of the travel back: a quarter beat
        return s;
    });
    // ~40 ms later: settled a quarter beat back, plus the time the hand has held it still since
    // (a held record doesn't turn while the track goes on).
    const double lag = r.lag[(size_t) (moveAt / kBlock + 8)] - 9.0 * kBlock;
    CHECK (lag > 0.22 * kSpb);
    CHECK (lag < 0.28 * kSpb);
    CHECK (r.state.back() == (int) State::live);
    CHECK (isLive (r, x, moveAt + (int) (0.5 * kSr), (int) x.size()));
}

TEST_CASE ("The on-screen platter scratches by how far it's turned")
{
    const auto x = noise ((int) kSr);
    const int on = 20 * kBlock;
    const auto r = play (x, [=] (int at) {
        auto s = base();
        s.touch = at >= on;
        s.touchSeconds = at >= on + 10 * kBlock ? -0.3 : 0.0;   // pulled back 0.3 s
        return s;
    });
    // Held from the touch on, while time goes on: 0.3 s pulled plus all the time it was held.
    const double held = (double) (x.size() - (size_t) on) / kSr;
    CHECK (std::abs (r.lag.back() / kSr - (0.3 + held)) < 0.02);
}

TEST_CASE ("No click: the snap back crossfades")
{
    const auto x = sine (3 * (int) kSr);
    const int on = 40 * kBlock, off = on + 120 * kBlock;
    const auto r = play (x, [=] (int at) { auto s = base(); s.reverse = at >= on && at < off; return s; });
    float worst = 0.0f;
    for (int i = on - 100; i < off + 2000; ++i) worst = std::max (worst, std::abs (r.out[(size_t) i] - r.out[(size_t) i - 1]));
    CHECK (worst < 0.09f);   // the sine itself moves up to 0.029 a sample
}

TEST_CASE ("Long holds stay inside the buffer; silence and odd settings never give NaN")
{
    std::vector<float> x ((size_t) (40 * kSr), 0.0f);
    auto n = noise (2 * (int) kSr);
    std::copy (n.begin(), n.end(), x.begin());
    const auto r = play (x, [] (int) { auto s = base(); s.reverse = true; s.inertiaMs = 1.0; s.jogSemis = 12.0; s.jog = -1.0f; return s; });
    for (auto v : r.out) REQUIRE (std::isfinite (v));
    CHECK (r.lag.back() < (double) (1 << 21));   // the buffer: 32 s rounded up to a power of two
}
