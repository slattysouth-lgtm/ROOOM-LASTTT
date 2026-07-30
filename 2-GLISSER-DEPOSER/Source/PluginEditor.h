#pragma once
#include "PluginProcessor.h"

class UncertainMasterEditor : public juce::AudioProcessorEditor,
                              private juce::Timer
{
public:
    explicit UncertainMasterEditor (UncertainMasterProcessor&);
    ~UncertainMasterEditor() override = default;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override { repaint(); }
    void setMode (bool advanced);
    void styleKnob (juce::Slider&, juce::Label&, const juce::String&, juce::Colour);

    void drawMeter    (juce::Graphics&, juce::Rectangle<int>);
    void drawAnalyser (juce::Graphics&, juce::Rectangle<int>);

    UncertainMasterProcessor& proc;

    juce::Slider cleanK, clipK, glueK, inputS, outputS;
    juce::Label  title, subtitle, cleanL, clipL, glueL, inL, outL;
    juce::TextButton mSimple {"SIMPLE"}, mAdv {"ADVANCED"};
    juce::TextButton pOff {"OFF"}, pWarm {"CHALEUR"}, pAir {"AIR"}, pImpact {"IMPACT"};
    juce::TextButton clipSoft {"SOFT"}, clipPunch {"PUNCH"}, clipHard {"HARD"};
    juce::TextButton deltaBtn { juce::CharPointer_UTF8 ("\xce\x94 DELTA") };
    juce::Label      advInfo;

    using SA = juce::AudioProcessorValueTreeState::SliderAttachment;
    using BA = juce::AudioProcessorValueTreeState::ButtonAttachment;
    std::unique_ptr<SA> aClean, aClip, aGlue, aIn, aOut;
    std::unique_ptr<BA> aDelta;

    bool advanced = false;
    juce::Rectangle<int> meterArea, analyserArea;

    static constexpr int kW = 600, kHSimple = 360, kHAdv = 600;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (UncertainMasterEditor)
};
