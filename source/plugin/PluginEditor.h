#pragma once
// Slip's window: the platter on the left (drag the label to scratch, the outer ring to jog; both let
// go when you do), the gestures and the snap on the right, and a line saying where the head is.

#include "plugin/PluginProcessor.h"
#include "plugin/Spotlight.h"

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

namespace slip
{
namespace colours
{
inline const juce::Colour bg { 0xff111316 }, panel { 0xff191c20 }, raised { 0xff22262b }, line { 0xff2e3339 }, text { 0xffe8e6e1 },
    dim { 0xff8d939b }, faint { 0xff5a6068 }, accent { 0xffffb648 }, live { 0xff5fd39a }, back { 0xff6fb7ff };
}

class Platter final : public juce::Component, public juce::SettableTooltipClient
{
public:
    explicit Platter (SlipProcessor& p) : proc (p) { setRepaintsOnMouseActivity (true); }
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void refresh();

private:
    float angleAt (juce::Point<float>) const;
    SlipProcessor& proc;
    enum class Grab { none, scratch, jog } grab = Grab::none;
    float lastAngle = 0.0f;
    double shownTurns = -1.0;
    int shownState = -1;
};

class SlipEditor final : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit SlipEditor (SlipProcessor&);
    ~SlipEditor() override;
    void paint (juce::Graphics&) override;
    void resized() override;
    lvrs::Spotlight& getSpotlight() { return *spotlight; }

private:
    void timerCallback() override;
    juce::Slider& knob (const char* id, const char* tip);
    juce::ComboBox& menu (const char* id, const char* tip);
    void momentary (juce::TextButton&, const char* id, juce::Colour, const char* tip);

    SlipProcessor& proc;
    juce::LookAndFeel_V4 look;
    Platter platter;
    juce::TextButton hold { "HOLD" }, reverse { "REVERSE" }, fire { "FIRE" };
    juce::Slider jog, scratch;
    std::vector<std::unique_ptr<juce::Slider>> knobs;
    std::vector<std::unique_ptr<juce::ComboBox>> menus;
    std::vector<juce::String> knobIds, menuIds;
    std::vector<std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>> sliderAs;
    std::vector<std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment>> menuAs;
    juce::String statusText;
    juce::TooltipWindow tips { this, 600 };
    std::unique_ptr<lvrs::Spotlight> spotlight;   // shows what Key Map / MIDI Map / automation is moving
};
} // namespace slip
