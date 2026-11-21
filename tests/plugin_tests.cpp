#include "plugin/PluginEditor.h"
#include "plugin/PluginProcessor.h"

#include <catch2/catch_test_macros.hpp>

using namespace slip;

namespace
{
void block (SlipProcessor& p, juce::MidiBuffer midi = {}, int n = 512)
{
    juce::AudioBuffer<float> b (2, n);
    for (int ch = 0; ch < 2; ++ch)
        for (int i = 0; i < n; ++i) b.setSample (ch, i, 0.1f * (float) std::sin (0.05 * i));
    p.processBlock (b, midi);
}
void hostSet (SlipProcessor& p, const char* id, float v)   // as Live's Key Map / MIDI Map does it
{
    auto* q = p.state().getParameter (id);
    q->setValue (v);
    q->sendValueChangedMessageToListeners (v);
}
} // namespace

TEST_CASE ("Plug-in: reports its latency, holds from the Hold parameter, and lets go")
{
    SlipProcessor p;
    p.prepareToPlay (48000.0, 512);
    CHECK (p.getLatencySamples() == Engine::kLatency);
    block (p);
    CHECK (p.status().state.load() == (int) State::live);
    hostSet (p, "hold", 1.0f);
    for (int i = 0; i < 10; ++i) block (p);
    CHECK (p.status().state.load() == (int) State::slipping);
    CHECK (p.status().lagSeconds.load() > 0.01f);   // braking over Inertia (450 ms): ~0.013 s behind after 0.1 s
    hostSet (p, "hold", 0.0f);   // no transport: the snap is at once
    for (int i = 0; i < 4; ++i) block (p);
    CHECK (p.status().state.load() == (int) State::live);
}

TEST_CASE ("Plug-in: every press of a key mapped to Fire plays the gesture (Live toggles it each time)")
{
    SlipProcessor p;
    p.prepareToPlay (48000.0, 512);
    hostSet (p, "length", 0.0f);   // 1/4 beat
    for (float v : { 1.0f, 0.0f })
    {
        hostSet (p, "fire", v);
        block (p);
        CHECK (p.status().state.load() == (int) State::slipping);
        for (int i = 0; i < 20; ++i) block (p);
        CHECK (p.status().state.load() == (int) State::live);
    }
}

TEST_CASE ("Plug-in: MIDI C1 holds while the note is down; the pitch wheel jogs")
{
    SlipProcessor p;
    p.prepareToPlay (48000.0, 512);
    juce::MidiBuffer on, off, bend;
    on.addEvent (juce::MidiMessage::noteOn (1, SlipProcessor::kHoldNote, (juce::uint8) 100), 100);
    off.addEvent (juce::MidiMessage::noteOff (1, SlipProcessor::kHoldNote), 0);
    block (p, on);
    for (int i = 0; i < 5; ++i) block (p);
    CHECK (p.status().state.load() == (int) State::slipping);
    block (p, off);
    for (int i = 0; i < 3; ++i) block (p);
    CHECK (p.status().state.load() == (int) State::live);
    bend.addEvent (juce::MidiMessage::pitchWheel (1, 0), 0);   // all the way down
    block (p, bend);
    for (int i = 0; i < 20; ++i) block (p);
    CHECK (p.status().state.load() == (int) State::slipping);
    CHECK (p.status().speed.load() < 0.95f);
}

TEST_CASE ("Plug-in: the set keeps the settings but always opens live")
{
    SlipProcessor a;
    hostSet (a, "hold", 1.0f);
    hostSet (a, "inertia", 0.8f);
    hostSet (a, "snap", 1.0f);
    juce::MemoryBlock mb;
    a.getStateInformation (mb);
    SlipProcessor b;
    b.setStateInformation (mb.getData(), (int) mb.getSize());
    CHECK (b.state().getParameter ("hold")->getValue() == 0.0f);
    CHECK (std::abs (b.state().getParameter ("inertia")->getValue() - 0.8f) < 1.0e-3f);
    CHECK (b.state().getParameter ("snap")->getValue() == 1.0f);
}

TEST_CASE ("Editor: opens, and a mapped Hold lights up while one clicked in the window doesn't")
{
    SlipProcessor p;
    p.prepareToPlay (48000.0, 512);
    std::unique_ptr<juce::AudioProcessorEditor> e (p.createEditor());
    e->createComponentSnapshot (e->getLocalBounds());
    auto& sp = dynamic_cast<SlipEditor&> (*e).getSpotlight();
    sp.poll();
    CHECK (sp.litCount() == 0);
    hostSet (p, "hold", 1.0f);
    sp.poll();
    CHECK (sp.newest() == "hold");
    // SLIP_SNAPSHOT=file.png: a picture of the window, slipping.
    if (auto path = juce::SystemStats::getEnvironmentVariable ("SLIP_SNAPSHOT", {}); path.isNotEmpty())
    {
        for (int i = 0; i < 30; ++i) block (p);
        e->repaint();
        const auto img = e->createComponentSnapshot (e->getLocalBounds(), true, 2.0f);
        juce::File f (path);
        f.deleteFile();
        juce::FileOutputStream os (f);
        juce::PNGImageFormat().writeImageToStream (img, os);
    }
}
