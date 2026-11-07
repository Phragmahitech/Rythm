#include "PluginProcessor.h"
#include "PluginEditor.h"

using P = RythmAudioProcessor;

//==============================================================================
/** Un paramètre d'automation relayé vers le paramètre de même rang de l'instrument chargé. */
class RythmAudioProcessor::Proxy final : public juce::HostedAudioProcessorParameter
{
public:
    explicit Proxy (int i) : juce::HostedAudioProcessorParameter (1), index (i) {}

    juce::String getParameterID() const override        { return "p" + juce::String (index + 1); }
    float getValue() const override                     { return value.load(); }

    /** Appelé par l'hôte (souvent sur le fil audio) : appliqué à l'instrument au prochain bloc. */
    void setValue (float v) override
    {
        value.store (v);
        pending.store (true);
    }

    float getDefaultValue() const override              { return defaultValue.load(); }

    juce::String getName (int maximumStringLength) const override
    {
        const juce::ScopedLock sl (textLock);
        return (name.isNotEmpty() ? name : "P" + juce::String (index + 1)).substring (0, maximumStringLength);
    }

    juce::String getLabel() const override
    {
        const juce::ScopedLock sl (textLock);
        return label;
    }

    juce::String getText (float v, int maximumStringLength) const override
    {
        const juce::ScopedLock sl (textLock);
        if (target != nullptr)
            return target->getText (v, maximumStringLength);
        return juce::String (v, 2).substring (0, maximumStringLength);
    }

    float getValueForText (const juce::String& text) const override
    {
        const juce::ScopedLock sl (textLock);
        if (target != nullptr)
            return target->getValueForText (text);
        return juce::jlimit (0.0f, 1.0f, text.getFloatValue());
    }

    int getNumSteps() const override                    { return steps.load(); }
    bool isBound() const                                { const juce::ScopedLock sl (textLock); return target != nullptr; }

    void bind (juce::AudioProcessorParameter* p)
    {
        const juce::ScopedLock sl (textLock);
        target = p;
        name = p != nullptr ? p->getName (64) : juce::String();
        label = p != nullptr ? p->getLabel() : juce::String();
        if (p != nullptr)
        {
            value.store (p->getValue());
            defaultValue.store (p->getDefaultValue());
            steps.store (p->getNumSteps());
        }
        else
        {
            steps.store (AudioProcessor::getDefaultNumParameterSteps());
        }
        pending.store (false);
    }

    void apply() noexcept
    {
        if (pending.exchange (false) && target != nullptr)
            target->setValue (value.load());
    }

    const int index;
    std::atomic<float> value { 0.0f };

private:
    std::atomic<float> defaultValue { 0.0f };
    std::atomic<int> steps { AudioProcessor::getDefaultNumParameterSteps() };
    std::atomic<bool> pending { false };
    juce::CriticalSection textLock;
    juce::AudioProcessorParameter* target = nullptr;
    juce::String name, label;
};

//==============================================================================
namespace
{
    // Motif par défaut du séquenceur : basse roulante (on évite le kick sur chaque temps)
    bool defaultStep (int i)
    {
        return (i % 4) != 0 && i < 16;
    }

    juce::String percentText (float v, int)     { return juce::String (juce::roundToInt (v)) + " %"; }

    const char* noteNames[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
}

juce::AudioProcessorValueTreeState::ParameterLayout RythmAudioProcessor::createParameterLayout (RythmAudioProcessor& self)
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout layout;

    auto percent = [&layout] (const char* id, const char* name, float def)
    {
        layout.add (std::make_unique<AudioParameterFloat> (ParameterID { id, 1 }, name, NormalisableRange<float> (0.0f, 100.0f, 0.1f), def,
                                                           AudioParameterFloatAttributes().withStringFromValueFunction (percentText).withLabel ("%")));
    };
    auto choice = [&layout] (const char* id, const char* name, StringArray items, int def)
    {
        layout.add (std::make_unique<AudioParameterChoice> (ParameterID { id, 1 }, name, items, def));
    };

    choice (modeId, "Mode", { "RHYTHM", "MELODY" }, 0);

    // Rythme
    choice (rateId, "Rate", { "1/4", "1/8", "1/16", "1/32", "1/64" }, 2);
    choice (feelId, "Feel", { "STRAIGHT", "TRIPLET", "DOTTED" }, 0);
    choice (sourceId, "Source", { "DENSITY", "STEPS" }, 0);
    percent (densityId, "Density", 75.0f);
    percent (fillId, "Fill", 0.0f);
    percent (shuffleId, "Shuffle", 0.0f);
    percent (humanId, "Human", 0.0f);
    percent (glitchId, "Glitch", 0.0f);
    percent (groove2Id, "Groove x2", 0.0f);
    percent (groove4Id, "Groove x4", 0.0f);
    percent (splitId, "Groove /2", 0.0f);
    layout.add (std::make_unique<AudioParameterInt> (ParameterID { numStepsId, 1 }, "Steps", 1, rythm::maxSteps, 16));
    for (int i = 0; i < rythm::maxSteps; ++i)
        layout.add (std::make_unique<AudioParameterBool> (ParameterID { stepId (i), 1 }, "Step " + String (i + 1), defaultStep (i)));

    // Dynamique
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { gateId, 1 }, "Gate", NormalisableRange<float> (5.0f, 100.0f, 0.1f), 60.0f,
                                                       AudioParameterFloatAttributes().withStringFromValueFunction (percentText).withLabel ("%")));
    percent (gateRandomId, "Gate Random", 0.0f);
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { velocityId, 1 }, "Velocity", NormalisableRange<float> (1.0f, 127.0f, 1.0f), 100.0f,
                                                       AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return String (roundToInt (v)); })));
    percent (velocityRandomId, "Velocity Random", 0.0f);
    percent (accentId, "Accent", 0.0f);

    // Mélodie
    StringArray scales;
    for (int s = 0; s < rythm::numScales; ++s)
        scales.add (rythm::scaleName (s));
    choice (scaleId, "Scale", scales, rythm::phrygian);
    {
        const auto phrygianMask = rythm::scaleMask (rythm::phrygian);
        for (int k = 1; k < 12; ++k)
            layout.add (std::make_unique<AudioParameterBool> (ParameterID { keyId (k), 1 }, "Custom " + String (noteNames[k]) + " (+" + String (k) + ")",
                                                              ((phrygianMask >> k) & 1u) != 0));
    }
    percent (chanceId, "Chance", 60.0f);
    layout.add (std::make_unique<AudioParameterInt> (ParameterID { rangeId, 1 }, "Range", 1, 24, 7));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { directionId, 1 }, "Direction", NormalisableRange<float> (-100.0f, 100.0f, 0.1f), 0.0f,
                                                       AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int)
                                                       {
                                                           const int i = roundToInt (v);
                                                           return i == 0 ? String ("BOTH") : i > 0 ? "UP " + String (i) : "DOWN " + String (-i);
                                                       })));
    percent (walkId, "Walk", 30.0f);
    percent (octaveId, "Octave", 10.0f);
    percent (slideId, "Slide", 0.0f);
    percent (stackId, "Stack", 0.0f);
    {
        StringArray stacks;
        for (int s = 0; s < rythm::numStacks; ++s)
            stacks.add (rythm::stackName (s));
        choice (stackTypeId, "Stack Type", stacks, rythm::stackTriad);
    }

    // Bend
    percent (bendId, "Bend", 0.0f);
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { bendDepthId, 1 }, "Bend Depth", NormalisableRange<float> (-100.0f, 100.0f, 0.1f), -50.0f,
                                                       AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int)
                                                       { const int i = roundToInt (v); return (i > 0 ? "+" : "") + String (i) + " %"; })));
    choice (bendShapeId, "Bend Shape", { "DIVE", "SCOOP", "WOBBLE", "RANDOM" }, 0);
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { bendCyclesId, 1 }, "Bend Rate", NormalisableRange<float> (1.0f, 8.0f, 0.5f), 2.0f,
                                                       AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return String (v, 1) + "x"; })));

    // Modulations (CC ou paramètres de l'instrument chargé)
    for (int m = 0; m < rythm::numMods; ++m)
    {
        auto* selfPtr = &self;
        layout.add (std::make_unique<AudioParameterInt> (ParameterID { modId (m, "Dest"), 1 }, "Mod " + String (m + 1) + " Dest", 0, modDestCount - 1, 0,
                                                         AudioParameterIntAttributes().withStringFromValueFunction ([selfPtr] (int v, int) { return selfPtr->modDestName (v); })));
        layout.add (std::make_unique<AudioParameterFloat> (ParameterID { modId (m, "Min"), 1 }, "Mod " + String (m + 1) + " Min", NormalisableRange<float> (0.0f, 100.0f, 0.1f), 0.0f,
                                                           AudioParameterFloatAttributes().withStringFromValueFunction (percentText)));
        layout.add (std::make_unique<AudioParameterFloat> (ParameterID { modId (m, "Max"), 1 }, "Mod " + String (m + 1) + " Max", NormalisableRange<float> (0.0f, 100.0f, 0.1f), 100.0f,
                                                           AudioParameterFloatAttributes().withStringFromValueFunction (percentText)));
        layout.add (std::make_unique<AudioParameterFloat> (ParameterID { modId (m, "Chance"), 1 }, "Mod " + String (m + 1) + " Chance", NormalisableRange<float> (0.0f, 100.0f, 0.1f), 50.0f,
                                                           AudioParameterFloatAttributes().withStringFromValueFunction (percentText)));
        layout.add (std::make_unique<AudioParameterFloat> (ParameterID { modId (m, "Smooth"), 1 }, "Mod " + String (m + 1) + " Smooth", NormalisableRange<float> (0.0f, 100.0f, 0.1f), 0.0f,
                                                           AudioParameterFloatAttributes().withStringFromValueFunction (percentText)));
    }

    // Boucle
    layout.add (std::make_unique<AudioParameterBool> (ParameterID { loopId, 1 }, "Loop", false));
    choice (loopLengthId, "Loop Length", loopLengthNames(), 2);

    return layout;
}

juce::String RythmAudioProcessor::modDestName (int dest) const
{
    if (dest <= 0)
        return "OFF";
    if (dest < 128)
    {
        juce::String name = "CC " + juce::String (dest);
        switch (dest)
        {
            case 1:  name << " MOD WHEEL"; break;
            case 7:  name << " VOLUME"; break;
            case 10: name << " PAN"; break;
            case 11: name << " EXPRESSION"; break;
            case 71: name << " RESONANCE"; break;
            case 74: name << " CUTOFF"; break;
            default: break;
        }
        return name;
    }
    const int index = dest - 128;
    if (juce::isPositiveAndBelow (index, (int) proxies.size()) && proxies[(size_t) index]->isBound())
        return "SYNTH: " + proxies[(size_t) index]->getName (40);
    return "SYNTH P" + juce::String (index + 1);
}

//==============================================================================
// Points de départ
int RythmAudioProcessor::getNumFactoryPresets()             { return 11; }

juce::String RythmAudioProcessor::getFactoryPresetName (int i)
{
    const char* names[] = { "DEFAULT", "PSY ROLL", "HITECH STUTTER", "TRIPLET GATES", "PHRYGIAN LEAD", "ACID LINE",
                            "MAJOR ARP", "DARK HARMONIC", "CHORD STABS", "WOBBLE BEND", "HIRAJOSHI DRIFT" };
    return juce::isPositiveAndBelow (i, getNumFactoryPresets()) ? names[i] : "DEFAULT";
}

std::vector<std::pair<juce::String, float>> RythmAudioProcessor::factoryValues (int i)
{
    using V = std::vector<std::pair<juce::String, float>>;
    switch (i)
    {
        case 1:  return V { { modeId, 0 }, { sourceId, 1 }, { gateId, 55 }, { velocityId, 110 }, { velocityRandomId, 15 }, { accentId, 10 } };
        case 2:  return V { { modeId, 0 }, { rateId, 3 }, { densityId, 80 }, { glitchId, 25 }, { splitId, 20 }, { gateId, 40 }, { accentId, 20 } };
        case 3:  return V { { modeId, 0 }, { feelId, 1 }, { densityId, 70 }, { groove2Id, 15 }, { gateId, 45 }, { velocityRandomId, 20 } };
        case 4:  return V { { modeId, 1 }, { scaleId, rythm::phrygian }, { densityId, 85 }, { chanceId, 70 }, { walkId, 60 }, { rangeId, 7 },
                            { octaveId, 15 }, { slideId, 10 }, { gateId, 70 }, { accentId, 15 } };
        case 5:  return V { { modeId, 1 }, { scaleId, rythm::acid }, { densityId, 85 }, { chanceId, 65 }, { slideId, 25 }, { accentId, 35 },
                            { octaveId, 35 }, { gateId, 60 }, { groove2Id, 10 }, { walkId, 20 } };
        case 6:  return V { { modeId, 1 }, { scaleId, rythm::major }, { densityId, 100 }, { walkId, 80 }, { directionId, 40 }, { rangeId, 10 },
                            { gateId, 50 }, { chanceId, 85 }, { octaveId, 5 } };
        case 7:  return V { { modeId, 1 }, { scaleId, rythm::harmonicMinor }, { densityId, 80 }, { splitId, 30 }, { chanceId, 75 }, { rangeId, 9 },
                            { octaveId, 20 }, { stackId, 10 }, { stackTypeId, rythm::stack5th }, { gateId, 55 } };
        case 8:  return V { { modeId, 1 }, { scaleId, rythm::minor }, { rateId, 1 }, { densityId, 60 }, { chanceId, 50 }, { stackId, 100 },
                            { stackTypeId, rythm::stackTriad }, { gateId, 35 }, { groove2Id, 20 }, { walkId, 40 } };
        case 9:  return V { { modeId, 0 }, { rateId, 1 }, { densityId, 90 }, { bendId, 60 }, { bendShapeId, rythm::bendWobble }, { bendDepthId, 35 },
                            { bendCyclesId, 2 }, { gateId, 85 } };
        case 10: return V { { modeId, 1 }, { scaleId, rythm::hirajoshi }, { densityId, 65 }, { walkId, 70 }, { humanId, 30 }, { groove2Id, 20 },
                            { octaveId, 20 }, { gateId, 65 }, { chanceId, 70 } };
        default: return {};
    }
}

void RythmAudioProcessor::applyFactory (int index)
{
    const auto values = factoryValues (index);
    for (auto* p : getParameters())
        if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (p); ranged != nullptr && presets.isStored (*ranged))
        {
            float normalised = ranged->getDefaultValue();
            for (auto& [id, plain] : values)
                if (id == ranged->getParameterID())
                    normalised = ranged->convertTo0to1 (plain);
            ranged->beginChangeGesture();
            ranged->setValueNotifyingHost (normalised);
            ranged->endChangeGesture();
        }
}

int RythmAudioProcessor::matchingFactory() const
{
    for (int i = 0; i < getNumFactoryPresets(); ++i)
    {
        const auto values = factoryValues (i);
        bool same = true;
        for (auto* p : getParameters())
            if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (p); same && ranged != nullptr && presets.isStored (*ranged))
            {
                float normalised = ranged->getDefaultValue();
                for (auto& [id, plain] : values)
                    if (id == ranged->getParameterID())
                        normalised = ranged->convertTo0to1 (plain);
                same = std::abs (ranged->getValue() - normalised) < 1.0e-4f;
            }
        if (same)
            return i;
    }
    return -1;
}

PresetLibrary::Factory RythmAudioProcessor::makeFactory()
{
    return { [] { return getNumFactoryPresets(); },
             [] (int i) { return getFactoryPresetName (i); },
             [this] (int i) { applyFactory (i); },
             [this] { return matchingFactory(); } };
}

//==============================================================================
RythmAudioProcessor::RythmAudioProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "Rythm", createParameterLayout (*this)),
      presets (apvts, "Rythm", ".rytpreset", makeFactory(), transientParameters(), "UNTITLED")
{
    auto raw = [this] (const juce::String& id) { return Raw { apvts.getRawParameterValue (id) }; };
    mode = raw (modeId); rate = raw (rateId); feel = raw (feelId); source = raw (sourceId);
    density = raw (densityId); fill = raw (fillId); shuffle = raw (shuffleId); human = raw (humanId); glitch = raw (glitchId);
    groove2 = raw (groove2Id); groove4 = raw (groove4Id); split = raw (splitId); numSteps = raw (numStepsId);
    gate = raw (gateId); gateRandom = raw (gateRandomId); velocity = raw (velocityId); velocityRandom = raw (velocityRandomId); accent = raw (accentId);
    scale = raw (scaleId); chance = raw (chanceId); range = raw (rangeId); direction = raw (directionId); walk = raw (walkId);
    octave = raw (octaveId); slide = raw (slideId); stack = raw (stackId); stackType = raw (stackTypeId);
    bend = raw (bendId); bendDepth = raw (bendDepthId); bendShape = raw (bendShapeId); bendCycles = raw (bendCyclesId);
    loop = raw (loopId); loopLength = raw (loopLengthId);
    for (int i = 0; i < rythm::maxSteps; ++i)
        steps[(size_t) i] = raw (stepId (i));
    for (int k = 1; k < 12; ++k)
        keys[(size_t) k] = raw (keyId (k));
    const char* what[] = { "Dest", "Min", "Max", "Chance", "Smooth" };
    for (int m = 0; m < rythm::numMods; ++m)
        for (int w = 0; w < 5; ++w)
            mods[(size_t) m][(size_t) w] = raw (modId (m, what[w]));

    formatManager.addFormat (std::make_unique<juce::VST3PluginFormat>());
    for (int i = 0; i < numProxies; ++i)
    {
        auto* p = new Proxy (i);
        proxies.push_back (p);
        addParameter (p);
    }

    lastCc.fill (-1);
    recorded.resize (8192);
    events.reserve (1024);
    noteInfos.reserve (512);
    inputs.reserve (256);
    work.setSize (8, 8192);
    loopSnapshot.reserve ((size_t) rythm::Engine::historySize / 2);
    heldSnapshotAudio.reserve (128);
    heldSnapshot.reserve (128);

    presets.restoreFromState();
    startTimer (300);
}

RythmAudioProcessor::~RythmAudioProcessor()
{
    stopTimer();
    unloadPlugin();
}

double RythmAudioProcessor::getTailLengthSeconds() const
{
    return inner != nullptr ? inner->getTailLengthSeconds() : 0.0;
}

bool RythmAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& out = layouts.getMainOutputChannelSet();
    return (out == juce::AudioChannelSet::mono() || out == juce::AudioChannelSet::stereo()) && layouts.getMainInputChannelSet().isDisabled();
}

void RythmAudioProcessor::prepareInner (juce::AudioPluginInstance& p)
{
    p.setRateAndBufferSizeDetails (currentRate, currentBlock);
    p.setNonRealtime (isNonRealtime());
    p.prepareToPlay (currentRate, currentBlock);
}

void RythmAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    currentRate = sampleRate > 0.0 ? sampleRate : 44100.0;
    currentBlock = juce::jmax (1, samplesPerBlock);

    // La boucle survit à un prepareToPlay
    const auto keep = engine.getLoopContent();
    engine.prepare (currentRate);
    if (! keep.empty())
        engine.setLoopContent (keep);
    lastCc.fill (-1);

    const juce::SpinLock::ScopedLockType sl (innerLock);
    work.setSize (juce::jmax (8, work.getNumChannels()), juce::jmax (currentBlock, 8192), false, true, true);
    if (inner != nullptr)
        prepareInner (*inner);
    prepared = true;
}

void RythmAudioProcessor::releaseResources()
{
    const juce::SpinLock::ScopedLockType sl (innerLock);
    if (inner != nullptr)
        inner->releaseResources();
    prepared = false;
}

rythm::Settings RythmAudioProcessor::readSettings() const
{
    rythm::Settings s;
    auto pc = [] (const Raw& r) { return juce::jlimit (0.0f, 1.0f, r.get() * 0.01f); };
    auto idx = [] (const Raw& r) { return juce::roundToInt (r.get()); };

    s.mode = idx (mode);
    s.rate = idx (rate);
    s.feel = idx (feel);
    s.source = idx (source);
    s.density = pc (density);
    s.fill = pc (fill);
    s.shuffle = pc (shuffle);
    s.human = pc (human);
    s.glitch = pc (glitch);
    s.groove2 = pc (groove2);
    s.groove4 = pc (groove4);
    s.split = pc (split);
    s.numSteps = juce::jlimit (1, rythm::maxSteps, idx (numSteps));
    for (int i = 0; i < rythm::maxSteps; ++i)
        s.steps[(size_t) i] = steps[(size_t) i].get() > 0.5f;

    s.gate = juce::jlimit (0.05f, 1.0f, gate.get() * 0.01f);
    s.gateRandom = pc (gateRandom);
    s.velocity = juce::jlimit (1.0f, 127.0f, velocity.get());
    s.velocityRandom = pc (velocityRandom);
    s.accent = pc (accent);

    s.scale = juce::jlimit (0, rythm::numScales - 1, idx (scale));
    uint16_t mask = 1;
    for (int k = 1; k < 12; ++k)
        if (keys[(size_t) k].get() > 0.5f)
            mask = (uint16_t) (mask | (1u << k));
    s.customMask = mask;
    s.chance = pc (chance);
    s.range = juce::jlimit (1, 24, idx (range));
    s.direction = juce::jlimit (-1.0f, 1.0f, direction.get() * 0.01f);
    s.walk = pc (walk);
    s.octave = pc (octave);
    s.slide = pc (slide);
    s.stack = pc (stack);
    s.stackType = juce::jlimit (0, rythm::numStacks - 1, idx (stackType));

    s.bendChance = pc (bend);
    s.bendDepth = juce::jlimit (-1.0f, 1.0f, bendDepth.get() * 0.01f);
    s.bendShape = juce::jlimit (0, rythm::numBendShapes - 1, idx (bendShape));
    s.bendCycles = juce::jlimit (1.0f, 8.0f, bendCycles.get());

    for (int m = 0; m < rythm::numMods; ++m)
    {
        auto& ms = s.mods[(size_t) m];
        const auto& r = mods[(size_t) m];
        ms.active = idx (r[0]) > 0;
        const float a = pc (r[1]), b = pc (r[2]);
        ms.min = std::min (a, b);
        ms.max = std::max (a, b);
        ms.chance = pc (r[3]);
        ms.smooth = pc (r[4]);
    }

    s.loop = loop.get() > 0.5f;
    return s;
}

//==============================================================================
void RythmAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();
    buffer.clear();
    wasBypassed = false;

    // Boucle rechargée avec le projet
    if (loopRestorePending.load())
    {
        const juce::SpinLock::ScopedTryLockType l (loopLock);
        if (l.isLocked())
        {
            engine.setLoopContent (loopToRestore);
            loopRestorePending.store (false);
        }
    }

    // Entrées : notes -> moteur ; le reste (CC, bend du clavier...) passe tel quel
    inputs.clear();
    juce::MidiBuffer thru;
    for (const auto meta : midi)
    {
        const auto& msg = meta.getMessage();
        const int pos = juce::jlimit (0, juce::jmax (0, n - 1), meta.samplePosition);
        if (msg.isNoteOn())
            inputs.push_back ({ pos, true, msg.getNoteNumber(), msg.getVelocity() });
        else if (msg.isNoteOff())
            inputs.push_back ({ pos, false, msg.getNoteNumber(), 0 });
        else if (msg.isAllNotesOff() || msg.isAllSoundOff())
            for (int held : engine.getHeld())
                inputs.push_back ({ pos, false, held, 0 });
        else
            thru.addEvent (msg, pos);
    }

    // Transport
    rythm::Transport t;
    if (auto* ph = getPlayHead())
        if (const auto pos = ph->getPosition())
        {
            t.playing = pos->getIsPlaying();
            if (const auto q = pos->getPpqPosition())   { t.hasPpq = true; t.ppq = *q; }
            if (const auto b = pos->getBpm())           t.bpm = *b;
            if (const auto sig = pos->getTimeSignature())
                if (sig->numerator > 0 && sig->denominator > 0)
                    t.barBeats = sig->numerator * 4.0 / sig->denominator;
        }
    t.bpm = juce::jlimit (20.0, 999.0, t.bpm > 0.0 ? t.bpm : 120.0);
    lastBpm.store (t.bpm);

    auto settings = readSettings();
    const double step = rythm::stepBeats (settings.rate, settings.feel);
    settings.loopSteps = juce::jlimit (1, rythm::Engine::historySize / 2,
                                       juce::roundToInt (loopLengthBars (juce::roundToInt (loopLength.get())) * t.barBeats / step));

    const int64_t blockStart = engine.getNow();
    const double bps = t.bpm / 60.0 / currentRate;
    const double clockStart = clockBeat;

    noteInfos.clear();
    engine.process (n, settings, t, inputs.data(), (int) inputs.size(), events, &noteInfos);

    // MIDI généré
    generated.clear();
    struct SynthMod { int index; float value; };
    std::array<SynthMod, rythm::numMods> synthMods {};
    int numSynthMods = 0;
    {
        const juce::SpinLock::ScopedTryLockType rl (recordLock);
        for (const auto& ev : events)
        {
            juce::MidiMessage msg;
            switch (ev.type)
            {
                case rythm::Event::noteOn:    msg = juce::MidiMessage::noteOn (1, ev.note, (juce::uint8) juce::jlimit (1, 127, ev.velocity)); break;
                case rythm::Event::noteOff:   msg = juce::MidiMessage::noteOff (1, ev.note); break;
                case rythm::Event::pitchBend: msg = juce::MidiMessage::pitchWheel (1, juce::jlimit (0, 16383, ev.velocity)); break;
                case rythm::Event::mod:
                {
                    const int dest = juce::roundToInt (mods[(size_t) ev.slot][0].get());
                    if (dest > 0 && dest < 128)
                    {
                        const int value = juce::jlimit (0, 127, juce::roundToInt (ev.value * 127.0f));
                        if (value == lastCc[(size_t) ev.slot])
                            continue;
                        lastCc[(size_t) ev.slot] = value;
                        msg = juce::MidiMessage::controllerEvent (1, dest, value);
                    }
                    else
                    {
                        if (dest >= 128 && numSynthMods < rythm::numMods)
                        {
                            // Une seule valeur par bloc et par case : la dernière
                            bool replaced = false;
                            for (int i = 0; i < numSynthMods; ++i)
                                if (synthMods[(size_t) i].index == dest - 128)
                                {
                                    synthMods[(size_t) i].value = ev.value;
                                    replaced = true;
                                }
                            if (! replaced)
                                synthMods[(size_t) numSynthMods++] = { dest - 128, ev.value };
                        }
                        continue;
                    }
                    break;
                }
            }
            generated.addEvent (msg, juce::jlimit (0, juce::jmax (0, n - 1), ev.offset));
            if (rl.isLocked())
                recordEvent (clockStart + ev.offset * bps, msg);
        }
    }
    generated.addEvents (thru, 0, n, 0);

    // Instrument chargé
    {
        const juce::SpinLock::ScopedTryLockType sl (innerLock);
        if (sl.isLocked() && inner != nullptr && prepared && ! inner->isSuspended())
        {
            for (auto* p : proxies)
                p->apply();
            for (int i = 0; i < numSynthMods; ++i)
            {
                const int index = synthMods[(size_t) i].index;
                if (juce::isPositiveAndBelow (index, (int) relayed.size()))
                {
                    relayed[(size_t) index]->setValue (juce::jlimit (0.0f, 1.0f, synthMods[(size_t) i].value));
                    proxies[(size_t) index]->value.store (relayed[(size_t) index]->getValue());
                }
            }

            inner->setPlayHead (getPlayHead());
            const int ins = inner->getTotalNumInputChannels(), outs = inner->getTotalNumOutputChannels();
            const int ch = juce::jmax (1, ins, outs);
            if (work.getNumChannels() < ch || work.getNumSamples() < n)
                work.setSize (juce::jmax (ch, work.getNumChannels()), juce::jmax (n, work.getNumSamples()), false, false, true);

            juce::AudioBuffer<float> view (work.getArrayOfWritePointers(), ch, n);
            view.clear();
            juce::MidiBuffer innerMidi (generated);
            inner->processBlock (view, innerMidi);

            if (outs > 0)
                for (int c = 0; c < buffer.getNumChannels(); ++c)
                    buffer.copyFrom (c, 0, view, juce::jmin (c, outs - 1), 0, n);
        }
    }

    midi.swapWith (generated);

    // Affichage
    for (const auto& info : noteInfos)
    {
        int s1, n1, s2, n2;
        noteFifo.prepareToWrite (1, s1, n1, s2, n2);
        if (n1 + n2 == 0)
            break;
        auto& d = noteRing[(size_t) (n1 > 0 ? s1 : s2)];
        d.beat = clockStart + (double) (info.time - blockStart) * bps;
        d.length = info.length;
        d.note = info.note;
        d.velocity = info.velocity;
        d.slide = info.slide;
        d.accent = info.accent;
        d.looped = info.looped;
        noteFifo.finishedWrite (1);
    }

    clockBeat += n * bps;
    displayBeat.store (clockBeat);
    displayBarBeats.store (t.barBeats);
    displayBarPhase.store (engine.getBeat() - clockBeat);
    tonic.store (engine.getTonic());
    running.store (engine.isRunning());
    looping.store (engine.isLooping());
    loopPosition.store (engine.getLoopPosition());
    loopStepsShown.store (engine.isLooping() ? engine.getLoopLength() : settings.loopSteps);
    {
        const auto k = engine.getCurrentStep();
        const int stepsCount = settings.numSteps;
        stepIndex.store (engine.isRunning() && k != std::numeric_limits<int64_t>::min() ? (int) (((k % stepsCount) + stepsCount) % stepsCount) : -1);
    }
    scaleMask.store (rythm::Engine::effectiveMask (settings, engine.getHeld()));

    // Copie de la boucle pour la sauvegarde et le glisser-déposer (seulement quand elle change)
    static_assert (sizeof (uint32_t) == 4);
    if (engine.getLoopVersion() != shownLoopVersion || engine.getHeld() != heldSnapshotAudio)
    {
        const juce::SpinLock::ScopedTryLockType l (loopLock);
        if (l.isLocked())
        {
            loopSnapshot = engine.getLoopContent();
            shownLoopVersion = engine.getLoopVersion();
            heldSnapshotAudio = engine.getHeld();
            if (! heldSnapshotAudio.empty())
                heldSnapshot = heldSnapshotAudio;
        }
    }
}

void RythmAudioProcessor::processBlockBypassed (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    buffer.clear();
    midi.clear();
    if (! wasBypassed)
    {
        engine.allNotesOff (events, 0);
        for (const auto& ev : events)
        {
            if (ev.type == rythm::Event::noteOff)
                midi.addEvent (juce::MidiMessage::noteOff (1, ev.note), 0);
            else if (ev.type == rythm::Event::pitchBend)
                midi.addEvent (juce::MidiMessage::pitchWheel (1, 8192), 0);
        }
        events.clear();
        wasBypassed = true;
    }
}

//==============================================================================
void RythmAudioProcessor::recordEvent (double beat, const juce::MidiMessage& msg)
{
    recorded[recordedHead] = { beat, msg };
    recordedHead = (recordedHead + 1) % recorded.size();
    recordedFull = recordedFull || recordedHead == 0;
}

void RythmAudioProcessor::popDisplayNotes (std::vector<DisplayNote>& out)
{
    int s1, n1, s2, n2;
    noteFifo.prepareToRead (noteFifo.getNumReady(), s1, n1, s2, n2);
    for (int i = 0; i < n1; ++i) out.push_back (noteRing[(size_t) (s1 + i)]);
    for (int i = 0; i < n2; ++i) out.push_back (noteRing[(size_t) (s2 + i)]);
    noteFifo.finishedRead (n1 + n2);
}

void RythmAudioProcessor::randomiseSteps()
{
    juce::Random r;
    const float d = juce::jlimit (0.05f, 1.0f, density.get() * 0.01f);
    const int count = juce::jlimit (1, rythm::maxSteps, juce::roundToInt (numSteps.get()));
    for (int i = 0; i < rythm::maxSteps; ++i)
        if (auto* p = apvts.getParameter (stepId (i)))
        {
            const bool on = i < count && r.nextFloat() < d;
            p->beginChangeGesture();
            p->setValueNotifyingHost (on ? 1.0f : 0.0f);
            p->endChangeGesture();
        }
}

juce::File RythmAudioProcessor::writeMidiForDrag (juce::String& description)
{
    constexpr int ppqTicks = 960;
    const double bpm = lastBpm.load();
    const double barBeats = displayBarBeats.load();
    auto settings = readSettings();

    struct Ev { double beat; juce::MidiMessage msg; };
    std::vector<Ev> list;
    double lengthBeats = 0.0;

    std::vector<rythm::Outcome> content;
    std::vector<int> held;
    {
        const juce::SpinLock::ScopedLockType l (loopLock);
        content = loopSnapshot;
        held = heldSnapshot;
    }
    if (held.empty())
        held.push_back (tonic.load());

    const int tonicNote = rythm::Engine::tonicOf (held);
    const juce::String tonicName = juce::String (noteNames[tonicNote % 12]) + juce::String (tonicNote / 12 - 2);

    if (looping.load() && ! content.empty())
    {
        // La boucle, rejouée hors ligne depuis son début (calé sur la mesure)
        rythm::Engine offline;
        offline.prepare (currentRate);
        offline.setLoopContent (content);
        settings.loop = true;
        settings.loopSteps = (int) content.size();
        const double step = rythm::stepBeats (settings.rate, settings.feel);
        lengthBeats = step * (double) content.size();
        rythm::Transport t;
        t.bpm = bpm;
        t.barBeats = barBeats;
        const int64_t total = (int64_t) std::ceil (lengthBeats * 60.0 / bpm * currentRate);
        std::vector<rythm::Input> ins;
        for (int note : held)
            ins.push_back ({ 0, true, note, 100 });
        std::vector<rythm::Event> out;
        constexpr int chunk = 512;
        const double bps = bpm / 60.0 / currentRate;
        for (int64_t pos = 0; pos < total; pos += chunk)
        {
            const int count = (int) std::min<int64_t> (chunk, total - pos);
            offline.process (count, settings, t, pos == 0 ? ins.data() : nullptr, pos == 0 ? (int) ins.size() : 0, out);
            for (auto& ev : out)
            {
                const double beat = (double) (pos + ev.offset) * bps;
                if (ev.type == rythm::Event::noteOn)       list.push_back ({ beat, juce::MidiMessage::noteOn (1, ev.note, (juce::uint8) ev.velocity) });
                else if (ev.type == rythm::Event::noteOff) list.push_back ({ beat, juce::MidiMessage::noteOff (1, ev.note) });
                else if (ev.type == rythm::Event::pitchBend) list.push_back ({ beat, juce::MidiMessage::pitchWheel (1, ev.velocity) });
                else if (ev.type == rythm::Event::mod)
                {
                    const int dest = juce::roundToInt (mods[(size_t) ev.slot][0].get());
                    if (dest > 0 && dest < 128)
                        list.push_back ({ beat, juce::MidiMessage::controllerEvent (1, dest, juce::jlimit (0, 127, juce::roundToInt (ev.value * 127.0f))) });
                }
            }
        }
        out.clear();
        offline.allNotesOff (out);
        for (auto& ev : out)
            if (ev.type == rythm::Event::noteOff)
                list.push_back ({ lengthBeats, juce::MidiMessage::noteOff (1, ev.note) });
            else if (ev.type == rythm::Event::pitchBend)
                list.push_back ({ lengthBeats, juce::MidiMessage::pitchWheel (1, 8192) });
        description = "LOOP";
    }
    else
    {
        // Les dernières mesures jouées (longueur de LOOP LENGTH), calées sur les mesures
        const double bars = loopLengthBars (juce::roundToInt (loopLength.get()));
        lengthBeats = bars * barBeats;
        const double phase = displayBarPhase.load();
        const double nowBeat = displayBeat.load();
        double end = std::floor ((nowBeat + phase) / barBeats + 1.0e-6) * barBeats - phase;
        if (nowBeat - end > 0.75 * barBeats)
            end += barBeats;   // mesure en cours presque finie : on la prend
        const double start = end - lengthBeats;

        std::vector<Recorded> copy;
        {
            const juce::SpinLock::ScopedLockType l (recordLock);
            const size_t count = recordedFull ? recorded.size() : recordedHead;
            for (size_t i = 0; i < count; ++i)
            {
                const auto& r = recorded[(recordedHead + recorded.size() - count + i) % recorded.size()];
                copy.push_back (r);
            }
        }

        std::array<bool, 128> open {};
        for (auto& r : copy)
        {
            if (r.beat < start - 1.0e-9 || r.beat >= end - 1.0e-9)
                continue;
            if (r.message.isNoteOn())
                open[(size_t) r.message.getNoteNumber()] = true;
            else if (r.message.isNoteOff())
            {
                if (! open[(size_t) r.message.getNoteNumber()])
                    continue;   // note commencée avant la fenêtre
                open[(size_t) r.message.getNoteNumber()] = false;
            }
            list.push_back ({ r.beat - start, r.message });
        }
        for (int note = 0; note < 128; ++note)
            if (open[(size_t) note])
                list.push_back ({ lengthBeats, juce::MidiMessage::noteOff (1, note) });

        bool anyNote = false;
        for (auto& e : list)
            anyNote = anyNote || e.msg.isNoteOn();
        if (! anyNote)
        {
            description = "NOTHING PLAYED YET";
            return {};
        }
        description = "LAST " + loopLengthNames()[juce::roundToInt (loopLength.get())];
    }

    juce::MidiMessageSequence seq;
    seq.addEvent (juce::MidiMessage::tempoMetaEvent (juce::roundToInt (60000000.0 / bpm)), 0.0);
    seq.addEvent (juce::MidiMessage::timeSignatureMetaEvent (juce::jmax (1, juce::roundToInt (barBeats)), 4), 0.0);
    std::stable_sort (list.begin(), list.end(), [] (const Ev& a, const Ev& b)
    {
        if (std::abs (a.beat - b.beat) > 1.0e-9)
            return a.beat < b.beat;
        return a.msg.isNoteOff() && ! b.msg.isNoteOff();
    });
    for (auto& e : list)
        seq.addEvent (e.msg, juce::jlimit (0.0, lengthBeats, e.beat) * ppqTicks);
    seq.addEvent (juce::MidiMessage::endOfTrack(), lengthBeats * ppqTicks);
    seq.updateMatchedPairs();

    juce::MidiFile file;
    file.setTicksPerQuarterNote (ppqTicks);
    file.addTrack (seq);

    const auto dir = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("Rythm MIDI");
    dir.createDirectory();
    const auto scaleText = settings.mode == rythm::rhythmMode ? juce::String ("RHYTHM") : juce::String (rythm::scaleName (settings.scale));
    const auto name = "Rythm " + tonicName + " " + scaleText.toLowerCase() + " " + juce::Time::getCurrentTime().formatted ("%H%M%S");
    const auto target = dir.getChildFile (juce::File::createLegalFileName (name) + ".mid");
    target.deleteFile();
    juce::FileOutputStream stream (target);
    if (! stream.openedOk() || ! file.writeTo (stream))
    {
        description = "CANNOT WRITE MIDI FILE";
        return {};
    }
    stream.flush();
    return target;
}

//==============================================================================
// Instrument chargé (repris d'Oscilloscope Sync Instrument)
void RythmAudioProcessor::refreshLatency()
{
    const int l = inner != nullptr ? juce::jmax (0, inner->getLatencySamples()) : 0;
    if (l != innerLatency.load() || l != getLatencySamples())
    {
        innerLatency.store (l);
        setLatencySamples (l);
        sendChangeMessage();
    }
}

void RythmAudioProcessor::attachProxies()
{
    relayed.clear();
    innerToProxy.clear();
    if (inner == nullptr)
        return;

    const auto& params = inner->getParameters();
    auto* bypassParam = inner->getBypassParameter();
    innerToProxy.assign ((size_t) params.size(), -1);
    for (int i = 0; i < params.size() && (int) relayed.size() < numProxies; ++i)
    {
        auto* p = params[i];
        if (p == bypassParam || ! p->isAutomatable())
            continue;
        innerToProxy[(size_t) i] = (int) relayed.size();
        relayed.push_back (p);
    }

    for (int i = 0; i < numProxies; ++i)
    {
        auto* p = i < (int) relayed.size() ? relayed[(size_t) i] : nullptr;
        proxies[(size_t) i]->bind (p);
        if (p != nullptr)
            p->addListener (this);
    }
}

void RythmAudioProcessor::detachProxies()
{
    for (auto* p : relayed)
        p->removeListener (this);
    relayed.clear();
    innerToProxy.clear();
    for (auto* p : proxies)
        p->bind (nullptr);
}

void RythmAudioProcessor::parameterValueChanged (int parameterIndex, float newValue)
{
    if (parameterIndex < 0 || parameterIndex >= (int) innerToProxy.size() || innerToProxy[(size_t) parameterIndex] < 0)
        return;
    auto* p = proxies[(size_t) innerToProxy[(size_t) parameterIndex]];
    if (std::abs (p->value.load() - newValue) < 1.0e-6f)
        return;
    p->value.store (newValue);
    p->sendValueChangedMessageToListeners (newValue);
}

void RythmAudioProcessor::parameterGestureChanged (int parameterIndex, bool starting)
{
    if (parameterIndex < 0 || parameterIndex >= (int) innerToProxy.size() || innerToProxy[(size_t) parameterIndex] < 0)
        return;
    auto* p = proxies[(size_t) innerToProxy[(size_t) parameterIndex]];
    if (starting) p->beginChangeGesture();
    else          p->endChangeGesture();
}

juce::StringArray RythmAudioProcessor::getRelayedNames() const
{
    juce::StringArray names;
    for (auto* p : proxies)
        if (p->isBound())
            names.add (p->getName (40));
    return names;
}

bool RythmAudioProcessor::loadPlugin (const juce::PluginDescription& description, juce::String& error, const juce::MemoryBlock* state)
{
    JUCE_ASSERT_MESSAGE_THREAD

    if (description.manufacturerName == "Phragma" && description.name == "Rythm")
    {
        error = "Rythm cannot be loaded inside itself.";
        return false;
    }

    auto instance = formatManager.createPluginInstance (description, currentRate, currentBlock, error);
    if (instance == nullptr)
    {
        if (error.isEmpty())
            error = "Could not load " + description.name + ".";
        return false;
    }

    if (state != nullptr && state->getSize() > 0)
        instance->setStateInformation (state->getData(), (int) state->getSize());

    if (prepared)
        prepareInner (*instance);

    if (openEditor != nullptr)
        openEditor->dropInnerEditor();

    std::unique_ptr<juce::AudioPluginInstance> old;
    {
        const juce::SpinLock::ScopedLockType sl (innerLock);
        detachProxies();
        old = std::move (inner);
        inner = std::move (instance);
        innerDescription = description;
        attachProxies();
    }

    if (old != nullptr)
    {
        old->releaseResources();
        old.reset();
    }

    loadError = {};
    pendingSynthState = {};
    refreshLatency();
    updateHostDisplay (ChangeDetails().withParameterInfoChanged (true));
    if (openEditor != nullptr)
        openEditor->innerChanged();
    sendChangeMessage();
    return true;
}

bool RythmAudioProcessor::loadFile (const juce::File& file, juce::String& error, const juce::String& name)
{
    juce::OwnedArray<juce::PluginDescription> types;
    for (auto* format : formatManager.getFormats())
        if (format->fileMightContainThisPluginType (file.getFullPathName()))
            format->findAllTypesForFile (types, file.getFullPathName());

    if (types.isEmpty())
    {
        error = "No VST3 plugin found in " + file.getFileName() + ".";
        return false;
    }

    const juce::PluginDescription* chosen = nullptr;
    for (auto* t : types)
        if (name.isNotEmpty() && t->name == name)
            chosen = t;
    for (auto* t : types)
        if (chosen == nullptr && t->isInstrument)
            chosen = t;
    if (chosen == nullptr)
        chosen = types.getFirst();

    return loadPlugin (*chosen, error);
}

void RythmAudioProcessor::unloadPlugin()
{
    if (inner == nullptr)
        return;

    if (openEditor != nullptr)
        openEditor->dropInnerEditor();

    std::unique_ptr<juce::AudioPluginInstance> old;
    {
        const juce::SpinLock::ScopedLockType sl (innerLock);
        detachProxies();
        old = std::move (inner);
    }
    old->releaseResources();
    old.reset();

    innerDescription = {};
    refreshLatency();
    updateHostDisplay (ChangeDetails().withParameterInfoChanged (true));
    if (openEditor != nullptr)
        openEditor->innerChanged();
    sendChangeMessage();
}

juce::Array<juce::File> RythmAudioProcessor::findVst3Files()
{
    juce::Array<juce::File> dirs;
    const auto home = juce::File::getSpecialLocation (juce::File::userHomeDirectory);
   #if JUCE_MAC
    dirs.add (home.getChildFile ("Library/Audio/Plug-Ins/VST3"));
    dirs.add (juce::File ("/Library/Audio/Plug-Ins/VST3"));
   #elif JUCE_WINDOWS
    const auto common = juce::SystemStats::getEnvironmentVariable ("CommonProgramFiles", "C:\\Program Files\\Common Files");
    dirs.add (juce::File (common).getChildFile ("VST3"));
    dirs.add (juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory).getParentDirectory().getChildFile ("Local\\Programs\\Common\\VST3"));
   #else
    dirs.add (home.getChildFile (".vst3"));
    dirs.add (juce::File ("/usr/lib/vst3"));
    dirs.add (juce::File ("/usr/local/lib/vst3"));
   #endif
    const auto extra = juce::SystemStats::getEnvironmentVariable ("RYTHM_EXTRA_VST3_DIR", {});
    if (extra.isNotEmpty())
        dirs.add (juce::File (extra));

    juce::Array<juce::File> found;
    for (auto& dir : dirs)
    {
        if (! dir.isDirectory())
            continue;
        for (const auto& entry : juce::RangedDirectoryIterator (dir, true, "*.vst3", juce::File::findFilesAndDirectories))
        {
            const auto f = entry.getFile();
            if (f.getFullPathName().upToLastOccurrenceOf (f.getFileName(), false, false).containsIgnoreCase (".vst3"))
                continue;
            if (f.getFileNameWithoutExtension() == "Rythm")
                continue;
            bool duplicate = false;
            for (auto& g : found)
                duplicate = duplicate || g.getFileName() == f.getFileName();
            if (! duplicate)
                found.add (f);
        }
    }

    std::sort (found.begin(), found.end(), [] (const juce::File& a, const juce::File& b)
    {
        return a.getFileName().compareIgnoreCase (b.getFileName()) < 0;
    });
    return found;
}

//==============================================================================
void RythmAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    state.removeChild (state.getChildWithName ("SYNTH"), nullptr);
    state.removeChild (state.getChildWithName ("LOOPDATA"), nullptr);

    // Instrument chargé
    if (inner != nullptr)
    {
        juce::ValueTree synth ("SYNTH");
        juce::MemoryBlock innerState;
        inner->getStateInformation (innerState);
        if (auto xml = innerDescription.createXml())
            synth.appendChild (juce::ValueTree::fromXml (*xml), nullptr);
        synth.setProperty ("innerState", innerState.toBase64Encoding(), nullptr);
        state.appendChild (synth, nullptr);
    }
    else if (pendingSynthState.isValid())
    {
        state.appendChild (pendingSynthState.createCopy(), nullptr);
    }

    // Boucle capturée
    std::vector<rythm::Outcome> content;
    {
        const juce::SpinLock::ScopedLockType l (loopLock);
        content = loopRestorePending.load() ? loopToRestore : loopSnapshot;
    }
    if (! content.empty())
    {
        juce::StringArray items;
        for (auto& o : content)
            items.add (o.toString());
        juce::ValueTree loopData ("LOOPDATA");
        loopData.setProperty ("steps", items.joinIntoString (";"), nullptr);
        state.appendChild (loopData, nullptr);
    }

    if (const auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void RythmAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    const auto xml = getXmlFromBinary (data, sizeInBytes);
    if (xml == nullptr || ! xml->hasTagName (apvts.state.getType()))
        return;

    auto tree = juce::ValueTree::fromXml (*xml);
    const auto synth = tree.getChildWithName ("SYNTH").createCopy();
    const auto loopData = tree.getChildWithName ("LOOPDATA").createCopy();
    tree.removeChild (tree.getChildWithName ("SYNTH"), nullptr);
    tree.removeChild (tree.getChildWithName ("LOOPDATA"), nullptr);

    apvts.replaceState (tree);
    presets.restoreFromState();

    // Boucle
    std::vector<rythm::Outcome> content;
    if (loopData.isValid())
    {
        juce::StringArray items;
        items.addTokens (loopData["steps"].toString(), ";", {});
        for (auto& item : items)
        {
            rythm::Outcome o;
            if (! rythm::Outcome::fromString (item.toStdString(), o))
            {
                content.clear();
                break;
            }
            content.push_back (o);
            if ((int) content.size() >= rythm::Engine::historySize / 2)
                break;
        }
    }
    {
        const juce::SpinLock::ScopedLockType l (loopLock);
        loopToRestore = content;
        loopSnapshot = content;
        loopRestorePending.store (true);
    }

    // Instrument
    auto load = [synth] (RythmAudioProcessor& self)
    {
        if (! synth.isValid())
        {
            self.unloadPlugin();
            return;
        }
        const auto pluginTree = synth.getChildWithName ("PLUGIN");
        juce::PluginDescription description;
        std::unique_ptr<juce::XmlElement> pluginXml (pluginTree.isValid() ? pluginTree.createXml() : nullptr);
        if (pluginXml == nullptr || ! description.loadFromXml (*pluginXml))
            return;

        juce::MemoryBlock innerState;
        innerState.fromBase64Encoding (synth["innerState"].toString());

        juce::String error;
        if (! self.loadPlugin (description, error, &innerState))
        {
            self.loadError = "CANNOT LOAD " + description.name.toUpperCase() + " (" + error + ")";
            self.pendingSynthState = synth.createCopy();
            self.sendChangeMessage();
        }
    };

    if (juce::MessageManager::getInstance()->isThisTheMessageThread())
    {
        load (*this);
    }
    else
    {
        juce::WeakReference<RythmAudioProcessor> weak (this);
        juce::MessageManager::callAsync ([weak, load]
        {
            if (auto* self = weak.get())
                load (*self);
        });
    }
}

juce::AudioProcessorEditor* RythmAudioProcessor::createEditor()
{
    return new RythmAudioProcessorEditor (*this);
}

//==============================================================================
#if ! RYTHM_TESTS
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new RythmAudioProcessor();
}
#endif
