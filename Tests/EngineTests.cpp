// Tests du moteur de Rythm (sans JUCE).
#include "RythmEngine.h"

#include <cstdio>
#include <functional>
#include <iostream>
#include <map>
#include <set>

using namespace rythm;

static int failures = 0, checks = 0;

static void check (bool ok, const std::string& what)
{
    ++checks;
    if (! ok)
    {
        ++failures;
        std::cout << "  ECHEC : " << what << std::endl;
    }
}

constexpr double sr = 48000.0;
constexpr int block = 512;

/** Une note jouée (après coup), avec sa position absolue en échantillons. */
struct Played
{
    int64_t on = 0, off = -1;
    int note = 0, velocity = 0;
};

struct Run
{
    std::vector<Played> notes;
    std::vector<std::pair<int64_t, int>> bends;
    std::vector<std::tuple<int64_t, int, float>> mods;
    int ons = 0, offs = 0;
    bool consistent = true;      // jamais deux note-on sans note-off entre, jamais de note-off orphelin
    std::string problem;
    std::array<int, 128> open {};
};

/** Fait tourner le moteur : inputs = (échantillon absolu, on?, note). */
static Run runEngine (Engine& e, const Settings& s, int64_t totalSamples, std::vector<std::tuple<int64_t, bool, int>> inputs,
                      Transport t = {}, bool hostClock = false, std::function<void (int64_t, Settings&)> tweak = {})
{
    Run r;
    r.open.fill (-1);
    std::vector<Event> out;
    Settings live = s;
    std::sort (inputs.begin(), inputs.end(), [] (auto& a, auto& b) { return std::get<0> (a) < std::get<0> (b); });

    size_t next = 0;
    for (int64_t pos = 0; pos < totalSamples; pos += block)
    {
        if (tweak)
            tweak (pos, live);
        std::vector<Input> in;
        while (next < inputs.size() && std::get<0> (inputs[next]) < pos + block)
        {
            Input i;
            i.offset = (int) (std::get<0> (inputs[next]) - pos);
            i.on = std::get<1> (inputs[next]);
            i.note = std::get<2> (inputs[next]);
            i.velocity = 100;
            in.push_back (i);
            ++next;
        }
        Transport tt = t;
        if (hostClock)
        {
            tt.playing = true;
            tt.hasPpq = true;
            tt.ppq = t.ppq + (double) pos * t.bpm / 60.0 / sr;
        }
        e.process (block, live, tt, in.data(), (int) in.size(), out);

        for (auto& ev : out)
        {
            const int64_t at = pos + ev.offset;
            if (ev.offset < 0 || ev.offset >= block)
            {
                r.consistent = false;
                r.problem = "offset hors du bloc";
            }
            if (ev.type == Event::noteOn)
            {
                ++r.ons;
                if (r.open[(size_t) ev.note] >= 0)
                {
                    r.consistent = false;
                    r.problem = "note-on sur une note deja tenue (" + std::to_string (ev.note) + ")";
                }
                r.open[(size_t) ev.note] = (int) r.notes.size();
                r.notes.push_back ({ at, -1, ev.note, ev.velocity });
            }
            else if (ev.type == Event::noteOff)
            {
                ++r.offs;
                if (r.open[(size_t) ev.note] < 0)
                {
                    r.consistent = false;
                    r.problem = "note-off orphelin";
                }
                else
                {
                    r.notes[(size_t) r.open[(size_t) ev.note]].off = at;
                    r.open[(size_t) ev.note] = -1;
                }
            }
            else if (ev.type == Event::pitchBend)
            {
                r.bends.push_back ({ at, ev.velocity });
            }
            else
            {
                r.mods.push_back ({ at, ev.slot, ev.value });
            }
        }
    }
    return r;
}

static Settings rhythmSettings()
{
    Settings s;
    s.mode = rhythmMode;
    s.rate = 2;            // 1/16
    s.density = 1.0f;
    s.gate = 0.5f;
    return s;
}

//==============================================================================
static void testRhythmGrid()
{
    std::cout << "- RHYTHM : grille libre, note tenue" << std::endl;
    Engine e;
    e.prepare (sr);
    auto s = rhythmSettings();
    // 120 BPM, 1/16 = 0,125 s = 6000 échantillons. Appui à 1000.
    auto r = runEngine (e, s, 48000, { { 1000, true, 48 }, { 40000, false, 48 } });
    check (r.consistent, "coherence : " + r.problem);
    check (! r.notes.empty() && r.notes.front().on == 1000, "la premiere note tombe sur l'appui");
    bool spacing = true, pitch = true, gate = true;
    for (size_t i = 0; i < r.notes.size(); ++i)
    {
        pitch = pitch && r.notes[i].note == 48;
        if (i > 0)
            spacing = spacing && r.notes[i].on - r.notes[i - 1].on == 6000;
        if (r.notes[i].off >= 0 && r.notes[i].off < 40000)
            gate = gate && std::abs ((r.notes[i].off - r.notes[i].on) - 3000) <= 1;
    }
    check (spacing, "un pas tous les 6000 echantillons");
    check (pitch, "RHYTHM joue la note tenue");
    check (gate, "GATE 50 % : 3000 echantillons");
    check (r.notes.size() == 7, "7 notes entre 1000 et 40000 : " + std::to_string (r.notes.size()));
    bool closed = true;
    for (auto& n : r.notes) closed = closed && n.off >= 0 && n.off <= 40000;
    check (closed, "tout s'arrete au relachement");

    // Densité nulle : rien
    Engine e2;
    e2.prepare (sr);
    s.density = 0.0f;
    auto r2 = runEngine (e2, s, 48000, { { 0, true, 48 } });
    check (r2.ons == 0, "DENSITY 0 : aucune note");
}

static void testRates()
{
    std::cout << "- divisions, triolets, pointes, shuffle" << std::endl;
    const int expected[] = { 24000, 12000, 6000, 3000, 1500 };
    for (int rate = 0; rate < 5; ++rate)
        for (int feel = 0; feel < 3; ++feel)
        {
            Engine e;
            e.prepare (sr);
            auto s = rhythmSettings();
            s.rate = rate;
            s.feel = feel;
            s.gate = 0.3f;
            auto r = runEngine (e, s, 96000, { { 0, true, 60 } });
            const double want = expected[rate] * (feel == tripletFeel ? 2.0 / 3.0 : feel == dottedFeel ? 1.5 : 1.0);
            bool ok = r.notes.size() >= 3 && r.consistent;
            for (size_t i = 1; i < r.notes.size(); ++i)
                ok = ok && std::abs ((double) (r.notes[i].on - r.notes[i - 1].on) - want) <= 1.0;
            check (ok, "espacement rate " + std::to_string (rate) + " feel " + std::to_string (feel));
        }

    Engine e;
    e.prepare (sr);
    auto s = rhythmSettings();
    s.shuffle = 0.5f;
    s.gate = 0.2f;
    auto r = runEngine (e, s, 48000, { { 0, true, 60 } });
    check (r.notes.size() > 4 && r.notes[1].on == 6000 + 1500 && r.notes[2].on == 12000, "SHUFFLE 50 % retarde les pas impairs d'un quart de pas");
}

static void testHostGrid()
{
    std::cout << "- calage sur le transport de l'hote" << std::endl;
    // Transport à 120 BPM : la double croche 1/16 = 6000 échantillons ; ppq de départ 0
    Transport t;
    t.bpm = 120.0;
    t.ppq = 0.0;
    {
        Engine e;
        e.prepare (sr);
        auto s = rhythmSettings();
        auto r = runEngine (e, s, 48000, { { 7000, true, 50 } }, t, true);
        check (r.consistent, "coherence");
        check (! r.notes.empty() && r.notes.front().on == 12000, "appui entre deux pas : on attend le pas suivant");
        bool grid = true;
        for (auto& n : r.notes) grid = grid && n.on % 6000 == 0;
        check (grid, "toutes les notes sur la grille de l'hote");
    }
    {
        Engine e;
        e.prepare (sr);
        auto s = rhythmSettings();
        auto r = runEngine (e, s, 48000, { { 12000, true, 50 } }, t, true);
        check (! r.notes.empty() && r.notes.front().on == 12000, "appui pile sur un pas : il joue tout de suite");
    }
    {
        // Saut du transport (boucle de Live) : on repart sans note bloquée
        Engine e;
        e.prepare (sr);
        auto s = rhythmSettings();
        std::vector<Event> out;
        Transport tt = t;
        tt.playing = true;
        tt.hasPpq = true;
        Input in { 0, true, 60, 100 };
        int ons = 0;
        for (int b = 0; b < 400; ++b)
        {
            tt.ppq = std::fmod ((double) b * block * 2.0 / sr, 2.0);   // boucle de 2 temps
            e.process (block, s, tt, b == 0 ? &in : nullptr, b == 0 ? 1 : 0, out);
            for (auto& ev : out) if (ev.type == Event::noteOn) ++ons;
        }
        // 400 blocs de 512 = 4,27 s = 34 doubles-croches à 120 BPM, à peu près autant de notes
        check (ons >= 30 && ons <= 38, "boucle du transport : rythme continu (" + std::to_string (ons) + ")");
    }
}

static void testSteps()
{
    std::cout << "- STEPS : sequenceur" << std::endl;
    Engine e;
    e.prepare (sr);
    auto s = rhythmSettings();
    s.source = stepsSource;
    s.numSteps = 4;
    s.steps.fill (false);
    s.steps[0] = true;
    s.steps[3] = true;
    auto r = runEngine (e, s, 6000 * 12 - 1000, { { 0, true, 40 } });
    std::set<int64_t> want = { 0, 18000, 24000, 42000, 48000, 66000 };
    std::set<int64_t> got;
    for (auto& n : r.notes) got.insert (n.on);
    std::string gotText;
    for (auto t : got) gotText += std::to_string (t) + " ";
    check (got == want, "pas 1 et 4 sur 4 : " + gotText);

    // FILL 100 % : tous les pas
    Engine e2;
    e2.prepare (sr);
    s.fill = 1.0f;
    auto r2 = runEngine (e2, s, 6000 * 8 - 1000, { { 0, true, 40 } });
    check (r2.notes.size() == 8, "FILL 100 % remplit tout");
}

static void testMelodyScales()
{
    std::cout << "- MELODY : gammes, portee, direction" << std::endl;
    for (int scale = 0; scale < numScales; ++scale)
    {
        if (scale == heldNotes)
            continue;
        Engine e;
        e.prepare (sr);
        e.setSeed (1234 + (uint64_t) scale);
        Settings s = rhythmSettings();
        s.mode = melodyMode;
        s.scale = scale;
        s.customMask = 0b000010001001;   // 0, 3, 7
        s.chance = 1.0f;
        s.range = 12;
        s.octave = 0.3f;
        s.stack = 0.3f;
        s.rate = 4;
        s.gate = 0.4f;
        const int tonic = 57;
        auto r = runEngine (e, s, 48000 * 4, { { 0, true, tonic } });
        const auto mask = Engine::effectiveMask (s, { tonic });
        bool inScale = true;
        std::set<int> distinct;
        for (auto& n : r.notes)
        {
            const int rel = ((n.note - tonic) % 12 + 12) % 12;
            if (! ((mask >> rel) & 1u))
                inScale = false;
            distinct.insert (n.note);
        }
        check (r.consistent, std::string ("coherence ") + scaleName (scale) + " : " + r.problem);
        check (inScale, std::string ("notes hors gamme : ") + scaleName (scale));
        check (distinct.size() >= 4, std::string ("melodie trop pauvre : ") + scaleName (scale));
    }

    // Direction : tout au-dessus de la tonique
    Engine e;
    e.prepare (sr);
    Settings s = rhythmSettings();
    s.mode = melodyMode;
    s.scale = rythm::major;
    s.chance = 1.0f;
    s.direction = 1.0f;
    s.octave = 0.0f;
    s.walk = 0.0f;
    s.range = 7;
    s.rate = 4;
    auto r = runEngine (e, s, 48000 * 2, { { 0, true, 60 } });
    bool up = true, inRange = true;
    for (auto& n : r.notes) { up = up && n.note >= 60; inRange = inRange && n.note <= 72; }
    check (up, "DIRECTION +100 : jamais sous la tonique");
    check (inRange, "RANGE 7 degres en majeur : au plus une octave");

    // CHANCE 0 : seulement la tonique
    Engine e2;
    e2.prepare (sr);
    s.chance = 0.0f;
    auto r2 = runEngine (e2, s, 48000, { { 0, true, 60 } });
    bool tonicOnly = ! r2.notes.empty();
    for (auto& n : r2.notes) tonicOnly = tonicOnly && n.note == 60;
    check (tonicOnly, "CHANCE 0 : seulement la tonique");

    // HELD NOTES : seulement les notes tenues (et leurs octaves)
    Engine e3;
    e3.prepare (sr);
    s.chance = 1.0f;
    s.scale = heldNotes;
    s.direction = 0.0f;
    s.octave = 0.2f;
    auto r3 = runEngine (e3, s, 48000 * 2, { { 0, true, 50 }, { 0, true, 53 }, { 0, true, 57 } });
    bool held = ! r3.notes.empty();
    for (auto& n : r3.notes) { const int rel = ((n.note - 50) % 12 + 12) % 12; held = held && (rel == 0 || rel == 3 || rel == 7); }
    check (held, "HELD NOTES : notes de l'accord tenu");
}

static void testChordAndStack()
{
    std::cout << "- accords : RHYTHM (notes tenues) et STACK" << std::endl;
    Engine e;
    e.prepare (sr);
    auto s = rhythmSettings();
    auto r = runEngine (e, s, 12000, { { 0, true, 48 }, { 0, true, 55 } });
    int at0 = 0;
    for (auto& n : r.notes) if (n.on == 0) ++at0;
    check (at0 == 2, "RHYTHM avec deux notes tenues : les deux jouent");

    Engine e2;
    e2.prepare (sr);
    s.mode = melodyMode;
    s.chance = 0.0f;
    s.stack = 1.0f;
    s.stackType = stackTriad;
    s.scale = rythm::minor;
    auto r2 = runEngine (e2, s, 6000, { { 0, true, 57 } });
    std::set<int> notes;
    for (auto& n : r2.notes) notes.insert (n.note);
    check (notes == std::set<int> ({ 57, 60, 64 }), "STACK TRIAD en mineur sur A : A C E");
}

static void testSlideAndGlitch()
{
    std::cout << "- SLIDE, GLITCH, SPLIT, groove" << std::endl;
    Engine e;
    e.prepare (sr);
    auto s = rhythmSettings();
    s.mode = melodyMode;
    s.scale = rythm::minor;
    s.chance = 1.0f;
    s.slide = 1.0f;
    s.octave = 0.0f;
    auto r = runEngine (e, s, 48000, { { 0, true, 45 } });
    check (r.consistent, "coherence slide : " + r.problem);
    int overlaps = 0;
    for (size_t i = 1; i < r.notes.size(); ++i)
        if (r.notes[i - 1].off > r.notes[i].on)
            ++overlaps;
    check (overlaps > 3, "SLIDE : les notes se chevauchent");

    // Slide sur la même note : liée (une seule note longue)
    Engine e2;
    e2.prepare (sr);
    s.chance = 0.0f;
    auto r2 = runEngine (e2, s, 30000, { { 0, true, 45 }, { 29000, false, 45 } });
    check (r2.consistent && r2.notes.size() == 1 && r2.notes[0].off == 29000, "SLIDE sur la meme note : une note liee");

    // GLITCH : plusieurs frappes dans un pas
    Engine e3;
    e3.prepare (sr);
    auto g = rhythmSettings();
    g.glitch = 1.0f;
    auto r3 = runEngine (e3, g, 6000 * 4, { { 0, true, 60 } });
    check (r3.consistent && r3.ons >= 8, "GLITCH : repetitions rapides");

    // Groove x4 : une note par mesure de 4 pas
    Engine e4;
    e4.prepare (sr);
    auto l = rhythmSettings();
    l.groove4 = 1.0f;
    auto r4 = runEngine (e4, l, 6000 * 16 - 1000, { { 0, true, 60 } });
    check (r4.notes.size() == 4 && r4.notes[1].on == 24000, "x4 : une note tous les 4 pas (" + std::to_string (r4.notes.size()) + ")");
    check (r4.notes.size() == 4 && std::abs ((r4.notes[0].off - r4.notes[0].on) - 12000) <= 1, "x4 : la duree suit (gate 50 % de 4 pas)");
}

static void testLoop()
{
    std::cout << "- LOOP : capture et repetition" << std::endl;
    Engine e;
    e.prepare (sr);
    auto s = rhythmSettings();
    s.mode = melodyMode;
    s.scale = phrygian;
    s.density = 0.7f;
    s.chance = 0.8f;
    s.slide = 0.2f;
    s.glitch = 0.1f;
    s.groove2 = 0.2f;
    s.loopSteps = 16;
    const int64_t loopOn = 6000 * 40 - 1000;   // juste avant le pas 40 (pas 24..39 capturés)
    auto r = runEngine (e, s, 6000 * 120, { { 0, true, 52 } }, {}, false, [&] (int64_t pos, Settings& live)
    {
        live.loop = pos >= loopOn;
    });
    check (r.consistent, "coherence : " + r.problem);

    // Après l'allumage, période de 16 pas (96000 échantillons)
    std::map<int64_t, int> after;
    for (auto& n : r.notes)
        if (n.on >= 6000 * 44)
            after[n.on] = n.note;
    bool periodic = after.size() > 20;
    for (auto& [t, note] : after)
        if (t + 96000 < 6000 * 116)
        {
            auto it = after.find (t + 96000);
            periodic = periodic && it != after.end() && it->second == note;
        }
    check (periodic, "la boucle se repete tous les 16 pas");

    // La boucle reprend les 16 derniers pas joués avant l'allumage
    std::map<int64_t, int> before;
    for (auto& n : r.notes)
        if (n.on >= 6000 * 24 && n.on < 6000 * 40)
            before[n.on] = n.note;
    bool same = ! before.empty();
    for (auto& [t, note] : before)
    {
        auto it = after.find (t + 96000 * 2);
        same = same && it != after.end() && it->second == note;
    }
    check (same, "la boucle rejoue les 16 pas d'avant LOOP");

    // Transposition : on rejoue la boucle en tenant une autre note
    Engine e2;
    e2.prepare (sr);
    std::vector<Event> out;
    s.loop = false;
    Input in { 0, true, 52, 100 };
    for (int b = 0; b < 200; ++b)
        e2.process (block, s, {}, b == 0 ? &in : nullptr, b == 0 ? 1 : 0, out);
    s.loop = true;
    std::vector<int> first, second;
    for (int b = 0; b < 400; ++b)
    {
        Input ins[2] = { { 0, false, 52, 0 }, { 0, true, 57, 100 } };
        const bool swap = b == 200;
        e2.process (block, s, {}, swap ? ins : nullptr, swap ? 2 : 0, out);
        for (auto& ev : out)
            if (ev.type == Event::noteOn)
                (b < 200 ? first : second).push_back (ev.note);
    }
    // les notes jouées après le changement sont celles d'avant + 5
    bool transposed = first.size() > 10 && second.size() > 10;
    std::set<int> a, bset;
    for (int n : first) a.insert (n + 5);
    for (int n : second) bset.insert (n);
    for (int n : bset) transposed = transposed && a.count (n) > 0;
    check (transposed, "la boucle suit la note tenue (transposition)");

    // Sauvegarde du contenu
    bool roundTrip = ! e2.getLoopContent().empty();
    for (auto& o : e2.getLoopContent())
    {
        Outcome back;
        roundTrip = roundTrip && Outcome::fromString (o.toString(), back);
        roundTrip = roundTrip && back.numHits == o.numHits && back.span == o.span;
        for (int h = 0; h < o.numHits; ++h)
            roundTrip = roundTrip && back.hits[(size_t) h].pitch == o.hits[(size_t) h].pitch && back.hits[(size_t) h].velocity == o.hits[(size_t) h].velocity;
    }
    check (roundTrip, "boucle : texte -> boucle identique");
    Outcome junk;
    check (! Outcome::fromString ("abc", junk) && ! Outcome::fromString ("", junk) && ! Outcome::fromString ("1,5,0", junk), "texte abime refuse");
}

static void testBendAndMods()
{
    std::cout << "- BEND et modulations" << std::endl;
    Engine e;
    e.prepare (sr);
    auto s = rhythmSettings();
    s.bendChance = 1.0f;
    s.bendDepth = -1.0f;
    s.bendShape = bendDive;
    s.mods[0].active = true;
    s.mods[0].min = 0.2f;
    s.mods[0].max = 0.6f;
    s.mods[0].chance = 1.0f;
    s.mods[1].active = true;
    s.mods[1].min = 0.0f;
    s.mods[1].max = 1.0f;
    s.mods[1].chance = 1.0f;
    s.mods[1].smooth = 1.0f;
    auto r = runEngine (e, s, 48000, { { 0, true, 60 }, { 30000, false, 60 } });
    check (! r.bends.empty(), "BEND : messages envoyes");
    int minBend = 16383;
    for (auto& [t, v] : r.bends) if (t < 30000) minBend = std::min (minBend, v);
    check (minBend < 1000, "DIVE -100 % descend jusqu'en bas");
    check (! r.bends.empty() && r.bends.back().second == 8192, "BEND : retour au centre a l'arret");
    bool inRange = true;
    int slot1 = 0;
    for (auto& [t, slot, v] : r.mods)
    {
        if (slot == 0) inRange = inRange && v >= 0.2f - 1e-5f && v <= 0.6f + 1e-5f;
        if (slot == 1) ++slot1;
    }
    check (inRange, "MOD 1 entre MIN et MAX");
    check (slot1 > 20, "MOD 2 lisse : plusieurs valeurs par pas");

    // Scoop : la note commence décalée (bend envoyé avant la note-on)
    Engine e2;
    e2.prepare (sr);
    s.bendShape = bendScoop;
    s.bendDepth = 0.5f;
    std::vector<Event> out;
    Input in { 100, true, 60, 100 };
    e2.process (block, s, {}, &in, 1, out);
    int bendIndex = -1, onIndex = -1;
    for (int i = 0; i < (int) out.size(); ++i)
    {
        if (out[(size_t) i].type == Event::pitchBend && bendIndex < 0) bendIndex = i;
        if (out[(size_t) i].type == Event::noteOn && onIndex < 0) onIndex = i;
    }
    check (bendIndex >= 0 && onIndex >= 0 && bendIndex < onIndex && out[(size_t) bendIndex].offset == 100
           && out[(size_t) bendIndex].velocity > 8192 + 3000, "SCOOP : bend avant la note");
}

static void testAcid()
{
    std::cout << "- ACID : slides, accents, octaves" << std::endl;
    Engine e;
    e.prepare (sr);
    auto s = rhythmSettings();
    s.mode = melodyMode;
    s.scale = acid;
    s.chance = 0.6f;
    s.octave = 0.0f;
    s.slide = 0.0f;
    s.accent = 0.0f;
    s.velocity = 90.0f;
    auto r = runEngine (e, s, 48000 * 8, { { 0, true, 36 } });
    int accents = 0, octaves = 0, overlaps = 0;
    for (size_t i = 0; i < r.notes.size(); ++i)
    {
        if (r.notes[i].velocity == 127) ++accents;
        if (r.notes[i].note >= 48) ++octaves;
        if (i > 0 && r.notes[i - 1].off > r.notes[i].on) ++overlaps;
    }
    const int n = (int) r.notes.size();
    check (r.consistent, "coherence acid : " + r.problem);
    check (accents > n / 6, "ACID : des accents meme a ACCENT 0");
    check (octaves > n / 8, "ACID : des sauts d'octave");
    check (overlaps > n / 10, "ACID : des slides");
}

static void testFuzz()
{
    std::cout << "- reglages au hasard (notes jamais bloquees)" << std::endl;
    Random rnd (777);
    bool allOk = true;
    std::string problem;
    for (int run = 0; run < 120; ++run)
    {
        Engine e;
        const double rates[] = { 22050.0, 44100.0, 48000.0, 96000.0, 192000.0 };
        const double rate = rates[rnd.below (5)];
        e.prepare (rate);
        e.setSeed ((uint64_t) run + 1);
        Settings s;
        s.mode = rnd.below (2);
        s.rate = rnd.below (5);
        s.feel = rnd.below (3);
        s.source = rnd.below (2);
        s.density = rnd.unit();
        s.fill = rnd.unit() * 0.5f;
        s.shuffle = rnd.unit();
        s.human = rnd.unit();
        s.glitch = rnd.unit() * 0.5f;
        s.groove2 = rnd.unit() * 0.5f;
        s.groove4 = rnd.unit() * 0.3f;
        s.split = rnd.unit() * 0.5f;
        s.numSteps = 1 + rnd.below (32);
        for (auto& st : s.steps) st = rnd.chance (0.5f);
        s.gate = 0.05f + rnd.unit() * 0.95f;
        s.gateRandom = rnd.unit();
        s.velocity = 1.0f + rnd.unit() * 126.0f;
        s.velocityRandom = rnd.unit();
        s.accent = rnd.unit();
        s.scale = rnd.below (numScales);
        s.customMask = (uint16_t) rnd.below (4096);
        s.chance = rnd.unit();
        s.direction = rnd.unit() * 2.0f - 1.0f;
        s.walk = rnd.unit();
        s.octave = rnd.unit();
        s.slide = rnd.unit();
        s.stack = rnd.unit();
        s.range = 1 + rnd.below (24);
        s.stackType = rnd.below (numStacks);
        s.bendChance = rnd.unit();
        s.bendDepth = rnd.unit() * 2.0f - 1.0f;
        s.bendShape = rnd.below (numBendShapes);
        s.bendCycles = 1.0f + rnd.unit() * 7.0f;
        for (auto& m : s.mods) { m.active = rnd.chance (0.5f); m.min = rnd.unit(); m.max = rnd.unit(); m.chance = rnd.unit(); m.smooth = rnd.unit(); }
        s.loopSteps = 1 + rnd.below (64);

        Transport t;
        t.bpm = 60.0 + rnd.unit() * 200.0;
        const bool host = rnd.chance (0.5f);

        std::vector<std::tuple<int64_t, bool, int>> inputs;
        std::set<int> down;
        for (int i = 0; i < 30; ++i)
        {
            const int note = rnd.chance (0.1f) ? (rnd.chance (0.5f) ? 0 : 127) : 30 + rnd.below (60);
            const int64_t at = (int64_t) rnd.below ((int) (rate * 6));
            const bool on = ! down.count (note);
            if (on) down.insert (note); else down.erase (note);
            inputs.push_back ({ at, on, note });
        }
        for (int note : down)
            inputs.push_back ({ (int64_t) (rate * 6), false, note });

        Random flips ((uint64_t) run * 31 + 7);
        auto r = runEngine (e, s, (int64_t) (rate * 6) + 4096, inputs, t, host, [&] (int64_t, Settings& live)
        {
            if (flips.chance (0.02f)) live.loop = ! live.loop;
            if (flips.chance (0.01f)) live.rate = flips.below (5);
            if (flips.chance (0.01f)) live.loopSteps = 1 + flips.below (64);
        });
        // On arrête : plus rien ne doit sonner
        std::vector<Event> out;
        e.allNotesOff (out);
        int open = 0;
        for (auto& n : r.notes) if (n.off < 0) ++open;
        int closedLater = 0;
        for (auto& ev : out) if (ev.type == Event::noteOff) ++closedLater;

        if (! r.consistent) { allOk = false; problem = r.problem; }
        if (open != closedLater) { allOk = false; problem = "notes ouvertes " + std::to_string (open) + " / fermees " + std::to_string (closedLater); }
        for (auto& n : r.notes)
            if (n.note < 0 || n.note > 127 || n.velocity < 1 || n.velocity > 127) { allOk = false; problem = "note ou velocite hors plage"; }
        for (auto& [t2, v] : r.bends)
            if (v < 0 || v > 16383) { allOk = false; problem = "bend hors plage"; }
        for (auto& [t2, slot, v] : r.mods)
            if (! (v >= 0.0f && v <= 1.0f)) { allOk = false; problem = "mod hors plage"; }
    }
    check (allOk, "fuzz : " + problem);
}

int main()
{
    std::cout << "Rythm : tests du moteur" << std::endl;
    testRhythmGrid();
    testRates();
    testHostGrid();
    testSteps();
    testMelodyScales();
    testChordAndStack();
    testSlideAndGlitch();
    testLoop();
    testBendAndMods();
    testAcid();
    testFuzz();
    std::cout << checks << " verifications, " << failures << " echec(s)" << std::endl;
    return failures == 0 ? 0 : 1;
}
