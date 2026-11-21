// The test runner: JUCE initialised once (the plug-in tests make editors and use the message thread).
#include <juce_events/juce_events.h>

#include <catch2/catch_session.hpp>

int main (int argc, char* argv[])
{
    juce::ScopedJuceInitialiser_GUI gui;
    return Catch::Session().run (argc, argv);
}
