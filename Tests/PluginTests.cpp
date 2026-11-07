// Tests du plugin Rythm : paramètres, MIDI produit, instrument chargé, boucle gardée avec le projet,
// glisser-déposer MIDI, interface, presets.
#include "../Source/PluginProcessor.h"
#include "../Source/PluginEditor.h"

#include <iostream>
#include <random>
#include <set>

static int failures = 0, checks = 0;

static void check (bool ok, const juce::String& what)
{
    ++checks;
    if (! ok)
    {
        ++failures;
        std::cout << "  ECHEC : " << what << std::endl;
    }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new RythmAudioProcessor();
}

using P = RythmAudioProcessor;

static std::unique_ptr<RythmAudioProcessor> makePlugin (double sr = 48000.0, int block = 512)
{
    auto p = std::make_unique<RythmAudioProcessor>();
    p->setRateAndBufferSizeDetails (sr, block);
    p->prepareToPlay (sr, block);
    return p;
}

static void setParam (juce::AudioProcessor& p, const juce::String& id, float plain)
{
    auto& rp = dynamic_cast<RythmAudioProcessor&> (p);
    auto* prm = rp.apvts.getParameter (id);
    prm->setValueNotifyingHost (prm->convertTo0to1 (plain));
}

/** Simple hôte : transport à 120 BPM qui avance (ou arrêté). */
struct TestPlayHead final : public juce::AudioPlayHead
{
    bool playing = false;
    double bpm = 120.0, ppq = 0.0;
    juce::Optional<PositionInfo> getPosition() const override
    {
        PositionInfo info;
        info.setIsPlaying (playing);
        info.setBpm (bpm);
        info.setPpqPosition (ppq);
        info.setTimeSignature (juce::AudioPlayHead::TimeSignature { 4, 4 });
        return info;
    }
};

struct Collected
{
    std::vector<std::pair<int64_t, juce::MidiMessage>> messages;
    double peak = 0.0;
    bool finite = true;

    int count (std::function<bool (const juce::MidiMessage&)> f) const
    {
        int n = 0;
        for (auto& m : messages) if (f (m.second)) ++n;
        return n;
    }
};

/** Fait tourner le plugin : notes d'entrée (échantillon absolu, on?, note). */
static Collected run (RythmAudioProcessor& p, int64_t total, std::vector<std::tuple<int64_t, bool, int>> notes, int block = 512,
                      TestPlayHead* head = nullptr)
{
    Collected c;
    juce::AudioBuffer<float> buffer (2, block);
    p.setPlayHead (head);
    for (int64_t pos = 0; pos < total; pos += block)
    {
        juce::MidiBuffer midi;
        for (auto& [at, on, note] : notes)
            if (at >= pos && at < pos + block)
                midi.addEvent (on ? juce::MidiMessage::noteOn (1, note, (juce::uint8) 100) : juce::MidiMessage::noteOff (1, note), (int) (at - pos));
        buffer.clear();
        p.processBlock (buffer, midi);
        for (const auto m : midi)
            c.messages.push_back ({ pos + m.samplePosition, m.getMessage() });
        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < block; ++i)
            {
                const float v = buffer.getSample (ch, i);
                c.finite = c.finite && std::isfinite (v);
                c.peak = std::max (c.peak, (double) std::abs (v));
            }
        if (head != nullptr && head->playing)
            head->ppq += block * head->bpm / 60.0 / p.getSampleRate();
    }
    p.setPlayHead (nullptr);
    return c;
}

//==============================================================================
static void testParameters()
{
    std::cout << "- parametres" << std::endl;
    auto p = makePlugin();
    const auto& params = p->getParameters();
    check (params.size() == 96 + P::numProxies, "nombre de parametres : " + juce::String (params.size()));

    std::set<juce::String> ids;
    int ranged = 0;
    for (auto* prm : params)
        if (auto* r = dynamic_cast<juce::RangedAudioParameter*> (prm))
        {
            ++ranged;
            ids.insert (r->getParameterID());
            check (r->getName (40).isNotEmpty(), "nom vide : " + r->getParameterID());
        }
    check (ranged == 96 && (int) ids.size() == 96, "identifiants uniques");
    for (auto id : { "mode", "rate", "scale", "loop", "loopLength", "step1", "step32", "key1", "key11", "mod1Dest", "mod4Smooth", "bendCycles" })
        check (p->apvts.getParameter (id) != nullptr, juce::String ("parametre absent : ") + id);

    // Textes
    auto* dest = p->apvts.getParameter ("mod1Dest");
    check (dest->getText (dest->convertTo0to1 (74.0f), 40) == "CC 74 CUTOFF", "nom de la destination CC 74 : " + dest->getText (dest->convertTo0to1 (74.0f), 40));
    check (dest->getText (0.0f, 40) == "OFF", "destination OFF");
    check (p->apvts.getParameter ("scale")->getText (p->apvts.getParameter ("scale")->getValue(), 40) == "PHRYGIAN", "gamme par defaut");
    check (p->apvts.getParameter ("direction")->getText (0.5f, 40) == "BOTH", "direction au centre");
    check (p->acceptsMidi() && p->producesMidi() && ! p->isMidiEffect(), "instrument qui produit du MIDI");
}

static void testMidiOut()
{
    std::cout << "- MIDI produit (RHYTHM, MELODY, transport)" << std::endl;
    {
        auto p = makePlugin();
        setParam (*p, P::densityId, 100.0f);
        auto c = run (*p, 48000, { { 1000, true, 45 }, { 40000, false, 45 } });
        const int ons = c.count ([] (const juce::MidiMessage& m) { return m.isNoteOn(); });
        const int offs = c.count ([] (const juce::MidiMessage& m) { return m.isNoteOff(); });
        check (ons == 7, "7 doubles-croches a 120 BPM : " + juce::String (ons));
        check (ons == offs, "autant de note-off que de note-on");
        bool pitch = true;
        for (auto& [t, m] : c.messages) if (m.isNoteOn()) pitch = pitch && m.getNoteNumber() == 45 && m.getChannel() == 1;
        check (pitch, "RHYTHM : la note tenue, canal 1");
        check (c.messages.front().first == 1000, "premiere note sur l'appui");
        check (c.peak < 1.0e-9, "sans instrument : silence");
    }
    {
        auto p = makePlugin();
        setParam (*p, P::modeId, 1.0f);
        setParam (*p, P::scaleId, (float) rythm::minorPentatonic);
        setParam (*p, P::densityId, 100.0f);
        setParam (*p, P::chanceId, 100.0f);
        auto c = run (*p, 96000, { { 0, true, 57 } });
        const uint16_t mask = rythm::scaleMask (rythm::minorPentatonic);
        bool inScale = true;
        std::set<int> distinct;
        for (auto& [t, m] : c.messages)
            if (m.isNoteOn())
            {
                distinct.insert (m.getNoteNumber());
                inScale = inScale && ((mask >> (((m.getNoteNumber() - 57) % 12 + 12) % 12)) & 1u);
            }
        check (inScale && distinct.size() >= 4, "MELODY pentatonique mineure sur A");
    }
    {
        // Transport qui tourne : calage sur la grille
        auto p = makePlugin();
        setParam (*p, P::densityId, 100.0f);
        TestPlayHead head;
        head.playing = true;
        auto c = run (*p, 48000, { { 7000, true, 60 } }, 512, &head);
        bool grid = true;
        int ons = 0;
        for (auto& [t, m] : c.messages) if (m.isNoteOn()) { ++ons; grid = grid && t % 6000 == 0; }
        check (ons > 4 && grid, "transport : notes sur la grille de l'hote");
    }
    {
        // Les CC et le bend du clavier passent ; les CC des modulations sortent
        auto p = makePlugin();
        setParam (*p, P::densityId, 100.0f);
        setParam (*p, P::modId (0, "Dest"), 74.0f);
        setParam (*p, P::modId (0, "Chance"), 100.0f);
        juce::AudioBuffer<float> buffer (2, 512);
        juce::MidiBuffer midi;
        midi.addEvent (juce::MidiMessage::noteOn (1, 60, (juce::uint8) 100), 0);
        midi.addEvent (juce::MidiMessage::controllerEvent (1, 64, 127), 10);
        p->processBlock (buffer, midi);
        bool sustainThru = false, cutoff = false;
        for (const auto m : midi)
        {
            sustainThru = sustainThru || (m.getMessage().isController() && m.getMessage().getControllerNumber() == 64);
            cutoff = cutoff || (m.getMessage().isController() && m.getMessage().getControllerNumber() == 74);
        }
        check (sustainThru, "CC du clavier transmis");
        check (cutoff, "MOD 1 -> CC 74 envoye");
    }
    {
        // Bypass : tout s'arrête
        auto p = makePlugin();
        setParam (*p, P::densityId, 100.0f);
        setParam (*p, P::gateId, 100.0f);
        juce::AudioBuffer<float> buffer (2, 512);
        juce::MidiBuffer midi;
        midi.addEvent (juce::MidiMessage::noteOn (1, 60, (juce::uint8) 100), 0);
        p->processBlock (buffer, midi);
        midi.clear();
        p->processBlockBypassed (buffer, midi);
        int offs = 0;
        for (const auto m : midi) if (m.getMessage().isNoteOff()) ++offs;
        check (offs == 1, "bypass : note-off envoye");
    }
}

static void testLoopStateAndDrag()
{
    std::cout << "- LOOP gardee avec le projet, glisser-deposer MIDI" << std::endl;
    auto p = makePlugin();
    setParam (*p, P::modeId, 1.0f);
    setParam (*p, P::densityId, 80.0f);
    setParam (*p, P::chanceId, 80.0f);
    setParam (*p, P::loopLengthId, 2.0f);   // 1 mesure = 16 doubles-croches
    run (*p, 48000 * 4, { { 0, true, 50 } });
    setParam (*p, P::loopId, 1.0f);
    auto first = run (*p, 48000 * 4, {});
    check (p->isLooping() && p->getLoopSteps() == 16, "boucle de 16 pas");

    juce::MemoryBlock state;
    p->getStateInformation (state);
    {
        auto xml = juce::AudioProcessor::getXmlFromBinary (state.getData(), (int) state.getSize());
        check (xml != nullptr && xml->getChildByName ("LOOPDATA") != nullptr, "la boucle est dans l'etat");
    }

    // Glisser : la boucle, 4 temps
    juce::String what;
    const auto file = p->writeMidiForDrag (what);
    check (what == "LOOP" && file.existsAsFile(), "fichier MIDI de la boucle : " + what);
    int fileNotes = 0;
    if (file.existsAsFile())
    {
        juce::FileInputStream in (file);
        juce::MidiFile mf;
        check (mf.readFrom (in), "fichier MIDI lisible");
        double lastTime = 0.0;
        if (mf.getNumTracks() > 0)
            for (const auto* e : *mf.getTrack (0))
            {
                if (e->message.isNoteOn()) ++fileNotes;
                lastTime = std::max (lastTime, e->message.getTimeStamp());
            }
        check (lastTime <= 4 * 960 + 1, "fichier de 4 temps (" + juce::String (lastTime) + " ticks)");
    }
    int perBar = 0;
    for (auto& [t, m] : first.messages) if (m.isNoteOn() && t >= 96000 && t < 96000 + 96000) ++perBar;
    check (fileNotes > 0 && fileNotes == perBar, "notes du fichier = notes d'une mesure de boucle (" + juce::String (fileNotes) + " / " + juce::String (perBar) + ")");

    // Nouveau plugin avec l'état : la même boucle rejouée
    auto q = makePlugin();
    q->setStateInformation (state.getData(), (int) state.getSize());
    check (q->apvts.getParameter (P::loopId)->getValue() > 0.5f, "LOOP rallume");
    auto replay = run (*q, 48000 * 2, { { 0, true, 50 } });
    std::multiset<int> a, b;
    for (auto& [t, m] : first.messages) if (m.isNoteOn() && t >= 0 && t < 96000) a.insert (m.getNoteNumber());
    for (auto& [t, m] : replay.messages) if (m.isNoteOn() && t >= 0 && t < 96000) b.insert (m.getNoteNumber());
    check (! a.empty() && a == b, "boucle rechargee identique");

    // Sans boucle : les dernières mesures jouées
    auto r = makePlugin();
    setParam (*r, P::densityId, 100.0f);
    juce::String what2;
    check (! r->writeMidiForDrag (what2).existsAsFile() && what2 == "NOTHING PLAYED YET", "rien joue : pas de fichier");
    run (*r, 48000 * 3, { { 0, true, 40 } });
    const auto f2 = r->writeMidiForDrag (what2);
    check (f2.existsAsFile() && what2 == "LAST 1 BAR", "dernieres mesures : " + what2);
    if (f2.existsAsFile())
    {
        juce::FileInputStream in (f2);
        juce::MidiFile mf;
        mf.readFrom (in);
        int ons = 0, offs = 0;
        for (const auto* e : *mf.getTrack (0)) { if (e->message.isNoteOn()) ++ons; if (e->message.isNoteOff()) ++offs; }
        check (ons == 16 && offs == 16, "derniere mesure : 16 notes et 16 fins (" + juce::String (ons) + " / " + juce::String (offs) + ")");
    }
}

static juce::File testSynthFile()
{
    return juce::File (RYTHM_TEST_SYNTH_DIR).getChildFile ("RythmTestSynth.vst3");
}

static void testHostedSynth()
{
    std::cout << "- instrument charge" << std::endl;
    const auto file = testSynthFile();
    check (file.exists(), "instrument de test introuvable : " + file.getFullPathName());
    if (! file.exists())
        return;

    auto p = makePlugin();
    juce::String error;
    check (p->loadFile (file, error), "chargement : " + error);
    check (p->getInner() != nullptr && p->getInnerName() == "RythmTestSynth", "nom de l'instrument");
    p->refreshLatencyNow();
    check (p->getLatencySamples() == 110, "latence de l'instrument declaree : " + juce::String (p->getLatencySamples()));
    const auto names = p->getRelayedNames();
    check (names.contains ("Gain") && names.contains ("Latency"), "parametres relayes : " + names.joinIntoString (", "));
    check (p->getParameters()[96]->getName (20) == "Gain", "P1 renomme Gain");
    check (p->modDestName (128) == "SYNTH: Gain", "destination SYNTH : " + p->modDestName (128));

    setParam (*p, P::densityId, 100.0f);
    auto c = run (*p, 24000, { { 0, true, 40 } });
    check (c.peak > 0.1 && c.finite, "l'instrument joue le MIDI genere (crete " + juce::String (c.peak, 3) + ")");
    check (c.count ([] (const juce::MidiMessage& m) { return m.isNoteOn(); }) > 0, "le MIDI sort aussi vers l'hote");

    // Modulation vers un paramètre de l'instrument (Gain)
    setParam (*p, P::modId (1, "Dest"), 128.0f);
    setParam (*p, P::modId (1, "Min"), 10.0f);
    setParam (*p, P::modId (1, "Max"), 20.0f);
    setParam (*p, P::modId (1, "Chance"), 100.0f);
    run (*p, 24000, {});
    auto* gain = p->getInner()->getParameters()[0];
    check (gain->getValue() >= 0.099f && gain->getValue() <= 0.201f, "MOD 2 pilote le Gain de l'instrument : " + juce::String (gain->getValue(), 3));

    // L'hôte automatise P1
    p->getParameters()[96]->setValueNotifyingHost (0.5f);
    run (*p, 512, {});
    setParam (*p, P::modId (1, "Dest"), 0.0f);
    p->getParameters()[96]->setValueNotifyingHost (0.7f);
    run (*p, 512, {});
    check (std::abs (gain->getValue() - 0.7f) < 0.01f, "automation de P1 -> Gain");

    // Etat : l'instrument revient avec ses réglages
    juce::MemoryBlock state;
    p->getStateInformation (state);
    auto q = makePlugin();
    q->setStateInformation (state.getData(), (int) state.getSize());
    check (q->getInner() != nullptr, "instrument recharge avec le projet");
    if (q->getInner() != nullptr)
        check (std::abs (q->getInner()->getParameters()[0]->getValue() - 0.7f) < 0.01f, "reglages de l'instrument recharges");

    // Instrument introuvable : l'état est gardé tel quel
    juce::MemoryBlock brokenState;
    {
        auto xml = juce::AudioProcessor::getXmlFromBinary (state.getData(), (int) state.getSize());
        auto text = xml->toString().replace ("RythmTestSynth.vst3", "RythmMissingSynth.vst3");
        auto bad = juce::parseXML (text);
        juce::AudioProcessor::copyXmlToBinary (*bad, brokenState);
    }
    auto m = makePlugin();
    m->setStateInformation (brokenState.getData(), (int) brokenState.getSize());
    check (m->getInner() == nullptr && m->getLoadError().startsWith ("CANNOT LOAD"), "instrument manquant signale : " + m->getLoadError());
    juce::MemoryBlock again;
    m->getStateInformation (again);
    {
        auto xml = juce::AudioProcessor::getXmlFromBinary (again.getData(), (int) again.getSize());
        check (xml != nullptr && xml->toString().contains ("RythmMissingSynth"), "etat de l'instrument manquant conserve");
    }

    p->unloadPlugin();
    check (p->getInner() == nullptr && p->getLatencySamples() == 0 && p->getRelayedNames().isEmpty(), "instrument retire");
}

static void snapshot (juce::Component& c, const juce::File& dir, const juce::String& name)
{
    if (dir == juce::File())
        return;
    dir.createDirectory();
    const auto img = c.createComponentSnapshot (c.getLocalBounds(), true, 1.0f);
    juce::PNGImageFormat png;
    juce::FileOutputStream os (dir.getChildFile (name + ".png"));
    if (os.openedOk())
    {
        os.setPosition (0);
        os.truncate();
        png.writeImageToStream (img, os);
    }
}

static void testEditor (const juce::File& outputDir)
{
    std::cout << "- interface" << std::endl;
    auto p = makePlugin();
    setParam (*p, P::modeId, 1.0f);
    setParam (*p, P::scaleId, (float) rythm::acid);
    setParam (*p, P::densityId, 85.0f);
    setParam (*p, P::slideId, 25.0f);
    setParam (*p, P::modId (0, "Dest"), 74.0f);
    setParam (*p, P::bendId, 20.0f);

    std::unique_ptr<juce::AudioProcessorEditor> ed (p->createEditorAndMakeActive());
    auto* editor = dynamic_cast<RythmAudioProcessorEditor*> (ed.get());
    check (editor != nullptr, "editeur");
    if (editor == nullptr)
        return;
    auto& facade = editor->getFacade();
    check (editor->getWidth() == 1116 && editor->getHeight() == 720, "ouvert a 90 % : " + juce::String (editor->getWidth()) + " x " + juce::String (editor->getHeight()));

    // Toutes les commandes dans la façade
    bool inside = true;
    for (auto* c : facade.getChildren())
        inside = inside && facade.getLocalBounds().contains (c->getBounds());
    check (inside, "commandes dans la facade");

    // Notes jouées : l'écran les reçoit
    TestPlayHead head;
    head.playing = true;
    run (*p, 48000 * 3, { { 0, true, 45 } }, 512, &head);
    juce::MessageManager::getInstance()->runDispatchLoopUntil (150);
    check (facade.getStream().getNumShown() > 10, "ecran : notes recues (" + juce::String (facade.getStream().getNumShown()) + ")");

    // Un pas du séquenceur cliqué
    auto& grid = facade.getSteps();
    const auto cell = grid.cellBounds (4).getCentre();
    const bool before = p->apvts.getParameter (P::stepId (4))->getValue() > 0.5f;
    grid.mouseDown (juce::MouseEvent (juce::Desktop::getInstance().getMainMouseSource(), cell, {}, 1.0f, 0, 0, 0, 0, &grid, &grid,
                                      juce::Time::getCurrentTime(), cell, juce::Time::getCurrentTime(), 1, false));
    check ((p->apvts.getParameter (P::stepId (4))->getValue() > 0.5f) != before, "clic sur le pas 5");

    // Une note de la gamme cliquée : passe en CUSTOM avec la note en plus
    auto& keys = facade.getKeys();
    const juce::Point<float> k6 ((float) keys.getWidth() / 12.0f * 6.5f, 5.0f);
    keys.mouseDown (juce::MouseEvent (juce::Desktop::getInstance().getMainMouseSource(), k6, {}, 1.0f, 0, 0, 0, 0, &keys, &keys,
                                      juce::Time::getCurrentTime(), k6, juce::Time::getCurrentTime(), 1, false));
    const auto s = p->readSettings();
    check (s.scale == rythm::custom && ((s.customMask >> 6) & 1u) && ((s.customMask >> 1) & 1u), "note ajoutee a la gamme (CUSTOM)");

    juce::MessageManager::getInstance()->runDispatchLoopUntil (100);
    run (*p, 48000, { { 0, true, 45 } }, 512, &head);
    setParam (*p, P::loopId, 1.0f);
    run (*p, 48000, {}, 512, &head);
    juce::MessageManager::getInstance()->runDispatchLoopUntil (150);
    snapshot (*ed, outputDir, "rythm");

    // Vue de l'instrument
    const auto file = testSynthFile();
    if (file.exists())
    {
        juce::String error;
        p->loadFile (file, error);
        editor->showSynth (true);
        check (editor->isShowingSynth() && editor->getInnerEditor() != nullptr, "vue de l'instrument");
        check (editor->getHeight() == RythmAudioProcessorEditor::synthBarHeight + editor->getInnerEditor()->getHeight(), "taille de la vue de l'instrument");
        snapshot (*ed, outputDir, "rythm-synth");
        editor->showSynth (false);
        check (! editor->isShowingSynth() && editor->getWidth() == 1116, "retour a Rythm, meme taille");
        p->unloadPlugin();
    }

    setParam (*p, P::modeId, 0.0f);
    setParam (*p, P::sourceId, 1.0f);
    juce::MessageManager::getInstance()->runDispatchLoopUntil (150);
    snapshot (*ed, outputDir, "rythm-steps");

    editor->setUiScale (0.6f);
    check (editor->getWidth() == 744, "taille 60 %");
    snapshot (*ed, outputDir, "rythm-60");

    p->editorBeingDeleted (editor);
    ed.reset();
}

static void testFuzz()
{
    std::cout << "- reglages au hasard, blocs de taille variable" << std::endl;
    std::mt19937 rng (11);
    std::uniform_real_distribution<float> u (0.0f, 1.0f);
    for (double fs : { 44100.0, 96000.0 })
    {
        auto p = makePlugin (fs, 2048);
        juce::AudioBuffer<float> io (2, 2048);
        TestPlayHead head;
        bool valid = true;
        std::array<int, 128> open {};
        for (int iter = 0; iter < 600; ++iter)
        {
            for (int i = 0; i < 96; ++i)
                if (u (rng) < 0.03f)
                    p->getParameters()[i]->setValueNotifyingHost (u (rng));
            head.playing = u (rng) < 0.7f;
            if (u (rng) < 0.01f) head.ppq = u (rng) * 64.0;
            head.bpm = 60.0 + 180.0 * u (rng);
            p->setPlayHead (&head);
            const int n = 1 + (int) (u (rng) * 2047.0f);
            io.setSize (2, n, false, false, true);
            juce::MidiBuffer midi;
            if (u (rng) < 0.2f)
            {
                const int note = 30 + (int) (u (rng) * 50.0f);
                midi.addEvent (u (rng) < 0.6f ? juce::MidiMessage::noteOn (1, note, (juce::uint8) 90) : juce::MidiMessage::noteOff (1, note), (int) (u (rng) * (float) (n - 1)));
            }
            if (u (rng) < 0.01f)
                midi.addEvent (juce::MidiMessage::allNotesOff (1), 0);
            p->processBlock (io, midi);
            for (const auto m : midi)
            {
                const auto& msg = m.getMessage();
                valid = valid && m.samplePosition >= 0 && m.samplePosition < n;
                if (msg.isNoteOn()) { valid = valid && open[(size_t) msg.getNoteNumber()] == 0; open[(size_t) msg.getNoteNumber()] = 1; }
                if (msg.isNoteOff()) { valid = valid && open[(size_t) msg.getNoteNumber()] == 1; open[(size_t) msg.getNoteNumber()] = 0; }
            }
            if (head.playing)
                head.ppq += n * head.bpm / 60.0 / fs;
        }
        p->setPlayHead (nullptr);
        check (valid, "MIDI coherent a " + juce::String ((int) fs));
    }
}

#include "PresetTests.h"

int main (int argc, char* argv[])
{
    juce::ScopedJuceInitialiser_GUI gui;
    const auto outputDir = argc > 1 ? juce::File::getCurrentWorkingDirectory().getChildFile (juce::String::fromUTF8 (argv[1])) : juce::File();

    testParameters();
    testMidiOut();
    testLoopStateAndDrag();
    testHostedSynth();
    testEditor (outputDir);
    testFuzz();
    testPresetsAndWindowSize<RythmAudioProcessor, RythmAudioProcessorEditor, RythmFacade> (RythmAudioProcessor::transientParameters(), outputDir,
                                                                                           RythmAudioProcessor::getNumFactoryPresets());

    std::cout << checks << " verifications, " << failures << " echec(s)" << std::endl;
    std::cout << (failures == 0 ? "TOUS LES TESTS PASSENT" : juce::String (failures) + " ECHEC(S)") << std::endl;
    return failures == 0 ? 0 : 1;
}
