#pragma once
// Slip as a plug-in: an audio effect that also takes MIDI (AU 'aumf', VST3 with an event input).
//
// Every gesture is a parameter, so Live's Key Map and MIDI Map play it:
//   Hold, Reverse: while on (a mapped key toggles: press to grab, press again to let go).
//   Jog: the pitch nudge (centre = none). Scratch: platter position (moving it scratches).
//   Fire: every press plays the chosen Gesture for its Length, then snaps.
// MIDI in (a MIDI track's "MIDI To" this track): C1 holds, C#1 reverses, D1 fires, the pitch-bend
// wheel jogs (it springs back by itself).

#include "engine/SlipEngine.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <atomic>

namespace slip
{
class SlipProcessor final : public juce::AudioProcessor
{
public:
    SlipProcessor();

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using juce::AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return "Slip"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}
    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    juce::AudioProcessorValueTreeState& state() noexcept { return apvts; }
    const Engine::Status& status() const noexcept { return engine.status(); }
    double currentBpm() const noexcept { return bpm.load(); }

    // The on-screen platter (not parameters: it's the mouse, not something to automate).
    std::atomic<bool> uiTouch { false };
    std::atomic<double> uiTouchSeconds { 0.0 };
    std::atomic<float> uiJog { 0.0f };

    static constexpr int kHoldNote = 36, kReverseNote = 37, kFireNote = 38;
    static const juce::StringArray& gestureNames();
    static const juce::StringArray& lengthNames();
    static double lengthBeats (int index, double barBeats);

    // For tests: the settings the next block will use.
    Settings settingsNow (double barBeats) const;

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout layout();
    juce::AudioProcessorValueTreeState apvts;
    Engine engine;
    std::atomic<float>* p_hold, *p_reverse, *p_jog, *p_scratch, *p_fire, *p_gesture, *p_length, *p_snap, *p_inertia, *p_jogRange,
        *p_scratchRange, *p_fade;
    bool lastFire = false;
    bool noteHold = false, noteReverse = false;
    float bendJog = 0.0f;
    std::atomic<double> bpm { 120.0 };
};
} // namespace slip
