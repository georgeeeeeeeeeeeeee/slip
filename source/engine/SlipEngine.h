#pragma once
// Slip: a CDJ's slip mode for any track. The input is recorded all the time, so the place the track
// "would have been" (live) is always known. A gesture (hold the platter, reverse, jog, scratch, or a
// one-tap brake / backspin / half speed / rewind) moves a second read head away from live: pitch and
// time move together, like vinyl. When the gesture ends, the head lets go of the gesture and snaps back
// to live, at once or on the next beat or bar line, so the track stays in time and in its structure.
//
// It can't go ahead of live (the future hasn't been played yet): pushing faster than 1x catches up and
// stops there. The output runs kLatency samples behind the input (reported to the host), so engaging
// a gesture starts from exactly the sample that was playing: no step, no click.
//
// Pure C++, no JUCE: the plug-in and the tests use it directly.

#include <array>
#include <atomic>
#include <cstdint>
#include <vector>

namespace slip
{
enum class Snap { instant, beat, bar };
enum class Gesture { brake, backspin, half, rewind };
enum class State { live, slipping, returning, fading };

struct Settings
{
    bool hold = false;        // hand on the platter: the head slows to a stop (Inertia) and stays
    bool reverse = false;     // plays backwards at 1x (Inertia to get there)
    float jog = 0.0f;         // -1..1: the pitch nudge, +-jogSemis at the ends (0: none)
    float scratch = 0.5f;     // 0..1: platter position for a knob or automation; moving it scratches
    bool touch = false;       // the on-screen platter is held
    double touchSeconds = 0;  // how far it's been turned since (seconds of audio, signed)
    Gesture gesture = Gesture::brake;
    double lengthBeats = 1.0; // how long a fired gesture lasts (then the snap)
    Snap snap = Snap::beat;
    double inertiaMs = 450;   // stops, starts and spin-ups: time between 0 and 1x
    double jogSemis = 2.0;
    double scratchBeats = 1.0;  // the scratch knob's whole travel
    double fadeMs = 6.0;      // the crossfade back to live
};

struct Clock
{
    bool playing = false;
    double ppq = 0.0;         // at the first sample of the call
    double bpm = 120.0;
    double barBeats = 4.0;    // quarter notes per bar
};

class Engine
{
public:
    static constexpr int kLatency = 4;   // the cubic needs two samples ahead of the head
    static constexpr int kMaxChannels = 2;

    void prepare (double sampleRate, int channels, double maxSeconds = 32.0);
    void reset();
    // The one-tap gesture, from the next sample processed.
    void fire() noexcept { firePending_ = true; }
    void process (float* const* channels, int numChannels, int numSamples, const Settings&, const Clock&);

    // For the editor (any thread).
    struct Status
    {
        std::atomic<int> state { 0 };
        std::atomic<float> lagSeconds { 0.0f }, speed { 1.0f }, untilSnapBeats { -1.0f };
        std::atomic<double> turns { 0.0 };   // the platter's rotation (33 1/3 rpm at 1x)
    };
    const Status& status() const noexcept { return status_; }
    State state() const noexcept { return state_; }
    double lagSamples() const noexcept { return (double) livePos() - head_; }
    double speed() const noexcept { return speed_; }

private:
    int64_t livePos() const noexcept { return written_ - 1 - kLatency; }
    float sampleAt (int ch, double pos) const noexcept;
    float liveSample (int ch) const noexcept;
    double samplesToSnap (Snap, double ppqNow, double fromBeats, const Clock&) const noexcept;

    double sr_ = 44100.0;
    int channels_ = 2;
    int size_ = 0;                      // power of two
    std::array<std::vector<float>, kMaxChannels> ring_;
    int64_t written_ = 0;

    State state_ = State::live;
    double head_ = 0.0, speed_ = 1.0;
    double countdown_ = 0.0;            // returning: samples to the snap
    int fadePos_ = 0, fadeLen_ = 1;
    // Scratch (knob / automation) and the on-screen platter.
    float lastScratch_ = 0.5f;
    bool scratching_ = false, scratchAnchored_ = false, touchAnchored_ = false;
    double scratchAnchor_ = 0.0, scratchFrom_ = 0.5, scratchTarget_ = 0.0, touchFrom_ = 0.0;
    int64_t lastMove_ = 0;
    // The fired gesture.
    bool firePending_ = false;
    double shotLeft_ = 0.0;             // samples; > 0 while it plays
    Gesture shot_ = Gesture::brake;

    Status status_;
};
} // namespace slip
