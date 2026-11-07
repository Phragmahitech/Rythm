# Rythm

Générateur de rythmes et de mélodies MIDI (VST3, macOS + Windows), par Phragma. Inspiré du principe de Rhythmizer
(Futurephonic) : une note tenue, et le plugin invente le reste par hasard contrôlé. Pas d'IA.

- **RHYTHM** : rythme sur une seule note (ou l'accord tenu) — densité ou séquenceur 32 pas, groove (x2, x4, /2), glitch, shuffle, human.
- **MELODY** : mélodie dans une gamme bâtie sur la note tenue — 23 gammes (majeur, mineur, modes, phrygien dominant, hongroise,
  hirajoshi…), **ACID** (règles 303 : sauts d'octave, slides, accents), HELD NOTES, CUSTOM ; chance, portée, direction, walk, octave, slide, accords.
- **Pitch bend**, **4 modulations** (CC ou réglages du synthé chargé), **LOOP** (gardée avec le projet, transposable), **glisser le MIDI** dans le DAW.
- Le MIDI sort vers l'hôte **et** vers un instrument VST3 chargé dans Rythm (128 réglages relayés pour l'automation).

Voir [INSTALLATION.txt](INSTALLATION.txt).

## Compiler

```
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release --target Rythm_VST3
```

Tests : `-DRYTHM_BUILD_TESTS=ON`, puis `ctest --test-dir build -C Release`.

JUCE 8.0.15 (téléchargé à la configuration). Police Chakra Petch (licence OFL, `Assets/ChakraPetch-OFL.txt`).
