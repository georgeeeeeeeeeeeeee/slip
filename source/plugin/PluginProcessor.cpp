#include "plugin/PluginProcessor.h"
#include "plugin/PluginEditor.h"

namespace slip
{
const juce::StringArray& SlipProcessor::gestureNames()
{
    static const juce::StringArray n { "Brake", "Backspin", "Half speed", "Rewind" };
    return n;
}

const juce::StringArray& SlipProcessor::lengthNames()
{
    static const juce::StringArray n { "1/4 beat", "1/2 beat", "1 beat", "2 beats", "1 bar", "2 bars" };
    return n;
}

double SlipProcessor::lengthBeats (int i, double barBeats)
{
    switch (i)
    {
        case 0: return 0.25;
        case 1: return 0.5;
        case 2: return 1.0;
        case 3: return 2.0;
        case 4: return barBeats;
        default: return 2.0 * barBeats;
    }
}

juce::AudioProcessorValueTreeState::ParameterLayout SlipProcessor::layout()
{
    using F = juce::AudioParameterFloat;
    using B = juce::AudioParameterBool;
    using C = juce::AudioParameterChoice;
    const auto id = [] (const char* s) { return juce::ParameterID { s, 1 }; };
    const auto unit = [] (const char* u, int dp) {
        const juce::String suffix = juce::String (" ") + u;
        return juce::AudioParameterFloatAttributes().withLabel (u).withStringFromValueFunction ([dp, suffix] (float v, int) { return juce::String (v, dp) + suffix; });
    };
    juce::AudioProcessorValueTreeState::ParameterLayout l;
    l.add (std::make_unique<B> (id ("hold"), "Hold", false));
    l.add (std::make_unique<B> (id ("reverse"), "Reverse", false));
    l.add (std::make_unique<F> (id ("jog"), "Jog", juce::NormalisableRange<float> (-1.0f, 1.0f), 0.0f,
                                juce::AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) {
                                    return std::abs (v) < 0.005f ? juce::String ("centre") : (v > 0 ? "+" : "") + juce::String (juce::roundToInt (v * 100.0f)) + "%";
                                })));
    l.add (std::make_unique<F> (id ("scratch"), "Scratch", juce::NormalisableRange<float> (0.0f, 1.0f), 0.5f,
                                juce::AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return juce::String (juce::roundToInt (v * 100.0f)) + "%"; })));
    l.add (std::make_unique<B> (id ("fire"), "Fire", false));
    l.add (std::make_unique<C> (id ("gesture"), "Gesture", gestureNames(), 0));
    l.add (std::make_unique<C> (id ("length"), "Length", lengthNames(), 2));
    l.add (std::make_unique<C> (id ("snap"), "Snap", juce::StringArray { "Instant", "Next beat", "Next bar" }, 1));
    l.add (std::make_unique<F> (id ("inertia"), "Inertia", juce::NormalisableRange<float> (20.0f, 3000.0f, 1.0f, 0.4f), 450.0f, unit ("ms", 0)));
    l.add (std::make_unique<F> (id ("jogRange"), "Jog Range", juce::NormalisableRange<float> (0.5f, 12.0f, 0.1f), 2.0f, unit ("st", 1)));
    l.add (std::make_unique<F> (id ("scratchRange"), "Scratch Range", juce::NormalisableRange<float> (0.25f, 4.0f, 0.25f), 1.0f, unit ("beats", 2)));
    l.add (std::make_unique<F> (id ("fade"), "Fade", juce::NormalisableRange<float> (1.0f, 50.0f, 0.5f, 0.6f), 6.0f, unit ("ms", 1)));
    return l;
}

SlipProcessor::SlipProcessor()
    : AudioProcessor (BusesProperties().withInput ("Input", juce::AudioChannelSet::stereo(), true).withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "Slip", layout())
{
    const auto raw = [this] (const char* s) { return apvts.getRawParameterValue (s); };
    p_hold = raw ("hold");
    p_reverse = raw ("reverse");
    p_jog = raw ("jog");
    p_scratch = raw ("scratch");
    p_fire = raw ("fire");
    p_gesture = raw ("gesture");
    p_length = raw ("length");
    p_snap = raw ("snap");
    p_inertia = raw ("inertia");
    p_jogRange = raw ("jogRange");
    p_scratchRange = raw ("scratchRange");
    p_fade = raw ("fade");
    setLatencySamples (Engine::kLatency);
}

bool SlipProcessor::isBusesLayoutSupported (const BusesLayout& l) const
{
    const auto out = l.getMainOutputChannelSet();
    return (out == juce::AudioChannelSet::mono() || out == juce::AudioChannelSet::stereo()) && l.getMainInputChannelSet() == out;
}

void SlipProcessor::prepareToPlay (double sampleRate, int)
{
    engine.prepare (sampleRate, std::max (1, getTotalNumOutputChannels()));
    lastFire = p_fire->load() > 0.5f;
    setLatencySamples (Engine::kLatency);
}

Settings SlipProcessor::settingsNow (double barBeats) const
{
    Settings s;
    s.hold = p_hold->load() > 0.5f || noteHold;
    s.reverse = p_reverse->load() > 0.5f || noteReverse;
    s.jog = std::clamp (p_jog->load() + bendJog + uiJog.load(), -1.0f, 1.0f);
    s.scratch = p_scratch->load();
    s.touch = uiTouch.load();
    s.touchSeconds = uiTouchSeconds.load();
    s.gesture = (Gesture) juce::jlimit (0, 3, (int) p_gesture->load());
    s.lengthBeats = lengthBeats ((int) p_length->load(), barBeats);
    s.snap = (Snap) juce::jlimit (0, 2, (int) p_snap->load());
    s.inertiaMs = p_inertia->load();
    s.jogSemis = p_jogRange->load();
    s.scratchBeats = p_scratchRange->load();
    s.fadeMs = p_fade->load();
    return s;
}

void SlipProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();
    for (int ch = getTotalNumInputChannels(); ch < buffer.getNumChannels(); ++ch) buffer.clear (ch, 0, n);

    Clock clk;
    if (auto* ph = getPlayHead())
        if (const auto pos = ph->getPosition())
        {
            clk.playing = pos->getIsPlaying();
            if (const auto b = pos->getBpm()) clk.bpm = *b;
            if (const auto q = pos->getPpqPosition()) clk.ppq = *q;
            else clk.playing = false;
            if (const auto ts = pos->getTimeSignature(); ts && ts->denominator > 0) clk.barBeats = ts->numerator * 4.0 / ts->denominator;
        }
    bpm.store (clk.bpm);
    const double spb = getSampleRate() * 60.0 / std::max (20.0, clk.bpm);

    // Fire: every change of the parameter is a press (Live's Key Map toggles it each time).
    const bool fireNow = p_fire->load() > 0.5f;
    if (fireNow != lastFire) { lastFire = fireNow; engine.fire(); }

    // Split at MIDI events so notes land on their sample.
    int at = 0;
    const auto run = [&] (int upTo) {
        if (upTo <= at) return;
        float* chans[Engine::kMaxChannels] {};
        const int nc = std::min (buffer.getNumChannels(), (int) Engine::kMaxChannels);
        for (int ch = 0; ch < nc; ++ch) chans[ch] = buffer.getWritePointer (ch, at);
        Clock c = clk;
        c.ppq = clk.ppq + at / spb;
        engine.process (chans, nc, upTo - at, settingsNow (clk.barBeats), c);
        at = upTo;
    };
    for (const auto m : midi)
    {
        const auto msg = m.getMessage();
        const int pos = juce::jlimit (0, n, m.samplePosition);
        if (msg.isNoteOn() || msg.isNoteOff() || msg.isPitchWheel()) run (pos);
        if (msg.isNoteOn())
        {
            if (msg.getNoteNumber() == kHoldNote) noteHold = true;
            else if (msg.getNoteNumber() == kReverseNote) noteReverse = true;
            else if (msg.getNoteNumber() == kFireNote) engine.fire();
        }
        else if (msg.isNoteOff())
        {
            if (msg.getNoteNumber() == kHoldNote) noteHold = false;
            else if (msg.getNoteNumber() == kReverseNote) noteReverse = false;
        }
        else if (msg.isPitchWheel())
            bendJog = (float) (msg.getPitchWheelValue() - 8192) / 8192.0f;
        else if (msg.isAllNotesOff() || msg.isAllSoundOff())
            noteHold = noteReverse = false;
    }
    run (n);
    midi.clear();
}

void SlipProcessor::getStateInformation (juce::MemoryBlock& mb)
{
    auto st = apvts.copyState();
    // The gestures are moments, not settings: a set always opens live.
    for (auto id : { "hold", "reverse", "fire" })
        for (auto c : st)
            if (c.getProperty ("id").toString() == id) c.setProperty ("value", 0.0f, nullptr);
    if (auto xml = st.createXml()) copyXmlToBinary (*xml, mb);
}

void SlipProcessor::setStateInformation (const void* data, int size)
{
    if (auto xml = getXmlFromBinary (data, size); xml != nullptr && xml->hasTagName (apvts.state.getType()))
    {
        apvts.replaceState (juce::ValueTree::fromXml (*xml));
        for (auto id : { "hold", "reverse" })
            if (auto* q = apvts.getParameter (id)) q->setValueNotifyingHost (0.0f);
        lastFire = p_fire->load() > 0.5f;
    }
}

juce::AudioProcessorEditor* SlipProcessor::createEditor() { return new SlipEditor (*this); }
} // namespace slip

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new slip::SlipProcessor(); }
