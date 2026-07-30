#include "PluginEditor.h"

static const juce::Colour kBg     (0xff0b0b10);
static const juce::Colour kPanel  (0xff15151d);
static const juce::Colour kPanel2 (0xff1b1b25);
static const juce::Colour kLine   (0xff26263a);
static const juce::Colour kViolet (0xff8b5cf6);
static const juce::Colour kCyan   (0xff22d3ee);
static const juce::Colour kAmber  (0xfff59e0b);
static const juce::Colour kText   (0xffe9e9f1);
static const juce::Colour kDim    (0xff8a8a9c);

UncertainMasterEditor::UncertainMasterEditor (UncertainMasterProcessor& p)
    : AudioProcessorEditor (p), proc (p)
{
    title.setText ("UNCERTAIN MASTER", juce::dontSendNotification);
    title.setColour (juce::Label::textColourId, kText);
    title.setFont (juce::FontOptions (18.f, juce::Font::bold));
    addAndMakeVisible (title);

    subtitle.setText ("RAP MODERNE - SUB & 808 - S'ADAPTE A CHAQUE SESSION",
                      juce::dontSendNotification);
    subtitle.setColour (juce::Label::textColourId, kDim);
    subtitle.setFont (juce::FontOptions (8.f));
    addAndMakeVisible (subtitle);

    for (auto* b : { &mSimple, &mAdv })
    {
        b->setColour (juce::TextButton::buttonColourId, kPanel);
        b->setColour (juce::TextButton::textColourOffId, kDim);
        b->setColour (juce::TextButton::textColourOnId, juce::Colours::white);
        b->setClickingTogglesState (false);
        addAndMakeVisible (*b);
    }
    mSimple.onClick = [this] { setMode (false); };
    mAdv.onClick    = [this] { setMode (true); };

    styleKnob (cleanK, cleanL, "CLEAN", kCyan);
    styleKnob (clipK,  clipL,  "CLIP",  kViolet);
    styleKnob (glueK,  glueL,  "GLUE",  kAmber);

    auto styleGain = [this] (juce::Slider& s, juce::Label& l, const juce::String& name)
    {
        s.setSliderStyle (juce::Slider::LinearVertical);
        s.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 48, 16);
        s.setColour (juce::Slider::trackColourId, kViolet.withAlpha (0.6f));
        s.setColour (juce::Slider::thumbColourId, kCyan);
        s.setColour (juce::Slider::textBoxTextColourId, kText);
        s.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
        addAndMakeVisible (s);
        l.setText (name, juce::dontSendNotification);
        l.setJustificationType (juce::Justification::centred);
        l.setColour (juce::Label::textColourId, kDim);
        l.setFont (juce::FontOptions (10.f, juce::Font::bold));
        addAndMakeVisible (l);
    };
    styleGain (inputS,  inL,  "IN");
    styleGain (outputS, outL, "OUT");

    auto setupPreset = [this] (juce::TextButton& b, int idx)
    {
        b.setColour (juce::TextButton::buttonColourId, kPanel);
        b.setColour (juce::TextButton::textColourOffId, kDim);
        b.setColour (juce::TextButton::textColourOnId, juce::Colours::white);
        b.setClickingTogglesState (false);
        b.onClick = [this, idx] {
            if (auto* pr = proc.apvts.getParameter ("preset"))
                pr->setValueNotifyingHost (pr->convertTo0to1 ((float) idx));
        };
        addAndMakeVisible (b);
    };
    setupPreset (pOff, 0); setupPreset (pWarm, 1);
    setupPreset (pAir, 2); setupPreset (pImpact, 3);

    auto setupClip = [this] (juce::TextButton& b, int idx)
    {
        b.setColour (juce::TextButton::buttonColourId, kPanel2);
        b.setColour (juce::TextButton::textColourOffId, kDim);
        b.setColour (juce::TextButton::textColourOnId, juce::Colours::white);
        b.setClickingTogglesState (false);
        b.onClick = [this, idx] {
            if (auto* pr = proc.apvts.getParameter ("clipchar"))
                pr->setValueNotifyingHost (pr->convertTo0to1 ((float) idx));
        };
        addAndMakeVisible (b);
    };
    setupClip (clipSoft, 0); setupClip (clipPunch, 1); setupClip (clipHard, 2);

    deltaBtn.setColour (juce::TextButton::buttonColourId, kPanel);
    deltaBtn.setColour (juce::TextButton::buttonOnColourId, kAmber);
    deltaBtn.setColour (juce::TextButton::textColourOffId, kDim);
    deltaBtn.setColour (juce::TextButton::textColourOnId, juce::Colour (0xff0b0b10));
    deltaBtn.setClickingTogglesState (true);
    addAndMakeVisible (deltaBtn);

    advInfo.setColour (juce::Label::textColourId, kDim);
    advInfo.setFont (juce::FontOptions (9.f));
    advInfo.setJustificationType (juce::Justification::centredLeft);
    addAndMakeVisible (advInfo);

    aClean = std::make_unique<SA> (proc.apvts, "clean",  cleanK);
    aClip  = std::make_unique<SA> (proc.apvts, "clip",   clipK);
    aGlue  = std::make_unique<SA> (proc.apvts, "glue",   glueK);
    aIn    = std::make_unique<SA> (proc.apvts, "input",  inputS);
    aOut   = std::make_unique<SA> (proc.apvts, "output", outputS);
    aDelta = std::make_unique<BA> (proc.apvts, "delta",  deltaBtn);

    setMode (false);
    startTimerHz (24);
}

void UncertainMasterEditor::styleKnob (juce::Slider& s, juce::Label& l,
                                       const juce::String& name, juce::Colour col)
{
    s.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    s.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 56, 18);
    s.setColour (juce::Slider::rotarySliderFillColourId, col);
    s.setColour (juce::Slider::rotarySliderOutlineColourId, kPanel.brighter (0.15f));
    s.setColour (juce::Slider::thumbColourId, juce::Colours::white);
    s.setColour (juce::Slider::textBoxTextColourId, kText);
    s.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    addAndMakeVisible (s);
    l.setText (name, juce::dontSendNotification);
    l.setJustificationType (juce::Justification::centred);
    l.setColour (juce::Label::textColourId, kDim);
    l.setFont (juce::FontOptions (13.f, juce::Font::bold));
    addAndMakeVisible (l);
}

void UncertainMasterEditor::setMode (bool adv)
{
    advanced = adv;
    for (auto* b : { &clipSoft, &clipPunch, &clipHard, &deltaBtn }) b->setVisible (adv);
    advInfo.setVisible (adv);
    setSize (kW, adv ? kHAdv : kHSimple);
    resized();
    repaint();
}

void UncertainMasterEditor::paint (juce::Graphics& g)
{
    g.fillAll (kBg);
    g.setColour (kPanel);
    g.fillRoundedRectangle (getLocalBounds().reduced (8).toFloat(), 12.f);

    mSimple.setColour (juce::TextButton::buttonColourId, advanced ? kPanel : kViolet);
    mAdv.setColour    (juce::TextButton::buttonColourId, advanced ? kViolet : kPanel);

    const int cur = (int) proc.apvts.getRawParameterValue ("preset")->load();
    juce::TextButton* pb[4] = { &pOff, &pWarm, &pAir, &pImpact };
    for (int i = 0; i < 4; ++i)
        pb[i]->setColour (juce::TextButton::buttonColourId, i == cur ? kViolet : kPanel.brighter (0.08f));

    drawMeter (g, meterArea);

    if (advanced)
    {
        const int cc = (int) proc.apvts.getRawParameterValue ("clipchar")->load();
        juce::TextButton* cb[3] = { &clipSoft, &clipPunch, &clipHard };
        for (int i = 0; i < 3; ++i)
            cb[i]->setColour (juce::TextButton::buttonColourId, i == cc ? kViolet : kPanel2);

        drawAnalyser (g, analyserArea);

        const float gr = proc.glueGrDb.load();
        advInfo.setText ("GLUE compression parallele  |  GR -" + juce::String (gr,1)
                         + " dB  |  Sub protege 120 Hz  |  Clip oversample x4",
                         juce::dontSendNotification);
    }

#if UM_TRIAL
    g.setColour (kAmber);
    g.setFont (juce::FontOptions (11.f, juce::Font::bold));
    g.drawText ("FREE VERSION - uncertain.fr", getLocalBounds().removeFromBottom (18),
                juce::Justification::centred);
#endif
}

void UncertainMasterEditor::drawMeter (juce::Graphics& g, juce::Rectangle<int> area)
{
    if (area.isEmpty()) return;
    auto m = area.toFloat();
    g.setColour (juce::Colour (0xff0e0e14));
    g.fillRoundedRectangle (m, 5.f);
    auto dbToY = [&m] (float db) {
        return juce::jmap (juce::jlimit (-30.f, 0.f, db), -30.f, 0.f, m.getBottom(), m.getY());
    };
    g.setColour (kCyan.withAlpha (0.16f));
    g.fillRect (juce::Rectangle<float> (m.getX(), dbToY (-10.f), m.getWidth(),
                                        dbToY (-12.f) - dbToY (-10.f)));
    const float lufs = proc.lufsShort.load();
    const float y = dbToY (lufs);
    const bool inZone = (lufs >= -12.5f && lufs <= -9.5f);
    g.setColour (inZone ? kCyan : kViolet);
    g.fillRoundedRectangle (m.getX()+3.f, y, m.getWidth()-6.f, m.getBottom()-y-2.f, 3.f);
    g.setColour (kDim);
    g.setFont (juce::FontOptions (9.f));
    g.drawText ("-10", (int) m.getX()-24, (int) dbToY (-10.f)-6, 22, 12, juce::Justification::right);
    g.drawText ("-12", (int) m.getX()-24, (int) dbToY (-12.f)-6, 22, 12, juce::Justification::right);
    g.drawText ("LUFS", (int) m.getX()-6, (int) m.getBottom()+3, (int) m.getWidth()+12, 12,
                juce::Justification::centred);
}

void UncertainMasterEditor::drawAnalyser (juce::Graphics& g, juce::Rectangle<int> area)
{
    if (area.isEmpty()) return;
    auto r = area.toFloat();
    g.setColour (juce::Colour (0xff0d0d13));
    g.fillRoundedRectangle (r, 10.f);
    g.setColour (kLine);
    g.drawRoundedRectangle (r.reduced (0.5f), 10.f, 1.f);

    const float W = r.getWidth(), H = r.getHeight();
    const float x0 = r.getX(), y0 = r.getY();
    auto fToX = [W,x0] (float f) { return x0 + std::log (juce::jlimit (20.f,20000.f,f)/20.f)
                                              / std::log (1000.f) * W; };

    // Spectre live
    juce::Path sp;
    const int N = UncertainMasterProcessor::kNumScopeBins;
    for (int b = 0; b < N; ++b)
    {
        const float t = (float) b / (N - 1);
        const float v = proc.scope[b].load();
        const float x = x0 + t * W;
        const float y = y0 + H - v * H * 0.9f - H * 0.05f;
        if (b == 0) sp.startNewSubPath (x, y); else sp.lineTo (x, y);
    }
    juce::ColourGradient grad (kViolet, x0, 0, kCyan, x0+W, 0, false);
    g.setGradientFill (grad);
    g.strokePath (sp, juce::PathStrokeType (2.f));

    // Marqueurs des 4 bandes CLEAN calees (frequence auto + profondeur)
    for (int b = 0; b < UncertainMasterProcessor::kNumCleanBands; ++b)
    {
        const float f  = proc.cleanHz[b].load();
        const float dB = proc.cleanDb[b].load();
        const float x  = fToX (f);
        const float act = juce::jlimit (0.f, 1.f, dB / 12.f);
        g.setColour (kCyan.withAlpha (0.3f + act * 0.7f));
        g.drawVerticalLine ((int) x, y0 + H*0.15f, y0 + H);
        g.fillEllipse (x-4.f, y0 + H*0.15f - 4.f, 8.f, 8.f);
        g.setColour (kText.withAlpha (0.7f));
        g.setFont (juce::FontOptions (9.f, juce::Font::bold));
        g.drawText (f >= 1000.f ? juce::String (f/1000.f,1)+"k" : juce::String ((int) f),
                    (int) x-24, (int) y0+2, 48, 11, juce::Justification::centred);
        g.setColour (dB > 0.2f ? kAmber : kDim);
        g.drawText (dB > 0.2f ? "-"+juce::String (dB,1) : "0",
                    (int) x-24, (int) (y0+H-13), 48, 11, juce::Justification::centred);
    }
    g.setColour (kDim);
    g.setFont (juce::FontOptions (8.f));
    g.drawText ("CLEAN - frequences calees auto sur les resonances de la session",
                (int) x0+8, (int) y0+H-26, (int) W-16, 12, juce::Justification::left);
}

void UncertainMasterEditor::resized()
{
    auto r = getLocalBounds().reduced (16);

    auto top = r.removeFromTop (32);
    auto tl = top.removeFromLeft (240);
    title.setBounds (tl.removeFromTop (20));
    subtitle.setBounds (tl);
    auto modeBox = top.removeFromRight (150);
    mSimple.setBounds (modeBox.removeFromLeft (75).reduced (2));
    mAdv.setBounds    (modeBox.reduced (2));

    r.removeFromTop (6);
    auto pr = r.removeFromTop (28);
    const int bw = pr.getWidth() / 4;
    pOff.setBounds    (pr.removeFromLeft (bw).reduced (3, 1));
    pWarm.setBounds   (pr.removeFromLeft (bw).reduced (3, 1));
    pAir.setBounds    (pr.removeFromLeft (bw).reduced (3, 1));
    pImpact.setBounds (pr.reduced (3, 1));

    r.removeFromTop (10);

    // Rangee : IN | CLEAN CLIP GLUE | OUT | meter LUFS
    auto row = r.removeFromTop (168);
    meterArea = row.removeFromRight (54).reduced (14, 6).withWidth (26);

    auto placeGain = [] (juce::Rectangle<int> a, juce::Slider& s, juce::Label& l) {
        l.setBounds (a.removeFromTop (16));
        s.setBounds (a.reduced (4, 2));
    };
    placeGain (row.removeFromLeft (52),  inputS,  inL);
    placeGain (row.removeFromRight (52), outputS, outL);

    const int kw = row.getWidth() / 3;
    auto place = [] (juce::Rectangle<int> a, juce::Slider& s, juce::Label& l) {
        l.setBounds (a.removeFromTop (18));
        s.setBounds (a.reduced (8));
    };
    place (row.removeFromLeft (kw), cleanK, cleanL);
    place (row.removeFromLeft (kw), clipK,  clipL);
    place (row,                     glueK,  glueL);

    if (! advanced) return;

    r.removeFromTop (12);
    auto clipRow = r.removeFromTop (26);
    clipSoft.setBounds  (clipRow.removeFromLeft (72).reduced (2, 0));
    clipPunch.setBounds (clipRow.removeFromLeft (72).reduced (2, 0));
    clipHard.setBounds  (clipRow.removeFromLeft (72).reduced (2, 0));
    deltaBtn.setBounds  (clipRow.removeFromRight (100).reduced (2, 0));

    r.removeFromTop (8);
    analyserArea = r.removeFromTop (120);
    r.removeFromTop (4);
    advInfo.setBounds (r.removeFromTop (14));
}
