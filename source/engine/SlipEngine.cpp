#include "engine/SlipEngine.h"

#include <algorithm>
#include <cmath>

namespace slip
{
void Engine::prepare (double sampleRate, int channels, double maxSeconds)
{
    sr_ = sampleRate > 0.0 ? sampleRate : 44100.0;
    channels_ = std::clamp (channels, 1, kMaxChannels);
    int size = 1024;
    while (size < (int) (maxSeconds * sr_)) size <<= 1;
    size_ = size;
    for (auto& r : ring_) r.assign ((size_t) size_, 0.0f);
    reset();
}

void Engine::reset()
{
    for (auto& r : ring_) std::fill (r.begin(), r.end(), 0.0f);
    written_ = kLatency + 4;   // a little silence behind live, so the first head is valid
    state_ = State::live;
    head_ = (double) livePos();
    speed_ = 1.0;
    countdown_ = 0.0;
    fadePos_ = 0;
    scratching_ = scratchAnchored_ = touchAnchored_ = false;
    firePending_ = false;
    shotLeft_ = 0.0;
    status_.state.store (0);
    status_.lagSeconds.store (0.0f);
    status_.speed.store (1.0f);
    status_.untilSnapBeats.store (-1.0f);
}

float Engine::liveSample (int ch) const noexcept
{
    return ring_[(size_t) ch][(size_t) (livePos() & (size_ - 1))];
}

float Engine::sampleAt (int ch, double pos) const noexcept
{
    // 4-point Hermite: exact at whole samples, so a head sitting on live plays live exactly.
    const auto& r = ring_[(size_t) ch];
    const int64_t i = (int64_t) std::floor (pos);
    const float t = (float) (pos - (double) i);
    const int64_t mask = size_ - 1;
    const auto at = [&] (int64_t k) { return r[(size_t) (k & mask)]; };
    const float y0 = at (i - 1), y1 = at (i), y2 = at (i + 1), y3 = at (i + 2);
    const float c1 = 0.5f * (y2 - y0), c2 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3, c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);
    return ((c3 * t + c2) * t + c1) * t + y1;
}

double Engine::samplesToSnap (Snap snap, double ppqNow, double fromBeats, const Clock& clk) const noexcept
{
    const double spb = sr_ * 60.0 / std::max (20.0, clk.bpm);
    if (snap == Snap::instant || ! clk.playing) return fromBeats * spb;
    const double q = snap == Snap::bar ? std::max (0.25, clk.barBeats) : 1.0;
    const double target = ppqNow + fromBeats;
    const double next = std::ceil (target / q - 1.0e-7) * q;
    return std::max (0.0, (next - ppqNow) * spb);
}

void Engine::process (float* const* chs, int numChannels, int n, const Settings& s, const Clock& clk)
{
    if (size_ == 0 || n <= 0) return;
    const double spb = sr_ * 60.0 / std::max (20.0, clk.bpm);
    const double slow = 1.0 / std::max (1.0, s.inertiaMs * 0.001 * sr_);   // speed change per sample
    const double fast = 1.0 / std::max (1.0, 0.02 * sr_);
    const double follow = 1.0 - std::exp (-1.0 / (0.004 * sr_));
    const int quiet = (int) (0.12 * sr_);                                   // a scratch lets go after this still
    const float scratchStart = lastScratch_;
    const bool moved = std::abs (s.scratch - lastScratch_) > 1.0e-4f;
    if (moved && ! scratching_) { scratching_ = true; scratchAnchored_ = false; }
    if (moved) lastMove_ = written_ + n;
    if (! s.touch) touchAnchored_ = false;
    const int outs = std::min (numChannels, kMaxChannels);
    const auto startFade = [this, &s] {
        state_ = State::fading;
        fadePos_ = 0;
        fadeLen_ = std::max (1, (int) (s.fadeMs * 0.001 * sr_));
    };

    for (int i = 0; i < n; ++i)
    {
        for (int ch = 0; ch < channels_; ++ch)
            ring_[(size_t) ch][(size_t) (written_ & (size_ - 1))] = chs[std::min (ch, numChannels - 1)][i];
        ++written_;
        const int64_t live = livePos();
        const double ppqNow = clk.ppq + (double) i / spb;

        if (firePending_)
        {
            firePending_ = false;
            shot_ = s.gesture;
            shotLeft_ = std::max (1.0, samplesToSnap (s.snap, ppqNow, std::max (0.0, s.lengthBeats), clk));
            if (state_ == State::live || state_ == State::fading) { state_ = State::slipping; head_ = (double) live; speed_ = 1.0; }
            if (shot_ == Gesture::backspin) speed_ = -4.0;   // the hand throws it back, then it slows
        }
        if (scratching_ && ! s.hold && written_ - lastMove_ > quiet) { scratching_ = false; scratchAnchored_ = false; }
        const bool shot = shotLeft_ > 0.0;
        const bool active = s.hold || s.reverse || std::abs (s.jog) > 1.0e-3f || scratching_ || s.touch || shot;

        switch (state_)
        {
            case State::live:
                if (active) { state_ = State::slipping; head_ = (double) live; speed_ = 1.0; }
                break;
            case State::slipping:
                if (! active)
                {
                    countdown_ = samplesToSnap (s.snap, ppqNow, 0.0, clk);
                    if (s.snap == Snap::instant || ! clk.playing || countdown_ < 1.0) startFade();
                    else state_ = State::returning;
                }
                break;
            case State::returning:
                if (active) state_ = State::slipping;
                else if ((countdown_ -= 1.0) <= 0.0) startFade();
                break;
            case State::fading: break;   // a few ms: a new gesture starts once it's done
        }

        if (state_ != State::live)
        {
            if (s.touch || scratching_)
            {
                double target;
                if (s.touch)
                {
                    if (! touchAnchored_) { touchAnchored_ = true; scratchAnchor_ = head_; touchFrom_ = s.touchSeconds; }
                    target = scratchAnchor_ + (s.touchSeconds - touchFrom_) * sr_;
                    scratchAnchored_ = false;
                }
                else
                {
                    if (! scratchAnchored_) { scratchAnchored_ = true; scratchAnchor_ = head_; scratchFrom_ = scratchStart; }
                    const double v = moved ? scratchStart + (s.scratch - scratchStart) * (double) (i + 1) / n : s.scratch;
                    target = scratchAnchor_ + (v - scratchFrom_) * s.scratchBeats * spb;
                }
                const double prev = head_;
                head_ += (target - head_) * follow;
                speed_ = head_ - prev;
            }
            else
            {
                double t = 1.0;
                bool quick = false;
                if (shot)
                    t = shot_ == Gesture::half ? 0.5 : shot_ == Gesture::rewind ? -1.0 : 0.0;
                else if (s.reverse) t = -1.0;
                else if (s.hold) t = 0.0;
                else
                {
                    t = std::exp2 ((double) s.jog * s.jogSemis / 12.0);
                    quick = speed_ > 0.25;   // a nudge on a running platter is quick; a start from still isn't
                }
                const double rate = quick ? fast : slow;
                speed_ += std::clamp (t - speed_, -rate * (shot && shot_ == Gesture::backspin ? 4.0 : 1.0), rate);
                head_ += speed_;
            }
            // Never past live (it hasn't been played yet), never off the end of what's kept.
            if (head_ > (double) live) { head_ = (double) live; speed_ = std::min (speed_, 1.0); }
            const double oldest = (double) (written_ - size_ + 4);
            if (head_ < oldest) head_ = oldest;
        }
        if (shot) shotLeft_ -= 1.0;

        if (state_ == State::live)
        {
            for (int ch = 0; ch < outs; ++ch) chs[ch][i] = liveSample (std::min (ch, channels_ - 1));
        }
        else
        {
            float gHead = 1.0f, gLive = 0.0f;
            if (state_ == State::fading)
            {
                const float x = (float) fadePos_ / (float) fadeLen_ * 1.5707963f;
                gHead = std::cos (x);
                gLive = std::sin (x);
            }
            for (int ch = 0; ch < outs; ++ch)
            {
                const int c = std::min (ch, channels_ - 1);
                chs[ch][i] = gHead * sampleAt (c, head_) + (gLive > 0.0f ? gLive * liveSample (c) : 0.0f);
            }
            if (state_ == State::fading && ++fadePos_ >= fadeLen_) state_ = State::live;
        }
    }
    lastScratch_ = s.scratch;

    status_.state.store ((int) state_, std::memory_order_relaxed);
    status_.lagSeconds.store (state_ == State::live ? 0.0f : (float) (lagSamples() / sr_), std::memory_order_relaxed);
    status_.speed.store (state_ == State::live ? 1.0f : (float) speed_, std::memory_order_relaxed);
    status_.untilSnapBeats.store (state_ == State::returning ? (float) (countdown_ / spb) : shotLeft_ > 0.0 ? (float) (shotLeft_ / spb) : -1.0f,
                                  std::memory_order_relaxed);
    status_.turns.store ((state_ == State::live ? (double) livePos() : head_) / sr_ * (100.0 / 3.0 / 60.0), std::memory_order_relaxed);
}
} // namespace slip
