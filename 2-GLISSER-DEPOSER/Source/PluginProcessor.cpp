#include "PluginProcessor.h"
#include "PluginEditor.h"

// Zones de recherche des 4 bandes CLEAN (grave / boxy / agressif / sifflantes)
static const float kZone[4][2] = {
    { 120.f,  400.f },
    { 400.f, 1000.f },
    { 1500.f, 4500.f },
    { 5000.f, 9000.f }
};

// ─────────────────────────────────────────────────────────────────────────
UncertainMasterProcessor::UncertainMasterProcessor()
    : AudioProcessor (BusesProperties()
        .withInput ("Input",  juce::AudioChannelSet::stereo(), true)
        .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PARAMS", createLayout())
{
    for (int b = 0; b < kNumCleanBands; ++b)
    {
        cleanDb[b].store (0.f);
        cleanHz[b].store (std::sqrt (kZone[b][0]*kZone[b][1]));
    }
    for (int i = 0; i < kNumScopeBins; ++i) scope[i].store (0.f);
}

juce::AudioProcessorValueTreeState::ParameterLayout UncertainMasterProcessor::createLayout()
{
    using F = juce::AudioParameterFloat;
    using C = juce::AudioParameterChoice;
    using B = juce::AudioParameterBool;
    juce::AudioProcessorValueTreeState::ParameterLayout l;

    l.add (std::make_unique<F>(juce::ParameterID{"input",1}, "Input",
            juce::NormalisableRange<float>(-18.f,18.f,0.1f), 0.f));
    l.add (std::make_unique<F>(juce::ParameterID{"clean",1}, "Clean",
            juce::NormalisableRange<float>(0.f,100.f,0.1f), 0.f));
    l.add (std::make_unique<F>(juce::ParameterID{"clip",1}, "Clip",
            juce::NormalisableRange<float>(0.f,100.f,0.1f), 0.f));
    l.add (std::make_unique<F>(juce::ParameterID{"glue",1}, "Glue",
            juce::NormalisableRange<float>(0.f,100.f,0.1f), 0.f));
    l.add (std::make_unique<F>(juce::ParameterID{"output",1}, "Output",
            juce::NormalisableRange<float>(-18.f,6.f,0.1f), -1.f));
    l.add (std::make_unique<C>(juce::ParameterID{"preset",1}, "Color",
            juce::StringArray{"OFF","CHALEUR","AIR","IMPACT"}, 0));
    l.add (std::make_unique<C>(juce::ParameterID{"clipchar",1}, "Clip Char",
            juce::StringArray{"SOFT","PUNCH","HARD"}, 0));
    l.add (std::make_unique<B>(juce::ParameterID{"delta",1}, "Delta", false));
    return l;
}

// ─────────────────────────────────────────────────────────────────────────
void UncertainMasterProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    sr = sampleRate;

    for (int b = 0; b < kNumCleanBands; ++b)
        bands[b].prepare (sr, kZone[b][0], kZone[b][1]);

    for (int c = 0; c < 2; ++c)
    {
        colA[c].setIdentity(); colA[c].reset();
        colB[c].setIdentity(); colB[c].reset();
    }
    lastPreset = -1; lastAirDb = -99.f;
    airGuardDet.setBandpass (sr, 7000.f, 1.2f); airGuardDet.reset();
    airGuardEnv.set (sr, 10.f, 300.f);

    glueHP.setHighpass (sr, 150.f, 0.707f); glueHP.reset();
    glueFast.set (sr, 8.f,  120.f);
    glueSlow.set (sr, 80.f, 350.f);
    glueRms .set (sr, 400.f, 400.f);
    glueWet.setSize (2, samplesPerBlock);
    glueMixSm.reset (sr, 0.05);
    glueMixSm.setCurrentAndTargetValue (0.f);

    juce::dsp::ProcessSpec spec { sr, (juce::uint32) samplesPerBlock, 2 };
    lrLow .prepare (spec); lrLow .setType (juce::dsp::LinkwitzRileyFilterType::lowpass);  lrLow .setCutoffFrequency (120.f);
    lrHigh.prepare (spec); lrHigh.setType (juce::dsp::LinkwitzRileyFilterType::highpass); lrHigh.setCutoffFrequency (120.f);

    os = std::make_unique<juce::dsp::Oversampling<float>> (
            2, 2, juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR, true, true);
    os->initProcessing ((size_t) samplesPerBlock);
    const int lat = (int) std::lround (os->getLatencyInSamples());

    subDelay.prepare (spec);
    subDelay.setMaximumDelayInSamples (juce::jmax (16, lat + 8));
    subDelay.setDelay ((float) lat);
    setLatencySamples (lat);

    lowBuf .setSize (2, samplesPerBlock);
    highBuf.setSize (2, samplesPerBlock);
    dryBuf .setSize (2, samplesPerBlock);

    crestPeak.set (sr, 0.3f, 300.f);
    crestRms .set (sr, 60.f, 300.f);

    tpGain = 1.f;
    outSm.reset (sr, 0.05);
    outSm.setCurrentAndTargetValue (1.f);
    inSm.reset (sr, 0.05);
    inSm.setCurrentAndTargetValue (1.f);

    kHP   .setHighpass  (sr, 60.f, 0.707f); kHP.reset();
    kShelf.setHighShelf (sr, 1680.f, 4.f);  kShelf.reset();
    lufsMs = 0.f;
    lufsCoef = 1.f - std::exp (-1.f / (3.f * (float) sr));

    fifo.fill (0.f); fftBuf.fill (0.f); mag.fill (0.f); scopeSmooth.fill (0.f);
    fifoIdx = 0;

#if UM_TRIAL
    trialCtr = 0; trialGain = 1.f;
#endif
}

// ─────────────────────────────────────────────────────────────────────────
float UncertainMasterProcessor::peakFreqInRange (float fa, float fb) const
{
    const int k0 = juce::jlimit (1, fftSize/2-1, (int) (fa / (float) sr * fftSize));
    const int k1 = juce::jlimit (k0+1, fftSize/2, (int) (fb / (float) sr * fftSize));
    int   kMax = k0; float vMax = 0.f;
    for (int k = k0; k < k1; ++k)
        if (mag[(size_t) k] > vMax) { vMax = mag[(size_t) k]; kMax = k; }
    return (float) kMax * (float) sr / (float) fftSize;
}

void UncertainMasterProcessor::pushToScope (float s)
{
    fifo[(size_t) fifoIdx] = s;
    if (++fifoIdx >= fftSize)
    {
        fifoIdx = 0;
        std::copy (fifo.begin(), fifo.end(), fftBuf.begin());
        std::fill (fftBuf.begin() + fftSize, fftBuf.end(), 0.f);
        win.multiplyWithWindowingTable (fftBuf.data(), (size_t) fftSize);
        fft.performFrequencyOnlyForwardTransform (fftBuf.data());
        for (int k = 0; k < fftSize/2; ++k) mag[(size_t) k] = fftBuf[(size_t) k];

        for (int b = 0; b < kNumScopeBins; ++b)
        {
            const float t0 = (float) b       / kNumScopeBins;
            const float t1 = (float)(b + 1)  / kNumScopeBins;
            const float f0 = 20.f * std::pow (1000.f, t0);
            const float f1 = 20.f * std::pow (1000.f, t1);
            int k0 = juce::jlimit (1, fftSize/2 - 1, (int) (f0 / (float) sr * fftSize));
            int k1 = juce::jlimit (k0 + 1, fftSize/2, (int) (f1 / (float) sr * fftSize));
            float mx = 0.f;
            for (int k = k0; k < k1; ++k) mx = juce::jmax (mx, mag[(size_t) k]);
            const float db = juce::Decibels::gainToDecibels (mx / (float) fftSize + 1.0e-9f);
            const float norm = juce::jlimit (0.f, 1.f, (db + 90.f) / 90.f);
            scopeSmooth[(size_t) b] += 0.4f * (norm - scopeSmooth[(size_t) b]);
            scope[(size_t) b].store (scopeSmooth[(size_t) b]);
        }
    }
}

// ─────────────────────────────────────────────────────────────────────────
void UncertainMasterProcessor::processBlock (juce::AudioBuffer<float>& buf, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals nd;
    const int n = buf.getNumSamples();
    if (n == 0) return;
    lowBuf .setSize (2, n, false, false, true);
    highBuf.setSize (2, n, false, false, true);
    dryBuf .setSize (2, n, false, false, true);
    glueWet.setSize (2, n, false, false, true);

    const float inDb   = apvts.getRawParameterValue("input")->load();
    const float clean  = apvts.getRawParameterValue("clean")->load()  * 0.01f;
    const float clip   = apvts.getRawParameterValue("clip")->load()   * 0.01f;
    const float glue   = apvts.getRawParameterValue("glue")->load()   * 0.01f;
    const float outDb  = apvts.getRawParameterValue("output")->load();
    const int   preset = (int) apvts.getRawParameterValue("preset")->load();
    const int   clipCh = (int) apvts.getRawParameterValue("clipchar")->load();
    const bool  delta  = apvts.getRawParameterValue("delta")->load() > 0.5f;

    float* L = buf.getWritePointer (0);
    float* R = buf.getWritePointer (buf.getNumChannels() > 1 ? 1 : 0);

    // ── 0. Gain d'entree ─────────────────────────────────────────────────
    inSm.setTargetValue (juce::Decibels::decibelsToGain (inDb));
    for (int s = 0; s < n; ++s)
    {
        const float gi = inSm.getNextValue();
        L[s] *= gi; R[s] *= gi;
    }

    // ── Cale les frequences CLEAN sur les pics de chaque zone (par session) ─
    for (int b = 0; b < kNumCleanBands; ++b)
    {
        const float target = peakFreqInRange (bands[b].zoneMin, bands[b].zoneMax);
        bands[b].freq += 0.02f * (target - bands[b].freq);   // lissage : se fixe en qq sec
        bands[b].retune (sr);
        cleanHz[b].store (bands[b].freq);
    }

    // ── 1. Analyse CLEAN + scope ─────────────────────────────────────────
    for (int s = 0; s < n; ++s)
    {
        const float mono = 0.5f * (L[s] + R[s]);
        pushToScope (mono);
        for (int b = 0; b < kNumCleanBands; ++b)
        {
            const float d = bands[b].det.process (mono);
            bands[b].fast.process (d);
            bands[b].slow.process (d);
        }
        airGuardEnv.process (airGuardDet.process (mono));
    }

    const float depthMax = 2.f + 10.f * clean;   // fond -2 dB, potard jusqu'a -12 dB
    for (int b = 0; b < kNumCleanBands; ++b)
    {
        auto& bd = bands[b];
        const float excessDb = 20.f * std::log10 (juce::jmax (1.0e-9f, bd.fast.v)
                                                / juce::jmax (1.0e-9f, bd.slow.v));
        // bande agressive (2-4.5k) traitee un peu plus tot
        const float thr = (b == 2 ? 3.f : 4.f);
        const float target = excessDb > thr
            ? juce::jmin (depthMax, (excessDb - thr) * 0.9f) : 0.f;
        bd.cutDb += 0.25f * (target - bd.cutDb);
        cleanDb[b].store (bd.cutDb);
        if (bd.cutDb > 0.05f)
            for (int c = 0; c < 2; ++c) bd.cut[c].setPeaking (sr, bd.freq, bd.q, -bd.cutDb);
        else
            for (int c = 0; c < 2; ++c) bd.cut[c].setIdentity();
    }

    // Sauvegarde du signal AVANT clean (pour DELTA)
    dryBuf.copyFrom (0, 0, buf, 0, 0, n);
    dryBuf.copyFrom (1, 0, buf, buf.getNumChannels() > 1 ? 1 : 0, 0, n);

    // ── 2. Couleur (preset) ─────────────────────────────────────────────
    if (preset != lastPreset)
    {
        lastPreset = preset;
        for (int c = 0; c < 2; ++c)
        {
            switch (preset)
            {
                case 1: colA[c].setLowShelf  (sr, 120.f,   2.0f);
                        colB[c].setHighShelf (sr, 8000.f, -1.0f); break;
                case 2: colB[c].setIdentity(); break;
                default: colA[c].setIdentity(); colB[c].setIdentity(); break;
            }
        }
        lastAirDb = -99.f;
    }
    if (preset == 2)
    {
        const float guard = juce::jlimit (0.f, 1.f, airGuardEnv.v * 10.f);
        const float airDb = 2.5f * (1.f - guard);
        if (std::fabs (airDb - lastAirDb) > 0.1f)
        {
            lastAirDb = airDb;
            for (int c = 0; c < 2; ++c) colA[c].setHighShelf (sr, 10000.f, airDb);
        }
    }

    // ── 3. Application CLEAN + couleur ───────────────────────────────────
    for (int c = 0; c < 2; ++c)
    {
        float* ch = (c == 0 ? L : R);
        for (int s = 0; s < n; ++s)
        {
            float v = ch[s];
            for (int b = 0; b < kNumCleanBands; ++b) v = bands[b].cut[c].process (v);
            v = colA[c].process (v);
            v = colB[c].process (v);
            ch[s] = v;
        }
    }

    // ── DELTA : sortir uniquement ce que le CLEAN retire ────────────────
    if (delta)
    {
        for (int c = 0; c < 2; ++c)
        {
            const float* dry = dryBuf.getReadPointer (c);
            float* ch = (c == 0 ? L : R);
            for (int s = 0; s < n; ++s) ch[s] = (dry[s] - ch[s]) * 4.f;
        }
        return;
    }

    // ── 4. GLUE : compression PARALLELE (remonte le faible, zero pompe) ──
    //     wet = copie compressee (detecteur HPF 150 Hz => sub ignore),
    //     out = sec + mix*wet. Le chemin sec garde les transients.
    glueMixSm.setTargetValue (glue * 0.6f);   // jusqu'a 60% de parallele
    {
        glueWet.copyFrom (0, 0, buf, 0, 0, n);
        glueWet.copyFrom (1, 0, buf, 1, 0, n);
        float* wl = glueWet.getWritePointer (0);
        float* wr = glueWet.getWritePointer (1);
        const float ratio = 3.0f, ratioExp = 1.f - 1.f/ratio;
        const float makeup = 1.8f;             // gain de rattrapage (remonte le faible)
        float grMeter = 0.f;

        for (int s = 0; s < n; ++s)
        {
            const float mono = 0.5f * (wl[s] + wr[s]);
            const float det  = glueHP.process (mono);      // sub exclu de la detection
            const float e    = glueFast.process (det);
            glueSlow.process (det);
            const float rms  = glueRms.process (det);
            const float thr  = juce::jmax (1.0e-5f, rms * 0.9f);
            const float ce   = juce::jmax (e, glueSlow.v);

            float g = 1.f;
            if (ce > thr) g = std::pow (thr / ce, ratioExp);
            g = juce::jmax (g, 0.35f);          // GR limitee (evite l'ecrasement)

            wl[s] = wl[s] * g * makeup;
            wr[s] = wr[s] * g * makeup;
            grMeter = juce::jmin (grMeter, 20.f * std::log10 (g));
        }
        glueGrDb.store (-grMeter);

        for (int c = 0; c < 2; ++c)
        {
            float* ch = (c == 0 ? L : R);
            const float* w = glueWet.getReadPointer (c);
            for (int s = 0; s < n; ++s)
            {
                const float mix = glueMixSm.getNextValue();
                ch[s] = ch[s] * (1.f - mix * 0.5f) + w[s] * mix;
            }
        }
    }

    // ── 5. Split 120 Hz : sub protege / haut clippe+sature oversample x4 ─
    for (int c = 0; c < 2; ++c)
    {
        lowBuf .copyFrom (c, 0, buf, c, 0, n);
        highBuf.copyFrom (c, 0, buf, c, 0, n);
    }
    {
        juce::dsp::AudioBlock<float> lb (lowBuf);
        juce::dsp::ProcessContextReplacing<float> lc (lb); lrLow.process (lc);
        juce::dsp::AudioBlock<float> hb (highBuf);
        juce::dsp::ProcessContextReplacing<float> hc (hb); lrHigh.process (hc);
    }

    for (int s = 0; s < n; ++s)
    {
        const float m = 0.5f * (highBuf.getSample (0, s) + highBuf.getSample (1, s));
        crestPeak.process (m);
        crestRms .process (m);
    }
    const float crestDb = 20.f * std::log10 (juce::jmax (1.0e-9f, crestPeak.v)
                                           / juce::jmax (1.0e-9f, crestRms.v));
    const float maxDriveDb = juce::jmap (juce::jlimit (6.f, 18.f, crestDb), 6.f, 18.f, 5.f, 14.f);
    float effDriveDb = clip * maxDriveDb;
    if (preset == 3) effDriveDb *= 1.15f;

    {
        const float pre  = juce::Decibels::decibelsToGain (effDriveDb);
        const float post = juce::Decibels::decibelsToGain (-effDriveDb * 0.82f);
        const float satAmt = 0.15f + clip * 0.5f;   // saturation qui monte avec le potard
        juce::dsp::AudioBlock<float> hb (highBuf);
        auto ov = os->processSamplesUp (hb);
        for (size_t c = 0; c < ov.getNumChannels(); ++c)
        {
            float* d = ov.getChannelPointer (c);
            for (size_t s = 0; s < ov.getNumSamples(); ++s)
            {
                float x = d[s] * pre;
                // etage 1 : saturation douce (tape-like)
                x = std::tanh (x * (1.f + satAmt)) / (1.f + satAmt * 0.6f);
                // etage 2 : clip selon caractere
                float y;
                if (clipCh == 0)       y = juce::jlimit (-1.f, 1.f, x);
                else if (clipCh == 1)  y = std::tanh (1.25f * juce::jlimit (-1.5f,1.5f,x)) * 1.09f;
                else                   y = juce::jlimit (-0.985f, 0.985f, x * 1.3f);
                d[s] = y * post;
            }
        }
        os->processSamplesDown (hb);
    }

    // Sub : alignement + saturation tres douce (garde le poids 808)
    {
        const float subDriveDb = effDriveDb * 0.30f;
        const float pre  = juce::Decibels::decibelsToGain (subDriveDb);
        const float post = juce::Decibels::decibelsToGain (-subDriveDb * 0.9f);
        for (int c = 0; c < 2; ++c)
        {
            float* d = lowBuf.getWritePointer (c);
            for (int s = 0; s < n; ++s)
            {
                subDelay.pushSample (c, d[s]);
                const float v = subDelay.popSample (c);
                d[s] = std::tanh (v * pre) * post;
            }
        }
    }

    for (int c = 0; c < 2; ++c)
    {
        float* ch = (c == 0 ? L : R);
        for (int s = 0; s < n; ++s)
            ch[s] = lowBuf.getSample (c, s) + highBuf.getSample (c, s);
    }

    // ── 6. Sortie + limiteur true-peak + LUFS + trial ───────────────────
    outSm.setTargetValue (juce::Decibels::decibelsToGain (outDb));
    const float tpCeil = 0.89f;

    for (int s = 0; s < n; ++s)
    {
        const float g = outSm.getNextValue();
        for (int c = 0; c < 2; ++c)
        {
            float* ch = (c == 0 ? L : R);
            float v = ch[s] * g;
            const float a = std::fabs (v);
            if (a > tpCeil) { const float gg = tpCeil / a; if (gg < tpGain) tpGain = gg; }
            tpGain += (1.f - tpGain) * 0.0009f;
            v = juce::jlimit (-tpCeil, tpCeil, v * tpGain);

#if UM_TRIAL
            if (c == 0)
            {
                ++trialCtr;
                const int period = (int) (60.0 * sr);
                const int muteAt = (int) (58.0 * sr);
                if (trialCtr >= period) trialCtr = 0;
                const float tgt = (trialCtr > muteAt) ? 0.f : 1.f;
                trialGain += (tgt - trialGain) * 0.0005f;
            }
            v *= trialGain;
#endif
            ch[s] = v;
        }
        const float km = kShelf.process (kHP.process (0.5f * (L[s] + R[s])));
        lufsMs += lufsCoef * (km * km - lufsMs);
    }
    lufsShort.store (-0.691f + 10.f * std::log10 (juce::jmax (1.0e-10f, lufsMs)));
}

// ─────────────────────────────────────────────────────────────────────────
void UncertainMasterProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary (*xml, dest);
}

void UncertainMasterProcessor::setStateInformation (const void* data, int size)
{
    if (auto xml = getXmlFromBinary (data, size))
        apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessorEditor* UncertainMasterProcessor::createEditor()
{
    return new UncertainMasterEditor (*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new UncertainMasterProcessor();
}
