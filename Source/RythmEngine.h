#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <string>
#include <vector>

/**
    Moteur de Rythm : génération de MIDI par hasard contrôlé (sans JUCE, testable seul).

    Une note tenue en entrée met le moteur en marche ; il joue sur une grille (1/4 à 1/64, triolet, pointé),
    calée sur le transport de l'hôte quand il tourne, sinon depuis l'appui.

      RHYTHM : chaque pas joue la (ou les) note(s) tenue(s) ; seul le rythme, la durée et la vélocité varient.
      MELODY : chaque pas tire une note dans une gamme construite sur la note tenue (la tonique).

    Chaque pas joué devient une « issue » (Outcome) : hauteurs relatives à la tonique, vélocités, durées, bend,
    valeurs des modulations. Les issues sont gardées : LOOP rejoue les N dernières en boucle (transposées si on
    change de note tenue).
*/
namespace rythm
{

//==============================================================================
// Gammes
enum Scale
{
    major, minor, harmonicMinor, melodicMinor, dorian, phrygian, phrygianDominant, lydian, mixolydian, locrian,
    minorPentatonic, majorPentatonic, blues, hungarianMinor, doubleHarmonic, hirajoshi, inSen, wholeTone,
    diminished, chromatic, acid, heldNotes, custom, numScales
};

inline const char* scaleName (int s)
{
    static const char* names[] = { "MAJOR", "MINOR", "HARMONIC MINOR", "MELODIC MINOR", "DORIAN", "PHRYGIAN",
                                   "PHRYGIAN DOMINANT", "LYDIAN", "MIXOLYDIAN", "LOCRIAN", "MINOR PENTATONIC",
                                   "MAJOR PENTATONIC", "BLUES", "HUNGARIAN MINOR", "DOUBLE HARMONIC", "HIRAJOSHI",
                                   "IN SEN", "WHOLE TONE", "DIMINISHED", "CHROMATIC", "ACID", "HELD NOTES", "CUSTOM" };
    return s >= 0 && s < numScales ? names[s] : "?";
}

/** Notes de la gamme (bit n = n demi-tons au-dessus de la tonique). HELD NOTES et CUSTOM : voir Settings. */
inline uint16_t scaleMask (int s)
{
    auto m = [] (std::initializer_list<int> notes) { uint16_t r = 0; for (int n : notes) r = (uint16_t) (r | (1u << n)); return r; };
    switch (s)
    {
        case major:            return m ({ 0, 2, 4, 5, 7, 9, 11 });
        case minor:            return m ({ 0, 2, 3, 5, 7, 8, 10 });
        case harmonicMinor:    return m ({ 0, 2, 3, 5, 7, 8, 11 });
        case melodicMinor:     return m ({ 0, 2, 3, 5, 7, 9, 11 });
        case dorian:           return m ({ 0, 2, 3, 5, 7, 9, 10 });
        case phrygian:         return m ({ 0, 1, 3, 5, 7, 8, 10 });
        case phrygianDominant: return m ({ 0, 1, 4, 5, 7, 8, 10 });
        case lydian:           return m ({ 0, 2, 4, 6, 7, 9, 11 });
        case mixolydian:       return m ({ 0, 2, 4, 5, 7, 9, 10 });
        case locrian:          return m ({ 0, 1, 3, 5, 6, 8, 10 });
        case minorPentatonic:  return m ({ 0, 3, 5, 7, 10 });
        case majorPentatonic:  return m ({ 0, 2, 4, 7, 9 });
        case blues:            return m ({ 0, 3, 5, 6, 7, 10 });
        case hungarianMinor:   return m ({ 0, 2, 3, 6, 7, 8, 11 });
        case doubleHarmonic:   return m ({ 0, 1, 4, 5, 7, 8, 11 });
        case hirajoshi:        return m ({ 0, 2, 3, 7, 8 });
        case inSen:            return m ({ 0, 1, 5, 7, 10 });
        case wholeTone:        return m ({ 0, 2, 4, 6, 8, 10 });
        case diminished:       return m ({ 0, 2, 3, 5, 6, 8, 9, 11 });
        case chromatic:        return 0x0fff;
        case acid:             return m ({ 0, 1, 3, 5, 7, 8, 10 });
        default:               return 1;
    }
}

//==============================================================================
enum Mode     { rhythmMode, melodyMode };
enum Source   { densitySource, stepsSource };
enum Feel     { straightFeel, tripletFeel, dottedFeel };
enum Stack    { stack3rd, stack5th, stackTriad, stack7th, stackOctave, stackPower, numStacks };
enum Bend     { bendDive, bendScoop, bendWobble, bendRandom, numBendShapes };

inline const char* stackName (int s)
{
    static const char* names[] = { "3RD", "5TH", "TRIAD", "7TH", "OCTAVE", "POWER" };
    return s >= 0 && s < numStacks ? names[s] : "?";
}

/** Durée d'un pas (en noires) : 1/4, 1/8, 1/16, 1/32, 1/64, avec triolet (× 2/3) ou pointé (× 3/2). */
inline double stepBeats (int rate, int feel)
{
    const double base = 1.0 / (double) (1 << std::clamp (rate, 0, 4));
    return base * (feel == tripletFeel ? 2.0 / 3.0 : feel == dottedFeel ? 1.5 : 1.0);
}

constexpr int maxSteps = 32;
constexpr int numMods = 4;
constexpr int maxHits = 4;
constexpr int maxStack = 3;

struct ModSettings
{
    bool active = false;
    float min = 0.0f, max = 1.0f, chance = 0.5f, smooth = 0.0f;
};

struct Settings
{
    int mode = rhythmMode;

    // Rythme
    int rate = 2, feel = straightFeel, source = densitySource;
    float density = 0.75f, fill = 0.0f, shuffle = 0.0f, human = 0.0f, glitch = 0.0f;
    float groove2 = 0.0f, groove4 = 0.0f, split = 0.0f;
    int numSteps = 16;
    std::array<bool, maxSteps> steps {};

    // Dynamique
    float gate = 0.6f, gateRandom = 0.0f, velocity = 100.0f, velocityRandom = 0.0f, accent = 0.0f;

    // Mélodie
    int scale = minor;
    uint16_t customMask = 0x0fff;
    float chance = 0.6f, direction = 0.0f, walk = 0.3f, octave = 0.1f, slide = 0.0f, stack = 0.0f;
    int range = 7, stackType = stackTriad;

    // Bend
    float bendChance = 0.0f, bendDepth = -0.5f, bendCycles = 2.0f;
    int bendShape = bendDive;

    std::array<ModSettings, numMods> mods {};

    // Boucle
    bool loop = false;
    int loopSteps = 16;
};

//==============================================================================
/** Une frappe dans un pas (plusieurs en cas de SPLIT ou de GLITCH). */
struct Hit
{
    float start = 0.0f, length = 1.0f;      // fractions de la durée occupée
    int8_t pitch = 0;                       // demi-tons au-dessus de la tonique
    uint8_t velocity = 100;
    uint8_t numStack = 0;
    std::array<int8_t, maxStack> stack {};  // notes ajoutées (demi-tons au-dessus de la tonique)
    bool slide = false, accent = false;
};

/** Ce qu'un pas a joué. span = 0 : pas couvert par une note plus longue, rien à faire. */
struct Outcome
{
    uint8_t span = 1;
    uint8_t numHits = 0;
    bool chord = false;                     // RHYTHM : toutes les notes tenues
    std::array<Hit, maxHits> hits {};
    uint8_t bendShape = 255;                // 255 : pas de bend
    float bendDepth = 0.0f, bendCycles = 2.0f;
    std::array<float, numMods> mod {};      // NaN : la modulation ne change pas
    float jitterMs = 0.0f;

    Outcome() { mod.fill (std::numeric_limits<float>::quiet_NaN()); }

    bool isRest() const noexcept            { return numHits == 0; }

    /** Texte compact (sauvegarde de la boucle avec le projet). */
    std::string toString() const;
    static bool fromString (const std::string&, Outcome&);
};

/** Événement produit par le moteur. */
struct Event
{
    enum Type : uint8_t { noteOff, pitchBend, mod, noteOn };   // ordre = priorité au même instant
    Type type = noteOn;
    int offset = 0;                 // échantillon dans le bloc
    int note = 0, velocity = 0;     // noteOn / noteOff ; pitchBend : velocity = 0..16383
    int slot = 0;                   // mod
    float value = 0.0f;             // mod : 0..1
    double beat = 0.0;              // position (noires) de l'événement
};

/** Note jouée, pour l'affichage. */
struct NoteInfo
{
    double beat = 0.0, length = 0.0;
    int64_t time = 0;               // échantillon absolu (compteur du moteur)
    int note = 60, velocity = 100;
    bool slide = false, accent = false, looped = false;
};

struct Transport
{
    bool playing = false;
    bool hasPpq = false;
    double ppq = 0.0;
    double bpm = 120.0;
    double barBeats = 4.0;
};

/** Note d'entrée (MIDI reçu), à un échantillon du bloc. */
struct Input
{
    int offset = 0;
    bool on = true;
    int note = 60, velocity = 100;
};

//==============================================================================
class Random
{
public:
    explicit Random (uint64_t seed = 0x9e3779b97f4a7c15ull) { setSeed (seed); }
    void setSeed (uint64_t seed) noexcept       { state = seed != 0 ? seed : 0x2545f4914f6cdd1dull; for (int i = 0; i < 4; ++i) next(); }

    uint64_t next() noexcept
    {
        state ^= state >> 12;
        state ^= state << 25;
        state ^= state >> 27;
        return state * 0x2545f4914f6cdd1dull;
    }

    /** [0, 1) */
    float unit() noexcept                      { return (float) ((next() >> 40) * (1.0 / 16777216.0)); }
    bool chance (float p) noexcept             { return p > 0.0f && unit() < p; }
    int below (int n) noexcept                 { return n <= 1 ? 0 : (int) ((next() >> 33) % (uint64_t) n); }

private:
    uint64_t state = 1;
};

//==============================================================================
class Engine
{
public:
    static constexpr int historySize = 4096;

    Engine()
    {
        history.resize ((size_t) historySize);
        historyStep.assign ((size_t) historySize, std::numeric_limits<int64_t>::min());
        offTime.fill (-1);
        // Rien n'est alloué sur le fil audio en temps normal
        loopContent.reserve ((size_t) historySize / 2);
        pending.reserve (512);
        held.reserve (128);
    }

    void prepare (double sampleRate)
    {
        sr = sampleRate > 0.0 ? sampleRate : 44100.0;
        reset();
    }

    /** Coupe tout (sans rien émettre) et oublie l'historique. */
    void reset()
    {
        offTime.fill (-1);
        pending.clear();
        held.clear();
        running = false;
        bendActive = false;
        bendValue = 8192;
        lastDegree = 0;
        occupiedUntil = std::numeric_limits<int64_t>::min();
        lastStep = std::numeric_limits<int64_t>::min();
        std::fill (historyStep.begin(), historyStep.end(), std::numeric_limits<int64_t>::min());
        for (auto& m : modGlide) m = {};
    }

    void setSeed (uint64_t seed) noexcept       { rng.setSeed (seed); }

    //==========================================================================
    /** Un bloc : notes d'entrée (triées) -> événements de sortie (triés). */
    void process (int numSamples, const Settings& s, const Transport& t, const Input* inputs, int numInputs,
                  std::vector<Event>& out, std::vector<NoteInfo>* notes = nullptr)
    {
        out.clear();
        settings = &s;
        noteLog = notes;
        updateLoop (s);

        const double bpm = std::clamp (t.bpm > 0.0 ? t.bpm : 120.0, 20.0, 999.0);
        beatsPerSample = bpm / 60.0 / sr;
        const double step = stepBeats (s.rate, s.feel);
        barBeats = t.barBeats > 0.0 ? t.barBeats : 4.0;

        // Position au début du bloc
        const bool hostClock = t.playing && t.hasPpq;
        if (hostClock)
        {
            const bool jumped = ! lastHostClock || std::abs (t.ppq - expectedBeat) > beatsPerSample * 2.0 + 1.0e-6;
            blockBeat = t.ppq;
            if (jumped || std::abs (step - lastStepBeats) > 1.0e-12)
                resyncGrid (step, blockBeat);
        }
        else
        {
            if (lastHostClock || std::abs (step - lastStepBeats) > 1.0e-12)
                resyncGrid (step, blockBeat);
        }
        lastHostClock = hostClock;
        lastStepBeats = step;

        int pos = 0;
        int inputIndex = 0;
        while (pos < numSamples)
        {
            // Entrées à cet instant
            while (inputIndex < numInputs && inputs[inputIndex].offset <= pos)
                handleInput (inputs[inputIndex++], pos, step, hostClock, out);

            const int nextInput = inputIndex < numInputs ? std::clamp (inputs[inputIndex].offset, pos + 1, numSamples) : numSamples;
            renderSpan (pos, nextInput, step, out);
            pos = nextInput;
        }
        while (inputIndex < numInputs)
            handleInput (inputs[inputIndex++], std::max (0, numSamples - 1), step, hostClock, out);

        now += numSamples;
        blockBeat += numSamples * beatsPerSample;
        expectedBeat = blockBeat;

        std::stable_sort (out.begin(), out.end(), [] (const Event& a, const Event& b)
        {
            return a.offset != b.offset ? a.offset < b.offset : a.type < b.type;
        });
        settings = nullptr;
        noteLog = nullptr;
    }

    /** Arrêt immédiat (bypass, transport arrêté...) : note-off pour tout ce qui sonne. */
    void allNotesOff (std::vector<Event>& out, int offset = 0)
    {
        for (int n = 0; n < 128; ++n)
            if (offTime[(size_t) n] >= 0)
            {
                out.push_back (makeEvent (Event::noteOff, offset, n, 0));
                offTime[(size_t) n] = -1;
            }
        pending.clear();
        if (bendValue != 8192)
        {
            auto e = makeEvent (Event::pitchBend, offset, 0, 8192);
            out.push_back (e);
            bendValue = 8192;
        }
        bendActive = false;
    }

    //==========================================================================
    bool isRunning() const noexcept                 { return running; }
    bool isLooping() const noexcept                 { return looping; }
    int getTonic() const noexcept                   { return held.empty() ? lastTonic : tonicOf (held); }
    double getBeat() const noexcept                 { return blockBeat; }
    /** Compteur d'échantillons (début du prochain bloc). */
    int64_t getNow() const noexcept                 { return now; }
    double getSampleRate() const noexcept           { return sr; }
    int64_t getCurrentStep() const noexcept         { return lastStep; }
    int getLoopLength() const noexcept              { return (int) loopContent.size(); }
    /** Rang du pas joué dans la boucle (-1 hors boucle). */
    int getLoopPosition() const noexcept            { return looping && ! loopContent.empty() && lastStep != std::numeric_limits<int64_t>::min()
                                                               ? (int) (((lastStep % (int64_t) loopContent.size()) + (int64_t) loopContent.size()) % (int64_t) loopContent.size()) : -1; }
    const std::vector<int>& getHeld() const noexcept { return held; }

    const std::vector<Outcome>& getLoopContent() const noexcept { return loopContent; }
    /** Change à chaque nouvelle capture (ou boucle rechargée). */
    uint32_t getLoopVersion() const noexcept        { return loopVersion; }
    /** Recharge une boucle (projet). Elle reprend dès que LOOP est allumé. */
    void setLoopContent (std::vector<Outcome> content)
    {
        loopContent = std::move (content);
        restoredLoop = ! loopContent.empty();
        ++loopVersion;
    }

    /** Toutes les notes de la gamme courante (demi-tons 0..11 au-dessus de la tonique), pour l'affichage. */
    static uint16_t effectiveMask (const Settings& s, const std::vector<int>& heldNotes)
    {
        if (s.scale == rythm::heldNotes)
        {
            if (heldNotes.empty())
                return 1;
            const int tonic = tonicOf (heldNotes);
            uint16_t m = 1;
            for (int n : heldNotes)
                m = (uint16_t) (m | (1u << (((n - tonic) % 12 + 12) % 12)));
            return m;
        }
        if (s.scale == custom)
            return (uint16_t) ((s.customMask | 1u) & 0x0fffu);
        return scaleMask (s.scale);
    }

    static int tonicOf (const std::vector<int>& notes)
    {
        int lowest = 127;
        for (int n : notes)
            lowest = std::min (lowest, n);
        return lowest;
    }

private:
    struct Pending
    {
        int64_t time = 0;
        int note = 0, velocity = 0;
        int64_t offTime = 0;
        bool tie = false;           // même note glissée : on prolonge
        double beat = 0.0, lengthBeats = 0.0;
        bool slide = false, accent = false, looped = false;
    };

    struct BendCurve
    {
        int64_t start = 0, end = 0;
        int shape = bendDive;
        float depth = 0.0f, cycles = 2.0f;
    };

    struct ModGlide
    {
        bool active = false;
        float from = 0.0f, to = 0.0f, current = -1.0f;
        int64_t start = 0, end = 0;
    };

    //==========================================================================
    Event makeEvent (Event::Type type, int offset, int note, int velocity) const
    {
        Event e;
        e.type = type;
        e.offset = offset;
        e.note = note;
        e.velocity = velocity;
        e.beat = blockBeat + offset * beatsPerSample;
        return e;
    }

    static int64_t floorDiv (double beat, double step)    { return (int64_t) std::floor (beat / step + 1.0e-9); }

    double stepStart (int64_t k, double step) const
    {
        const double swing = (k & 1) != 0 ? (double) settings->shuffle * 0.5 * step : 0.0;
        return (double) k * step + swing;
    }

    /** Dernier pas déjà passé (début < beat), pour ne pas rejouer le pas en cours. */
    void resyncGrid (double step, double beat)
    {
        int64_t k = floorDiv (beat, step) + 1;
        while (k > std::numeric_limits<int64_t>::min() + 2 && stepStart (k, step) >= beat - 1.0e-9)
            --k;
        lastStep = k;
        occupiedUntil = std::min (occupiedUntil, lastStep);
    }

    void handleInput (const Input& in, int offset, double step, bool hostClock, std::vector<Event>& out)
    {
        if (in.note < 0 || in.note > 127)
            return;

        const bool wasRunning = ! held.empty();
        auto it = std::find (held.begin(), held.end(), in.note);
        if (in.on && in.velocity > 0)
        {
            if (it == held.end())
                held.push_back (in.note);
        }
        else if (it != held.end())
        {
            held.erase (it);
        }

        if (! held.empty())
            lastTonic = tonicOf (held);

        if (! wasRunning && ! held.empty())
        {
            running = true;
            lastDegree = 0;
            const double beat = blockBeat + offset * beatsPerSample;
            if (! hostClock)
            {
                // Départ libre : le pas 0 tombe sur l'appui
                blockBeat = -offset * beatsPerSample;
                lastStep = -1;
                occupiedUntil = std::numeric_limits<int64_t>::min();
            }
            else
            {
                // Calé sur la grille : un pas qui vient de commencer (moins d'une demi-milliseconde) joue tout de suite
                const double tolerance = 0.0005 * sr * beatsPerSample;
                int64_t k = floorDiv (beat, step) + 1;
                while (stepStart (k, step) >= beat - tolerance)
                    --k;
                lastStep = k;
                occupiedUntil = std::numeric_limits<int64_t>::min();
            }
        }
        else if (wasRunning && held.empty())
        {
            running = false;
            allNotesOff (out, offset);
        }
    }

    void renderSpan (int from, int to, double step, std::vector<Event>& out)
    {
        const double beatFrom = blockBeat + from * beatsPerSample;
        const double beatTo = blockBeat + to * beatsPerSample;

        // Pas qui commencent dans [from, to)
        if (running)
        {
            for (;;)
            {
                const int64_t k = lastStep + 1;
                const double start = stepStart (k, step);
                if (start >= beatTo - 1.0e-12)
                    break;
                lastStep = k;
                if (start < beatFrom - 1.0e-9 - beatsPerSample)
                    continue;   // en retard de plus d'un échantillon (ne devrait pas arriver)
                const int offset = std::clamp ((int) std::floor ((start - blockBeat) / beatsPerSample + 1.0e-6), from, to - 1);
                triggerStep (k, offset, step);
            }
        }

        flushPending (from, to, out);
        renderBend (from, to, out);
        renderMods (from, to, out);
    }

    //==========================================================================
    void triggerStep (int64_t k, int offset, double step)
    {
        const auto& s = *settings;
        Outcome o;

        if (looping && ! loopContent.empty())
        {
            const auto len = (int64_t) loopContent.size();
            o = loopContent[(size_t) (((k % len) + len) % len)];
        }
        else if (k < occupiedUntil)
        {
            o.span = 0;   // couvert par une note longue
        }
        else
        {
            o = generate (k);
        }

        record (k, o);

        if (o.span > 1 && ! (looping && ! loopContent.empty()))
            occupiedUntil = k + o.span;

        if (o.span == 0 || o.isRest())
            return;

        schedule (o, offset, step, looping && ! loopContent.empty());
        (void) s;
    }

    void record (int64_t k, const Outcome& o)
    {
        const auto i = (size_t) (((k % historySize) + historySize) % historySize);
        history[i] = o;
        historyStep[i] = k;
    }

    //==========================================================================
    Outcome generate (int64_t k)
    {
        const auto& s = *settings;
        Outcome o;

        bool play = false;
        if (s.source == stepsSource)
        {
            const int n = std::clamp (s.numSteps, 1, maxSteps);
            const int idx = (int) (((k % n) + n) % n);
            play = s.steps[(size_t) idx] || rng.chance (s.fill);
        }
        else
        {
            play = rng.chance (s.density);
        }

        if (! play)
        {
            o.span = 1;
            o.numHits = 0;
            return o;
        }

        // Durée occupée : ×4, ×2 ou 1 pas
        o.span = rng.chance (s.groove4) ? 4 : rng.chance (s.groove2) ? 2 : 1;
        o.chord = s.mode == rhythmMode;

        const bool isAcid = s.mode == melodyMode && s.scale == acid;
        const float accentChance = isAcid ? std::max (s.accent, 0.3f) : s.accent;
        const float slideChance = isAcid ? std::max (s.slide, 0.2f) : s.slide;

        // Frappes : GLITCH (même note répétée), SPLIT (deux notes) ou une seule
        int hits = 1;
        bool repeat = false;
        if (rng.chance (s.glitch))      { hits = 2 + rng.below (3); repeat = true; }
        else if (rng.chance (s.split))  { hits = 2; }
        o.numHits = (uint8_t) hits;

        for (int h = 0; h < hits; ++h)
        {
            auto& hit = o.hits[(size_t) h];
            hit.start = (float) h / (float) hits;
            const float slot = 1.0f / (float) hits;

            if (h > 0 && repeat)
            {
                hit = o.hits[0];
                hit.start = (float) h / (float) hits;
                hit.length = slot * 0.6f;
                hit.slide = false;
                hit.velocity = (uint8_t) std::clamp ((int) hit.velocity - 12 * h, 1, 127);
                continue;
            }

            hit.pitch = (int8_t) (s.mode == melodyMode ? choosePitch (s, isAcid) : 0);

            float vel = s.velocity;
            if (s.velocityRandom > 0.0f)
                vel -= s.velocityRandom * rng.unit() * 80.0f;
            hit.accent = rng.chance (accentChance);
            if (hit.accent)
                vel = 127.0f;
            hit.velocity = (uint8_t) std::clamp ((int) std::lround (vel), 1, 127);

            float gate = s.gate;
            if (s.gateRandom > 0.0f)
                gate *= 1.0f - s.gateRandom * rng.unit() * 0.9f;
            hit.length = slot * std::clamp (gate, 0.02f, 1.0f);

            hit.slide = rng.chance (slideChance);
            if (repeat)
            {
                hit.length = slot * 0.6f;
                hit.slide = false;
            }

            // Notes ajoutées (accords dans la gamme)
            if (rng.chance (s.stack))
                fillStack (s, hit);
        }

        if (rng.chance (s.bendChance))
        {
            o.bendShape = (uint8_t) (s.bendShape == bendRandom ? rng.below (3) : std::clamp (s.bendShape, 0, 2));
            o.bendDepth = s.bendDepth;
            o.bendCycles = s.bendCycles;
        }

        for (int m = 0; m < numMods; ++m)
        {
            const auto& ms = s.mods[(size_t) m];
            if (ms.active && rng.chance (ms.chance))
                o.mod[(size_t) m] = ms.min + (ms.max - ms.min) * rng.unit();
        }

        if (s.human > 0.0f)
            o.jitterMs = s.human * 14.0f * rng.unit();

        return o;
    }

    /** Degrés de la gamme courante (demi-tons 0..11, triés). */
    struct Degrees
    {
        std::array<int, 12> notes {};
        int count = 0;
        int operator[] (size_t i) const noexcept    { return notes[i]; }
        size_t size() const noexcept                { return (size_t) count; }
    };

    Degrees degrees (const Settings& s) const
    {
        const auto mask = effectiveMask (s, held);
        Degrees d;
        for (int n = 0; n < 12; ++n)
            if ((mask >> n) & 1u)
                d.notes[(size_t) d.count++] = n;
        if (d.count == 0)
            d.notes[(size_t) d.count++] = 0;
        return d;
    }

    static int semitoneOf (const Degrees& d, int degree)
    {
        const int n = d.count;
        const int octave = (int) std::floor ((double) degree / n);
        return d[(size_t) (degree - octave * n)] + 12 * octave;
    }

    int choosePitch (const Settings& s, bool isAcid)
    {
        const auto d = degrees (s);
        const int range = std::clamp (s.range, 1, 36);
        const float upBias = std::clamp ((s.direction + 1.0f) * 0.5f, 0.0f, 1.0f);
        const float chance = isAcid ? s.chance * 0.8f : s.chance;

        int degree = 0;
        if (rng.chance (chance))
        {
            if (lastDegree != 0 && rng.chance (s.walk))
            {
                // Pas à pas depuis la note précédente
                const int size = rng.chance (0.7f) ? 1 : 2;
                degree = lastDegree + (rng.chance (upBias) ? size : -size);
            }
            else
            {
                // Saut depuis la tonique, les petits intervalles plus souvent
                const int magnitude = 1 + (int) std::floor (std::pow (rng.unit(), 1.35f) * (float) range);
                degree = rng.chance (upBias) ? magnitude : -magnitude;
            }
            const int lo = s.direction > 0.95f ? 0 : -range;
            const int hi = s.direction < -0.95f ? 0 : range;
            degree = std::clamp (degree, lo, hi);
        }
        lastDegree = degree;

        int semitones = semitoneOf (d, degree);

        // Saut d'octave (ACID : surtout vers le haut, au moins une fois sur trois)
        const float octaveChance = isAcid ? std::max (s.octave, 0.33f) : s.octave;
        if (rng.chance (octaveChance))
        {
            const float up = isAcid ? 0.8f : upBias;
            semitones += rng.chance (up) ? 12 : -12;
        }

        return std::clamp (semitones, -60, 60);
    }

    void fillStack (const Settings& s, Hit& hit)
    {
        const auto d = degrees (s);
        // Degré de la note jouée dans la gamme (le plus proche par en dessous)
        const int n = (int) d.size();
        const int pitch = hit.pitch;
        int base = 0;
        for (int deg = -6 * n; deg <= 6 * n; ++deg)
            if (semitoneOf (d, deg) <= pitch)
                base = deg;
        const int offsetFromScale = pitch - semitoneOf (d, base);

        auto add = [&] (int semis)
        {
            if (hit.numStack < maxStack)
                hit.stack[hit.numStack++] = (int8_t) std::clamp (semis + offsetFromScale, -60, 72);
        };

        switch (s.stackType)
        {
            case stack3rd:    add (semitoneOf (d, base + 2)); break;
            case stack5th:    add (semitoneOf (d, base + 4)); break;
            case stackTriad:  add (semitoneOf (d, base + 2)); add (semitoneOf (d, base + 4)); break;
            case stack7th:    add (semitoneOf (d, base + 2)); add (semitoneOf (d, base + 4)); add (semitoneOf (d, base + 6)); break;
            case stackOctave: add (semitoneOf (d, base) + 12); break;
            case stackPower:  add (pitch + 7 - offsetFromScale); add (semitoneOf (d, base) + 12); break;
            default: break;
        }
    }

    //==========================================================================
    void schedule (const Outcome& o, int offset, double step, bool looped)
    {
        const double spanBeats = step * o.span;
        const double spb = 1.0 / beatsPerSample;
        const int64_t base = now + offset + (int64_t) std::llround (o.jitterMs * 0.001 * sr);
        const double baseBeat = blockBeat + offset * beatsPerSample + o.jitterMs * 0.001 * sr * beatsPerSample;
        const int64_t overlap = (int64_t) std::llround (0.003 * sr);

        std::array<int, 128> roots {};
        int numRoots = 0;
        if (o.chord)
            for (int h : held)
                roots[(size_t) numRoots++] = h;
        else
            roots[(size_t) numRoots++] = tonicOf (held);

        for (int h = 0; h < o.numHits; ++h)
        {
            const auto& hit = o.hits[(size_t) h];
            const int64_t start = base + (int64_t) std::llround (hit.start * spanBeats * spb);
            const double slotBeats = spanBeats / o.numHits;
            int64_t end = base + (int64_t) std::llround ((hit.start * spanBeats + hit.length * spanBeats) * spb);
            if (hit.slide)
                end = base + (int64_t) std::llround ((hit.start * spanBeats + slotBeats) * spb) + overlap;
            end = std::max (end, start + 8);

            for (int r = 0; r < numRoots; ++r)
            {
                const int root = roots[(size_t) r];
                auto push = [&] (int note)
                {
                    if (note < 0 || note > 127)
                        return;
                    Pending p;
                    p.time = start;
                    p.note = note;
                    p.velocity = hit.velocity;
                    p.offTime = end;
                    p.tie = false;
                    p.beat = baseBeat + hit.start * spanBeats;
                    p.lengthBeats = (double) (end - start) * beatsPerSample;
                    p.slide = hit.slide;
                    p.accent = hit.accent;
                    p.looped = looped;
                    pending.push_back (p);
                };
                push (root + hit.pitch);
                if (! o.chord || r == 0)
                    for (int i = 0; i < hit.numStack; ++i)
                        push (root + hit.stack[(size_t) i]);
            }
        }

        std::stable_sort (pending.begin(), pending.end(), [] (const Pending& a, const Pending& b) { return a.time < b.time; });

        // Bend sur la première frappe ; sinon retour au centre au début de la note
        const int64_t firstStart = base;
        const int64_t firstEnd = base + (int64_t) std::llround ((o.hits[0].start + o.hits[0].length) * spanBeats * spb);
        if (o.bendShape != 255)
        {
            bendCurve = { firstStart, std::max (firstEnd, firstStart + 64), (int) o.bendShape, o.bendDepth, o.bendCycles };
            bendActive = true;
        }
        else if (bendValue != 8192 || bendActive)
        {
            bendCurve = { firstStart, firstStart + 1, -1, 0.0f, 0.0f };
            bendActive = true;
        }

        // Modulations
        for (int m = 0; m < numMods; ++m)
        {
            const float target = o.mod[(size_t) m];
            if (std::isnan (target))
                continue;
            auto& g = modGlide[(size_t) m];
            const float smooth = settings->mods[(size_t) m].smooth;
            g.from = g.current < 0.0f ? target : g.current;
            g.to = target;
            g.start = firstStart;
            g.end = firstStart + (smooth > 0.0f ? (int64_t) std::llround (smooth * spanBeats * spb) : 0);
            g.active = true;
        }
    }

    void flushPending (int from, int to, std::vector<Event>& out)
    {
        const int64_t t0 = now + from, t1 = now + to;

        // Note-on à jouer
        for (size_t i = 0; i < pending.size();)
        {
            auto& p = pending[i];
            if (p.time >= t1)
            {
                ++i;
                continue;
            }
            const int offset = (int) std::clamp<int64_t> (p.time - now, from, to - 1);
            auto& off = offTime[(size_t) p.note];
            if (off >= 0 && off <= p.time)
            {
                // la note précédente finit avant (ou pile quand) celle-ci commence
                out.push_back (makeEvent (Event::noteOff, (int) std::clamp<int64_t> (off - now, from, offset), p.note, 0));
                off = -1;
            }
            if (off < 0)
                out.push_back (makeEvent (Event::noteOn, offset, p.note, p.velocity));
            // sinon : la précédente déborde (glissée) sur la même note -> liée, on la prolonge
            off = std::max (p.offTime, p.time + 1);

            if (noteLog != nullptr)
            {
                NoteInfo n;
                n.beat = p.beat;
                n.time = p.time;
                n.length = p.lengthBeats;
                n.note = p.note;
                n.velocity = p.velocity;
                n.slide = p.slide;
                n.accent = p.accent;
                n.looped = p.looped;
                noteLog->push_back (n);
            }

            pending.erase (pending.begin() + (std::ptrdiff_t) i);
        }

        // Note-off
        for (int n = 0; n < 128; ++n)
        {
            auto& off = offTime[(size_t) n];
            if (off >= 0 && off < t1)
            {
                const int offset = (int) std::clamp<int64_t> (off - now, from, to - 1);
                out.push_back (makeEvent (Event::noteOff, offset, n, 0));
                off = -1;
            }
        }
        (void) t0;
    }

    void renderBend (int from, int to, std::vector<Event>& out)
    {
        if (! bendActive)
            return;

        const int64_t t0 = now + from, t1 = now + to;
        const auto& c = bendCurve;
        if (c.start >= t1)
            return;

        auto valueAt = [&c] (int64_t t) -> int
        {
            if (c.shape < 0)
                return 8192;
            const double x = std::clamp ((double) (t - c.start) / (double) std::max<int64_t> (1, c.end - c.start), 0.0, 1.0);
            double v = 0.0;
            switch (c.shape)
            {
                case bendDive:   v = std::pow (x, 1.6); break;
                case bendScoop:  { const double y = std::min (1.0, x / 0.35); v = (1.0 - y) * (1.0 - y); break; }
                case bendWobble: v = std::sin (2.0 * 3.14159265358979323846 * c.cycles * x); break;
                default: break;
            }
            return std::clamp ((int) std::lround (8192.0 + v * c.depth * 8191.0), 0, 16383);
        };

        // Un message tous les 64 échantillons (et au début de la courbe, avant la note)
        constexpr int64_t grid = 64;
        int64_t t = std::max (t0, c.start);
        if (t > c.start)
            t = c.start + ((t - c.start + grid - 1) / grid) * grid;
        for (; t < t1 && t <= c.end; t += grid)
        {
            const int v = valueAt (t);
            if (v != bendValue)
            {
                auto e = makeEvent (Event::pitchBend, (int) (t - now), 0, v);
                out.push_back (e);
                bendValue = v;
            }
        }
        if (t1 > c.end)
        {
            const int v = valueAt (c.end);
            if (v != bendValue)
            {
                out.push_back (makeEvent (Event::pitchBend, (int) std::clamp<int64_t> (c.end - now, from, to - 1), 0, v));
                bendValue = v;
            }
            bendActive = false;
        }
    }

    void renderMods (int from, int to, std::vector<Event>& out)
    {
        const int64_t t1 = now + to;
        for (int m = 0; m < numMods; ++m)
        {
            auto& g = modGlide[(size_t) m];
            if (! g.active || g.start >= t1)
                continue;

            auto emit = [&] (int64_t t, float v)
            {
                if (std::abs (v - g.current) < 1.0e-4f)
                    return;
                Event e = makeEvent (Event::mod, (int) std::clamp<int64_t> (t - now, from, to - 1), 0, 0);
                e.slot = m;
                e.value = v;
                out.push_back (e);
                g.current = v;
            };

            if (g.end <= g.start)
            {
                emit (g.start, g.to);
                g.active = false;
                continue;
            }

            constexpr int64_t grid = 128;
            int64_t t = std::max (now + from, g.start);
            for (; t < t1 && t < g.end; t += grid)
            {
                const float x = (float) (t - g.start) / (float) (g.end - g.start);
                emit (t, g.from + (g.to - g.from) * x);
            }
            if (t1 >= g.end)
            {
                emit (std::max (g.end, now + from), g.to);
                g.active = false;
            }
        }
    }

    //==========================================================================
    void updateLoop (const Settings& s)
    {
        const int len = std::clamp (s.loopSteps, 1, historySize / 2);
        if (s.loop && ! looping)
        {
            if (restoredLoop && ! loopContent.empty())
            {
                // Boucle rechargée avec le projet : on la garde telle quelle
                restoredLoop = false;
            }
            else
            {
                capture (len);
            }
            looping = true;
            loopLengthSetting = len;
        }
        else if (! s.loop && looping)
        {
            looping = false;
            restoredLoop = false;
            occupiedUntil = std::numeric_limits<int64_t>::min();
        }
        else if (looping && len != loopLengthSetting)
        {
            capture (len);
            loopLengthSetting = len;
        }
        else if (! s.loop)
        {
            restoredLoop = restoredLoop && ! loopContent.empty();
        }
    }

    /** Les len derniers pas joués deviennent la boucle, rangés par position sur la grille (case k mod len). */
    void capture (int len)
    {
        ++loopVersion;
        loopContent.assign ((size_t) len, Outcome {});
        for (auto& o : loopContent) { o.span = 1; o.numHits = 0; }
        if (lastStep == std::numeric_limits<int64_t>::min())
            return;
        const int64_t first = lastStep - len + 1;
        int64_t firstSlot = -1;
        for (int i = 0; i < len; ++i)
        {
            const int64_t k = first + i;
            const auto idx = (size_t) (((k % historySize) + historySize) % historySize);
            const auto slot = (size_t) (((k % len) + len) % len);
            if (i == 0)
                firstSlot = (int64_t) slot;
            if (historyStep[idx] == k)
                loopContent[slot] = history[idx];
        }
        // Une note longue coupée au début de la fenêtre : rien plutôt qu'une suite sans début
        if (firstSlot >= 0 && loopContent[(size_t) firstSlot].span == 0)
            loopContent[(size_t) firstSlot].span = 1;
    }

    //==========================================================================
    double sr = 44100.0;
    double beatsPerSample = 120.0 / 60.0 / 44100.0;
    double blockBeat = 0.0, expectedBeat = 0.0, lastStepBeats = 0.25, barBeats = 4.0;
    bool lastHostClock = false;
    int64_t now = 0;

    const Settings* settings = nullptr;
    std::vector<NoteInfo>* noteLog = nullptr;
    Random rng;

    std::vector<int> held;
    int lastTonic = 60;
    bool running = false;

    int64_t lastStep = std::numeric_limits<int64_t>::min();
    int64_t occupiedUntil = std::numeric_limits<int64_t>::min();
    int lastDegree = 0;

    std::array<int64_t, 128> offTime {};
    std::vector<Pending> pending;

    BendCurve bendCurve;
    bool bendActive = false;
    int bendValue = 8192;

    std::array<ModGlide, numMods> modGlide {};

    std::vector<Outcome> history;
    std::vector<int64_t> historyStep;
    std::vector<Outcome> loopContent;
    bool looping = false, restoredLoop = false;
    uint32_t loopVersion = 0;
    int loopLengthSetting = 0;
};

//==============================================================================
inline std::string Outcome::toString() const
{
    // span,hits,chord,bendShape,bendDepth,bendCycles,jitter,m0..m3 ; puis chaque frappe :
    // start,length,pitch,velocity,slide,accent,numStack,s0,s1,s2
    std::string r;
    auto num = [&r] (double v)
    {
        char buf[32];
        if (std::isnan (v)) { r += "n,"; return; }
        std::snprintf (buf, sizeof (buf), "%.4g,", v);
        r += buf;
    };
    num (span); num (numHits); num (chord ? 1 : 0); num (bendShape); num (bendDepth); num (bendCycles); num (jitterMs);
    for (float m : mod) num (m);
    for (int h = 0; h < numHits && h < maxHits; ++h)
    {
        const auto& x = hits[(size_t) h];
        num (x.start); num (x.length); num (x.pitch); num (x.velocity); num (x.slide ? 1 : 0); num (x.accent ? 1 : 0); num (x.numStack);
        for (int i = 0; i < maxStack; ++i) num (x.stack[(size_t) i]);
    }
    if (! r.empty())
        r.pop_back();
    return r;
}

inline bool Outcome::fromString (const std::string& text, Outcome& o)
{
    std::vector<double> v;
    size_t pos = 0;
    while (pos <= text.size())
    {
        const auto comma = text.find (',', pos);
        const auto item = text.substr (pos, comma == std::string::npos ? std::string::npos : comma - pos);
        if (item == "n")
            v.push_back (std::numeric_limits<double>::quiet_NaN());
        else
        {
            char* end = nullptr;
            const double d = std::strtod (item.c_str(), &end);
            if (item.empty() || end == item.c_str())
                return false;
            v.push_back (d);
        }
        if (comma == std::string::npos)
            break;
        pos = comma + 1;
    }

    constexpr size_t head = 7 + numMods, perHit = 7 + maxStack;
    if (v.size() < head)
        return false;
    auto finite = [] (double d, double lo, double hi) { return std::isfinite (d) ? std::clamp (d, lo, hi) : lo; };

    Outcome r;
    r.span = (uint8_t) finite (v[0], 0, 4);
    r.numHits = (uint8_t) finite (v[1], 0, maxHits);
    r.chord = v[2] > 0.5;
    r.bendShape = (uint8_t) (std::isfinite (v[3]) && v[3] < 3 ? (int) v[3] : 255);
    r.bendDepth = (float) finite (v[4], -1, 1);
    r.bendCycles = (float) finite (v[5], 0.25, 16);
    r.jitterMs = (float) finite (v[6], 0, 50);
    for (int m = 0; m < numMods; ++m)
        r.mod[(size_t) m] = std::isnan (v[7 + (size_t) m]) ? std::numeric_limits<float>::quiet_NaN() : (float) finite (v[7 + (size_t) m], 0, 1);
    if (v.size() != head + perHit * r.numHits)
        return false;
    for (int h = 0; h < r.numHits; ++h)
    {
        const size_t b = head + perHit * (size_t) h;
        auto& x = r.hits[(size_t) h];
        x.start = (float) finite (v[b], 0, 0.999);
        x.length = (float) finite (v[b + 1], 0.001, 1);
        x.pitch = (int8_t) finite (v[b + 2], -60, 60);
        x.velocity = (uint8_t) finite (v[b + 3], 1, 127);
        x.slide = v[b + 4] > 0.5;
        x.accent = v[b + 5] > 0.5;
        x.numStack = (uint8_t) finite (v[b + 6], 0, maxStack);
        for (int i = 0; i < maxStack; ++i)
            x.stack[(size_t) i] = (int8_t) finite (v[b + 7 + (size_t) i], -60, 72);
    }
    o = r;
    return true;
}

} // namespace rythm
