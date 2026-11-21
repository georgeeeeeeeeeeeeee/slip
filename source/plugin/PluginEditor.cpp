#include "plugin/PluginEditor.h"

namespace slip
{
namespace
{
constexpr double kSecondsPerTurn = 1.8;   // 33 1/3 rpm
constexpr float kInner = 0.70f;            // inside this (of the radius): scratch; outside: the jog ring

void setParam (SlipProcessor& p, const char* id, float v01)
{
    if (auto* q = p.state().getParameter (id))
    {
        q->beginChangeGesture();
        q->setValueNotifyingHost (v01);
        q->endChangeGesture();
    }
}
} // namespace

// ---------------------------------------------------------------------------------------------- platter
float Platter::angleAt (juce::Point<float> pt) const
{
    const auto c = getLocalBounds().toFloat().getCentre();
    return std::atan2 (pt.y - c.y, pt.x - c.x);
}

void Platter::paint (juce::Graphics& g)
{
    const auto b = getLocalBounds().toFloat().reduced (4.0f);
    const float R = std::min (b.getWidth(), b.getHeight()) * 0.5f;
    const auto c = b.getCentre();
    const int st = proc.status().state.load();
    const auto ring = st == 0 ? colours::live : st == 2 ? colours::back : colours::accent;
    const double turns = proc.status().turns.load();
    const float ang = (float) (turns - std::floor (turns)) * juce::MathConstants<float>::twoPi;

    // The jog ring.
    g.setColour (colours::raised);
    g.fillEllipse (c.x - R, c.y - R, 2 * R, 2 * R);
    g.setColour (grab == Grab::jog ? colours::accent : colours::line);
    g.drawEllipse (c.x - R + 1, c.y - R + 1, 2 * R - 2, 2 * R - 2, 2.0f);
    for (int i = 0; i < 60; ++i)
    {
        const float a = ang + (float) i / 60.0f * juce::MathConstants<float>::twoPi;
        const float r0 = R * 0.88f, r1 = R * (i % 5 == 0 ? 0.97f : 0.93f);
        g.setColour (colours::faint.withAlpha (i % 5 == 0 ? 0.9f : 0.5f));
        g.drawLine (c.x + std::cos (a) * r0, c.y + std::sin (a) * r0, c.x + std::cos (a) * r1, c.y + std::sin (a) * r1, 1.0f);
    }
    // The record.
    const float r = R * kInner;
    g.setColour (juce::Colour (0xff0b0c0e));
    g.fillEllipse (c.x - r, c.y - r, 2 * r, 2 * r);
    g.setColour (juce::Colours::white.withAlpha (0.04f));
    for (float k = 0.42f; k < 0.98f; k += 0.06f) g.drawEllipse (c.x - r * k, c.y - r * k, 2 * r * k, 2 * r * k, 1.0f);
    // The state ring and the label with its marker.
    g.setColour (ring.withAlpha (0.85f));
    g.drawEllipse (c.x - r, c.y - r, 2 * r, 2 * r, 2.5f);
    const float lr = r * 0.36f;
    g.setColour (grab == Grab::scratch ? colours::accent : colours::panel);
    g.fillEllipse (c.x - lr, c.y - lr, 2 * lr, 2 * lr);
    g.setColour (ring);
    g.drawLine (c.x + std::cos (ang) * lr * 0.25f, c.y + std::sin (ang) * lr * 0.25f, c.x + std::cos (ang) * r * 0.96f, c.y + std::sin (ang) * r * 0.96f, 3.0f);
    g.setColour (colours::text);
    g.setFont (juce::FontOptions (13.0f, juce::Font::bold));
    g.drawText (st == 0 ? "LIVE" : st == 2 ? "RETURN" : st == 3 ? "SNAP" : "SLIP", juce::Rectangle<float> (c.x - lr, c.y - 9, 2 * lr, 18),
                juce::Justification::centred);
}

void Platter::mouseDown (const juce::MouseEvent& e)
{
    const auto c = getLocalBounds().toFloat().getCentre();
    const float R = std::min (getWidth(), getHeight()) * 0.5f - 4.0f;
    grab = e.position.getDistanceFrom (c) < R * kInner ? Grab::scratch : Grab::jog;
    lastAngle = angleAt (e.position);
    if (grab == Grab::scratch) proc.uiTouch = true;
    repaint();
}

void Platter::mouseDrag (const juce::MouseEvent& e)
{
    if (grab == Grab::scratch)
    {
        const float a = angleAt (e.position);
        float d = a - lastAngle;
        if (d > juce::MathConstants<float>::pi) d -= juce::MathConstants<float>::twoPi;
        if (d < -juce::MathConstants<float>::pi) d += juce::MathConstants<float>::twoPi;
        lastAngle = a;
        proc.uiTouchSeconds = proc.uiTouchSeconds.load() + d / juce::MathConstants<double>::twoPi * kSecondsPerTurn;
    }
    else if (grab == Grab::jog)
    {
        // Round the ring: clockwise pushes, the other way drags; how far from where you grabbed it.
        const float a = angleAt (e.position);
        float d = a - angleAt (e.mouseDownPosition);
        if (d > juce::MathConstants<float>::pi) d -= juce::MathConstants<float>::twoPi;
        if (d < -juce::MathConstants<float>::pi) d += juce::MathConstants<float>::twoPi;
        proc.uiJog = juce::jlimit (-1.0f, 1.0f, d / 1.2f);
    }
}

void Platter::mouseUp (const juce::MouseEvent&)
{
    proc.uiTouch = false;
    proc.uiJog = 0.0f;   // the ring springs back
    grab = Grab::none;
    repaint();
}

void Platter::refresh()
{
    const double t = proc.status().turns.load();
    const int st = proc.status().state.load();
    if (std::abs (t - shownTurns) > 0.002 || st != shownState)
    {
        shownTurns = t;
        shownState = st;
        repaint();
    }
}

// ---------------------------------------------------------------------------------------------- editor
juce::Slider& SlipEditor::knob (const char* id, const char* tip)
{
    auto s = std::make_unique<juce::Slider> (juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::TextBoxBelow);
    s->setTextBoxStyle (juce::Slider::TextBoxBelow, false, 78, 16);
    s->setTooltip (tip);
    s->setColour (juce::Slider::rotarySliderFillColourId, colours::accent);
    s->setColour (juce::Slider::rotarySliderOutlineColourId, colours::line);
    s->setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    addAndMakeVisible (*s);
    sliderAs.push_back (std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (proc.state(), id, *s));
    if (auto* q = proc.state().getParameter (id)) s->setDoubleClickReturnValue (true, q->convertFrom0to1 (q->getDefaultValue()));
    lvrs::Spotlight::tag (*s, id);
    knobIds.push_back (id);
    knobs.push_back (std::move (s));
    return *knobs.back();
}

juce::ComboBox& SlipEditor::menu (const char* id, const char* tip)
{
    auto c = std::make_unique<juce::ComboBox>();
    if (auto* q = dynamic_cast<juce::AudioParameterChoice*> (proc.state().getParameter (id))) c->addItemList (q->choices, 1);
    c->setTooltip (tip);
    addAndMakeVisible (*c);
    menuAs.push_back (std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (proc.state(), id, *c));
    lvrs::Spotlight::tag (*c, id);
    menuIds.push_back (id);
    menus.push_back (std::move (c));
    return *menus.back();
}

void SlipEditor::momentary (juce::TextButton& b, const char* id, juce::Colour c, const char* tip)
{
    // Held with the mouse: on while pressed. From a mapped key it latches (Live toggles it).
    b.setColour (juce::TextButton::buttonOnColourId, c);
    b.setColour (juce::TextButton::textColourOnId, colours::bg);
    b.setTooltip (tip);
    b.onStateChange = [this, &b, id] {
        const bool down = b.isDown();
        if (down == (bool) b.getProperties()["wasDown"]) return;
        b.getProperties().set ("wasDown", down);
        setParam (proc, id, down ? 1.0f : 0.0f);
    };
    lvrs::Spotlight::tag (b, id);
    addAndMakeVisible (b);
}

SlipEditor::SlipEditor (SlipProcessor& p) : AudioProcessorEditor (p), proc (p), platter (p)
{
    look.setColourScheme ({ colours::bg, colours::panel, colours::raised, colours::line, colours::text, colours::accent, colours::bg,
                            colours::raised, colours::text });
    look.setColour (juce::ComboBox::backgroundColourId, colours::raised);
    look.setColour (juce::ComboBox::outlineColourId, colours::line);
    look.setColour (juce::TextButton::buttonColourId, colours::raised);
    setLookAndFeel (&look);
    platter.setTooltip ("Drag the record to scratch (it carries on from there when you let go). Turn the outer ring to nudge the pitch: it springs back");
    addAndMakeVisible (platter);

    momentary (hold, "hold", colours::accent, "Hand on the platter: it slows to a stop (Inertia) and waits. Let go and it snaps back to where the track is now");
    momentary (reverse, "reverse", colours::back, "Plays backwards while held. Let go and it snaps back");
    fire.setColour (juce::TextButton::buttonOnColourId, colours::accent);
    fire.setTooltip ("Play the Gesture for its Length, then snap back. Map a key to Fire: every press plays it");
    fire.onClick = [this] {
        auto* q = proc.state().getParameter ("fire");
        setParam (proc, "fire", q->getValue() > 0.5f ? 0.0f : 1.0f);
    };
    lvrs::Spotlight::tag (fire, "fire");
    addAndMakeVisible (fire);

    for (auto* s : { &jog, &scratch })
    {
        s->setSliderStyle (juce::Slider::LinearHorizontal);
        s->setTextBoxStyle (juce::Slider::TextBoxRight, false, 60, 18);
        s->setColour (juce::Slider::trackColourId, colours::accent.withAlpha (0.5f));
        s->setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
        addAndMakeVisible (*s);
    }
    sliderAs.push_back (std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (p.state(), "jog", jog));
    sliderAs.push_back (std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (p.state(), "scratch", scratch));
    jog.setDoubleClickReturnValue (true, 0.0);
    jog.onDragEnd = [this] { jog.setValue (0.0, juce::sendNotificationSync); };   // springs back, like the wheel
    jog.setTooltip ("Pitch nudge (map a knob or the pitch-bend wheel). Centre: none");
    scratch.setTooltip ("Platter position for a mapped knob: moving it scratches; still for a moment, it lets go");
    lvrs::Spotlight::tag (jog, "jog");
    lvrs::Spotlight::tag (scratch, "scratch");

    menu ("gesture", "What Fire plays: Brake (tape stop), Backspin, Half speed, or Rewind");
    menu ("length", "How long a fired gesture lasts before it snaps back");
    menu ("snap", "When the track comes back after a gesture: at once (like a CDJ), or on the next beat or bar line");
    knob ("inertia", "How long the platter takes to stop or get back up to speed");
    knob ("jogRange", "How far the jog nudges the pitch, at its ends");
    knob ("scratchRange", "How much of the track the Scratch knob's whole travel covers");
    knob ("fade", "The crossfade back to the track, so the snap doesn't click");

    spotlight = std::make_unique<lvrs::Spotlight> (p, *this);
    addAndMakeVisible (*spotlight);
    setSize (780, 440);
    startTimerHz (30);
}

SlipEditor::~SlipEditor()
{
    stopTimer();
    proc.uiTouch = false;
    proc.uiJog = 0.0f;
    setLookAndFeel (nullptr);
}

void SlipEditor::paint (juce::Graphics& g)
{
    g.fillAll (colours::bg);
    g.setColour (colours::text);
    g.setFont (juce::FontOptions (20.0f, juce::Font::bold).withKerningFactor (0.18f));
    g.drawText ("SLIP", 18, 10, 120, 26, juce::Justification::centredLeft);
    g.setColour (colours::faint);
    g.setFont (juce::FontOptions (11.0f));
    g.drawText ("gesture, then back in time", 84, 14, 240, 20, juce::Justification::centredLeft);
    g.drawText ("LeverStudios", getWidth() - 220, 14, 200, 20, juce::Justification::centredRight);

    const auto label = [&g] (const juce::String& t, juce::Rectangle<int> r) {
        g.setColour (colours::dim);
        g.setFont (juce::FontOptions (10.5f, juce::Font::bold).withKerningFactor (0.12f));
        g.drawText (t, r, juce::Justification::centredLeft);
    };
    label ("JOG", { jog.getX(), jog.getY() - 16, 120, 14 });
    label ("SCRATCH", { scratch.getX(), scratch.getY() - 16, 120, 14 });
    const char* menuLabels[] = { "GESTURE", "LENGTH", "SNAP BACK" };
    for (size_t i = 0; i < menus.size(); ++i) label (menuLabels[i], { menus[i]->getX(), menus[i]->getY() - 16, 140, 14 });
    const char* knobLabels[] = { "INERTIA", "JOG RANGE", "SCRATCH RANGE", "FADE" };
    for (size_t i = 0; i < knobs.size(); ++i)
    {
        g.setColour (colours::dim);
        g.setFont (juce::FontOptions (10.5f, juce::Font::bold).withKerningFactor (0.12f));
        g.drawText (knobLabels[i], knobs[i]->getX(), knobs[i]->getY() - 14, knobs[i]->getWidth(), 14, juce::Justification::centred);
    }
    g.setColour (colours::text);
    g.setFont (juce::FontOptions (13.0f, juce::Font::bold));
    g.drawText (statusText, 18, getHeight() - 34, getWidth() - 36, 20, juce::Justification::centredLeft);
    g.setColour (colours::faint);
    g.setFont (juce::FontOptions (11.0f));
    g.drawText ("MIDI in: C1 hold, C#1 reverse, D1 fire, pitch wheel jogs", 18, getHeight() - 34, getWidth() - 36, 20, juce::Justification::centredRight);
}

void SlipEditor::resized()
{
    if (spotlight != nullptr) spotlight->setBounds (getLocalBounds());
    platter.setBounds (18, 48, 340, 340);
    const int x = 384, w = getWidth() - x - 18;
    hold.setBounds (x, 52, (w - 16) / 3, 44);
    reverse.setBounds (x + (w - 16) / 3 + 8, 52, (w - 16) / 3, 44);
    fire.setBounds (x + 2 * ((w - 16) / 3 + 8), 52, (w - 16) / 3, 44);
    jog.setBounds (x, 128, w / 2 - 6, 24);
    scratch.setBounds (x + w / 2 + 6, 128, w / 2 - 6, 24);
    for (size_t i = 0; i < menus.size(); ++i) menus[i]->setBounds (x + (int) i * ((w + 8) / 3), 184, (w + 8) / 3 - 8, 26);
    for (size_t i = 0; i < knobs.size(); ++i) knobs[i]->setBounds (x + (int) i * (w / 4), 250, w / 4, 100);
}

void SlipEditor::timerCallback()
{
    platter.refresh();
    hold.setToggleState (proc.state().getRawParameterValue ("hold")->load() > 0.5f, juce::dontSendNotification);
    reverse.setToggleState (proc.state().getRawParameterValue ("reverse")->load() > 0.5f, juce::dontSendNotification);
    const auto& st = proc.status();
    const int s = st.state.load();
    const double beats = st.lagSeconds.load() * proc.currentBpm() / 60.0;
    juce::String t;
    if (s == 0) t = "Live: playing where the track is";
    else
    {
        t = juce::String (beats, 2) + " beats behind the track, at " + juce::String (st.speed.load(), 2) + "x";
        const float until = st.untilSnapBeats.load();
        if (s == 2 && until >= 0.0f) t << "  ·  back in " << juce::String (until, 2) << " beats";
        else if (s == 1 && until >= 0.0f) t << "  ·  gesture ends in " << juce::String (until, 2) << " beats";
    }
    if (t != statusText) { statusText = t; repaint (0, getHeight() - 40, getWidth(), 40); }
}
} // namespace slip
