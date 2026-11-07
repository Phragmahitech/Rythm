#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "PresetLibrary.h"
#include "RythmEngine.h"

/**
    Rythm : générateur de rythmes et de mélodies MIDI (façon Rhythmizer), par Phragma.

    Une note tenue (clip MIDI ou clavier) le met en marche. Le MIDI généré sort de deux façons à la fois :
      - vers l'hôte (sortie MIDI du plugin : dans Live, une autre piste MIDI le reçoit par « MIDI From ») ;
      - vers un instrument VST3 chargé dans Rythm (SYNTH), dont l'audio sort du plugin.
    Les 128 premiers paramètres automatisables de l'instrument chargé sont relayés à l'hôte (P1..P128).
*/
class RythmAudioProcessor final : public juce::AudioProcessor,
                                  public juce::ChangeBroadcaster,
                                  private juce::AudioProcessorParameter::Listener,
                                  private juce::Timer
{
public:
    RythmAudioProcessor();
    ~RythmAudioProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported (const BusesLayout&) const override;

    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    void processBlockBypassed (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using juce::AudioProcessor::processBlock;
    using juce::AudioProcessor::processBlockBypassed;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override                         { return true; }

    const juce::String getName() const override             { return JucePlugin_Name; }
    bool acceptsMidi() const override                       { return true; }
    bool producesMidi() const override                      { return true; }
    bool isMidiEffect() const override                      { return false; }
    double getTailLengthSeconds() const override;

    int getNumPrograms() override                           { return 1; }
    int getCurrentProgram() override                        { return 0; }
    void setCurrentProgram (int) override                   {}
    const juce::String getProgramName (int) override        { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    //==========================================================================
    // Paramètres (ne jamais renommer un identifiant : les projets et presets les gardent)
    static constexpr const char* modeId = "mode";
    static constexpr const char* rateId = "rate";
    static constexpr const char* feelId = "feel";
    static constexpr const char* sourceId = "source";
    static constexpr const char* densityId = "density";
    static constexpr const char* fillId = "fill";
    static constexpr const char* shuffleId = "shuffle";
    static constexpr const char* humanId = "human";
    static constexpr const char* glitchId = "glitch";
    static constexpr const char* groove2Id = "groove2";
    static constexpr const char* groove4Id = "groove4";
    static constexpr const char* splitId = "split";
    static constexpr const char* numStepsId = "numSteps";
    static constexpr const char* gateId = "gate";
    static constexpr const char* gateRandomId = "gateRandom";
    static constexpr const char* velocityId = "velocity";
    static constexpr const char* velocityRandomId = "velocityRandom";
    static constexpr const char* accentId = "accent";
    static constexpr const char* scaleId = "scale";
    static constexpr const char* chanceId = "chance";
    static constexpr const char* rangeId = "range";
    static constexpr const char* directionId = "direction";
    static constexpr const char* walkId = "walk";
    static constexpr const char* octaveId = "octave";
    static constexpr const char* slideId = "slide";
    static constexpr const char* stackId = "stack";
    static constexpr const char* stackTypeId = "stackType";
    static constexpr const char* bendId = "bend";
    static constexpr const char* bendDepthId = "bendDepth";
    static constexpr const char* bendShapeId = "bendShape";
    static constexpr const char* bendCyclesId = "bendCycles";
    static constexpr const char* loopId = "loop";
    static constexpr const char* loopLengthId = "loopLength";

    static juce::String stepId (int i)                      { return "step" + juce::String (i + 1); }     // i = 0..31
    static juce::String keyId (int i)                       { return "key" + juce::String (i); }          // i = 1..11
    static juce::String modId (int slot, const char* what)  { return "mod" + juce::String (slot + 1) + what; }  // Dest, Min, Max, Chance, Smooth

    static juce::StringArray loopLengthNames()              { return { "1/4 BAR", "1/2 BAR", "1 BAR", "2 BARS", "4 BARS", "8 BARS" }; }
    static double loopLengthBars (int index)                { const double b[] = { 0.25, 0.5, 1, 2, 4, 8 }; return b[juce::jlimit (0, 5, index)]; }

    /** Destination d'une modulation : 0 = OFF, 1..127 = CC, 128..255 = paramètre n° 1..128 de l'instrument chargé. */
    static constexpr int modDestCount = 256;
    juce::String modDestName (int dest) const;

    juce::AudioProcessorValueTreeState apvts;
    PresetLibrary presets;

    //==========================================================================
    // Instrument chargé
    static constexpr int numProxies = 128;

    bool loadPlugin (const juce::PluginDescription&, juce::String& error, const juce::MemoryBlock* state = nullptr);
    bool loadFile (const juce::File&, juce::String& error, const juce::String& name = {});
    void unloadPlugin();
    juce::AudioPluginInstance* getInner() const noexcept    { return inner.get(); }
    juce::String getInnerName() const                       { return inner != nullptr ? innerDescription.name : juce::String(); }
    int getInnerLatency() const noexcept                    { return innerLatency.load(); }
    juce::String getLoadError() const                       { return loadError; }
    static juce::Array<juce::File> findVst3Files();
    /** Noms des paramètres relayés (pour le menu des modulations). */
    juce::StringArray getRelayedNames() const;
    void refreshLatencyNow()                                { refreshLatency(); }

    /** La fenêtre ouverte : elle ferme celle de l'instrument avant qu'on le détruise. */
    void setOpenEditor (class RythmAudioProcessorEditor* e) noexcept { openEditor = e; }

    //==========================================================================
    // Pour l'interface
    struct DisplayNote
    {
        double beat = 0.0, length = 0.0;    // sur l'horloge d'affichage (noires, continue)
        int note = 60, velocity = 100;
        bool slide = false, accent = false, looped = false;
    };

    /** Notes jouées depuis le dernier appel (fil des messages). */
    void popDisplayNotes (std::vector<DisplayNote>&);
    double getDisplayBeat() const noexcept                  { return displayBeat.load(); }
    double getDisplayBarBeats() const noexcept              { return displayBarBeats.load(); }
    /** Écart entre l'horloge d'affichage et les mesures de l'hôte (pour dessiner les barres). */
    double getDisplayBarPhase() const noexcept              { return displayBarPhase.load(); }
    int getTonic() const noexcept                           { return tonic.load(); }
    bool isRunning() const noexcept                         { return running.load(); }
    bool isLooping() const noexcept                         { return looping.load(); }
    int getLoopPosition() const noexcept                    { return loopPosition.load(); }
    int getLoopSteps() const noexcept                       { return loopStepsShown.load(); }
    int getCurrentStepIndex() const noexcept                { return stepIndex.load(); }
    uint16_t getScaleMask() const noexcept                  { return scaleMask.load(); }

    /** Fichier .mid à glisser dans le DAW : la boucle (si LOOP) ou les dernières mesures jouées. */
    juce::File writeMidiForDrag (juce::String& description);
    /** Les réglages courants (lus des paramètres). */
    rythm::Settings readSettings() const;

    /** Remplit les pas (STEPS) au hasard selon DENSITY. */
    void randomiseSteps();

    // Points de départ d'usine
    static int getNumFactoryPresets();
    static juce::String getFactoryPresetName (int);
    void applyFactory (int);
    int matchingFactory() const;

    static juce::StringArray transientParameters()          { return { loopId }; }

private:
    class Proxy;

    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout (RythmAudioProcessor&);
    PresetLibrary::Factory makeFactory();
    static std::vector<std::pair<juce::String, float>> factoryValues (int);

    void refreshLatency();
    void prepareInner (juce::AudioPluginInstance&);
    void attachProxies();
    void detachProxies();
    void timerCallback() override                           { refreshLatency(); }
    void parameterValueChanged (int parameterIndex, float newValue) override;
    void parameterGestureChanged (int parameterIndex, bool gestureIsStarting) override;

    void recordEvent (double beat, const juce::MidiMessage&);

    // Paramètres lus sur le fil audio
    struct Raw
    {
        std::atomic<float>* p = nullptr;
        float get() const noexcept { return p != nullptr ? p->load() : 0.0f; }
    };
    Raw mode, rate, feel, source, density, fill, shuffle, human, glitch, groove2, groove4, split, numSteps,
        gate, gateRandom, velocity, velocityRandom, accent, scale, chance, range, direction, walk, octave, slide,
        stack, stackType, bend, bendDepth, bendShape, bendCycles, loop, loopLength;
    std::array<Raw, rythm::maxSteps> steps;
    std::array<Raw, 12> keys;
    std::array<std::array<Raw, 5>, rythm::numMods> mods;

    rythm::Engine engine;
    std::vector<rythm::Event> events;
    std::vector<rythm::NoteInfo> noteInfos;
    std::vector<rythm::Input> inputs;
    juce::MidiBuffer generated;
    std::array<int, rythm::numMods> lastCc {};
    double currentRate = 44100.0;
    int currentBlock = 512;
    bool prepared = false;
    bool wasBypassed = false;

    // Horloge d'affichage et d'enregistrement (continue, en noires)
    double clockBeat = 0.0;

    struct Recorded
    {
        double beat = 0.0;
        juce::MidiMessage message;
    };
    std::vector<Recorded> recorded;          // anneau
    size_t recordedHead = 0;
    bool recordedFull = false;
    juce::SpinLock recordLock;

    juce::AbstractFifo noteFifo { 1024 };
    std::array<DisplayNote, 1024> noteRing;

    std::atomic<double> displayBeat { 0.0 }, displayBarBeats { 4.0 }, displayBarPhase { 0.0 };
    std::atomic<int> tonic { 60 }, loopPosition { -1 }, loopStepsShown { 16 }, stepIndex { -1 };
    std::atomic<bool> running { false }, looping { false };
    std::atomic<uint16_t> scaleMask { 1 };
    std::atomic<double> lastBpm { 120.0 };

    // Boucle gardée avec le projet (fil des messages -> fil audio)
    juce::SpinLock loopLock;
    std::vector<rythm::Outcome> loopSnapshot;      // copie lisible depuis le fil des messages
    std::vector<rythm::Outcome> loopToRestore;
    std::atomic<bool> loopRestorePending { false };
    uint32_t shownLoopVersion = 0;
    std::vector<int> heldSnapshotAudio, heldSnapshot;   // notes tenues (fil audio / copie pour le glisser-déposer)

    // Instrument
    juce::AudioPluginFormatManager formatManager;
    std::unique_ptr<juce::AudioPluginInstance> inner;
    juce::PluginDescription innerDescription;
    juce::SpinLock innerLock;
    std::atomic<int> innerLatency { 0 };
    juce::AudioBuffer<float> work;
    std::vector<Proxy*> proxies;
    std::vector<int> innerToProxy;
    std::vector<juce::AudioProcessorParameter*> relayed;
    class RythmAudioProcessorEditor* openEditor = nullptr;
    juce::String loadError;
    juce::ValueTree pendingSynthState;

    JUCE_DECLARE_WEAK_REFERENCEABLE (RythmAudioProcessor)
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (RythmAudioProcessor)
};
