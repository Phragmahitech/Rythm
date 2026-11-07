#pragma once

#include "PluginProcessor.h"
#include "PresetBar.h"

#include <deque>

/**
    Look de Rythm : skin « Phragma electricity » (comme Phocus) — panneaux bleu nuit, texte clair,
    bleu clair #3CD2F8, mauve #9850E8, lilas #C898F8, bleu acier #5A8CF0. Police Chakra Petch embarquée.
*/
class RythmLookAndFeel final : public juce::LookAndFeel_V4
{
public:
    RythmLookAndFeel();

    static juce::Colour ground()      { return juce::Colour (0xff0e181c); }
    static juce::Colour panelTop()    { return juce::Colour (0xff1e2f36); }
    static juce::Colour panelBottom() { return juce::Colour (0xff17252b); }
    static juce::Colour edge()        { return juce::Colour (0xff2b4250); }
    static juce::Colour screen()      { return juce::Colour (0xff0b1418); }
    static juce::Colour text()        { return juce::Colour (0xffdde8ee); }
    static juce::Colour dim()         { return juce::Colour (0xff8aa0ac); }
    static juce::Colour accent()      { return juce::Colour (0xff3cd2f8); }
    static juce::Colour mauve()       { return juce::Colour (0xff9850e8); }
    static juce::Colour lilac()       { return juce::Colour (0xffc898f8); }
    static juce::Colour steel()       { return juce::Colour (0xff5a8cf0); }

    static juce::Path chamfered (juce::Rectangle<float> r, float cut);
    static void drawPanel (juce::Graphics&, juce::Rectangle<float> r, const juce::String& title, const juce::Font& font);

    juce::Font font (float height, bool bold, float tracking = 0.0f) const;

    void drawRotarySlider (juce::Graphics&, int x, int y, int width, int height, float sliderPos,
                           float rotaryStartAngle, float rotaryEndAngle, juce::Slider&) override;
    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour&, bool over, bool down) override;
    void drawButtonText (juce::Graphics&, juce::TextButton&, bool over, bool down) override;

    juce::Font getPopupMenuFont() override                  { return font (12.0f, true, 0.06f); }
    void drawPopupMenuBackground (juce::Graphics&, int w, int h) override;
    void drawPopupMenuItem (juce::Graphics&, const juce::Rectangle<int>& area, bool isSeparator, bool isActive,
                            bool isHighlighted, bool isTicked, bool hasSubMenu, const juce::String& text,
                            const juce::String& shortcutKeyText, const juce::Drawable* icon, const juce::Colour* textColour) override;
    void getIdealPopupMenuItemSize (const juce::String& text, bool isSeparator, int standardMenuItemHeight, int& w, int& h) override;
    void drawPopupMenuSectionHeader (juce::Graphics&, const juce::Rectangle<int>& area, const juce::String& sectionName) override;

private:
    juce::Typeface::Ptr semiBoldFace, boldFace;
};

//==============================================================================
/** Écran : les notes jouées défilent (2 mesures), la tonique et la gamme en haut. */
class NoteStream final : public juce::Component,
                         private juce::Timer
{
public:
    NoteStream (RythmAudioProcessor&, RythmLookAndFeel&);
    ~NoteStream() override;

    void paint (juce::Graphics&) override;
    int getNumShown() const noexcept                { return (int) notes.size(); }

private:
    void timerCallback() override;

    RythmAudioProcessor& processor;
    RythmLookAndFeel& lf;
    std::deque<RythmAudioProcessor::DisplayNote> notes;
    float low = 48.0f, high = 72.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (NoteStream)
};

//==============================================================================
/** Les 32 pas du séquenceur (cliquer, ou glisser pour peindre). */
class StepGrid final : public juce::Component,
                       private juce::Timer
{
public:
    StepGrid (RythmAudioProcessor&, RythmLookAndFeel&);
    ~StepGrid() override;

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;

    juce::Rectangle<float> cellBounds (int index) const;
    int cellAt (juce::Point<float>) const;

private:
    void timerCallback() override;
    void setStep (int index, bool on);

    RythmAudioProcessor& processor;
    RythmLookAndFeel& lf;
    std::vector<std::unique_ptr<juce::ParameterAttachment>> attachments;
    std::array<bool, rythm::maxSteps> values {};
    int numSteps = 16, current = -1, lastPainted = -1;
    bool paintValue = true;
    std::unique_ptr<juce::ParameterAttachment> countAttachment;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (StepGrid)
};

//==============================================================================
/** Les 12 notes au-dessus de la tonique : la gamme en cours, cliquables (passe en CUSTOM). */
class ScaleKeys final : public juce::Component,
                        private juce::Timer
{
public:
    ScaleKeys (RythmAudioProcessor&, RythmLookAndFeel&);
    ~ScaleKeys() override;

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    int keyAt (juce::Point<float>) const;

private:
    void timerCallback() override;

    RythmAudioProcessor& processor;
    RythmLookAndFeel& lf;
    uint16_t shownMask = 1;
    int shownTonic = 60;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ScaleKeys)
};

//==============================================================================
/** Zone à glisser vers le DAW : écrit un .mid (la boucle, ou les dernières mesures). */
class DragZone final : public juce::Component,
                       private juce::Timer
{
public:
    DragZone (RythmAudioProcessor&, RythmLookAndFeel&);
    ~DragZone() override;

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;

private:
    void timerCallback() override;

    RythmAudioProcessor& processor;
    RythmLookAndFeel& lf;
    juce::String message;
    bool dragging = false, hover = false;
    int messageTicks = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DragZone)
};

//==============================================================================
/** La façade à taille fixe ; l'éditeur la met à l'échelle. */
class RythmFacade final : public juce::Component,
                          private juce::Timer,
                          private juce::ChangeListener
{
public:
    static constexpr int width = 1240, height = 800;
    static constexpr float defaultScale = 0.9f;   // ouverte à 90 % (1116 x 720)

    explicit RythmFacade (RythmAudioProcessor&);
    ~RythmFacade() override;

    phragma::PresetBar& getPresetBar() noexcept         { return presetBar; }
    std::function<void()> onOpenSynth;

    void paint (juce::Graphics&) override;
    void paintOverChildren (juce::Graphics&) override;

    // Pour les tests
    juce::Component* findControl (const juce::String& componentId);
    void showLoadMenu();
    NoteStream& getStream() noexcept                    { return stream; }
    StepGrid& getSteps() noexcept                       { return stepGrid; }
    ScaleKeys& getKeys() noexcept                       { return scaleKeys; }

private:
    using APVTS = juce::AudioProcessorValueTreeState;

    struct ChoiceSwitch
    {
        juce::OwnedArray<juce::TextButton> buttons;
        std::unique_ptr<juce::ParameterAttachment> attachment;
    };

    struct ChoiceMenu
    {
        juce::TextButton button;
        std::unique_ptr<juce::ParameterAttachment> attachment;
        juce::String prefix;
    };

    void knob (juce::Slider&, const juce::String& id, juce::Rectangle<int> area, const juce::String& label, bool bipolar = false, bool mini = false);
    void toggle (juce::TextButton&, const juce::String& id, juce::Rectangle<int> area);
    void choiceSwitch (ChoiceSwitch&, const juce::String& id, const juce::StringArray& texts, juce::Rectangle<int> area);
    void choiceMenu (ChoiceMenu&, const juce::String& id, juce::Rectangle<int> area, const juce::String& prefix = {});
    void showModMenu (int slot);
    void updateSynthSection();
    void updateEnablement();

    void timerCallback() override;
    void changeListenerCallback (juce::ChangeBroadcaster*) override;

    RythmAudioProcessor& audioProcessor;
    RythmLookAndFeel lookAndFeel;

    NoteStream stream;
    StepGrid stepGrid;
    ScaleKeys scaleKeys;
    DragZone dragZone;

    ChoiceSwitch modeSwitch, rateSwitch, feelSwitch, sourceSwitch, bendShapeSwitch;
    ChoiceMenu scaleMenu, stackMenu, loopLengthMenu;
    juce::TextButton loopButton { "LOOP" };
    juce::TextButton synthLoadButton { "LOAD SYNTH" }, synthOpenButton { "OPEN" }, synthRemoveButton { "REMOVE" };
    juce::TextButton randomStepsButton { "RND" }, clearStepsButton { "CLR" };

    juce::Slider densityKnob, fillKnob, shuffleKnob, humanKnob, glitchKnob, groove2Knob, groove4Knob, splitKnob;
    juce::Slider stepsKnob;
    juce::Slider gateKnob, gateRandomKnob, velocityKnob, velocityRandomKnob, accentKnob;
    juce::Slider chanceKnob, rangeKnob, directionKnob, walkKnob, octaveKnob, slideKnob, stackKnob;
    juce::Slider bendKnob, bendDepthKnob, bendCyclesKnob;
    std::array<juce::TextButton, rythm::numMods> modDestButtons;
    std::array<std::array<juce::Slider, 4>, rythm::numMods> modKnobs;
    std::array<std::unique_ptr<juce::ParameterAttachment>, rythm::numMods> modDestAttachments;

    std::vector<std::unique_ptr<APVTS::SliderAttachment>> sliderAttachments;
    std::vector<std::unique_ptr<APVTS::ButtonAttachment>> buttonAttachments;
    std::unique_ptr<juce::FileChooser> chooser;

    juce::String synthName;
    bool acidShown = false;
    int enablementKey = -1;

    // Barre des presets et de la taille : déclarée en dernier, détruite en premier.
    phragma::PresetBar presetBar;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (RythmFacade)
};

//==============================================================================
/** Deux vues : la façade de Rythm (mise à l'échelle) ou la fenêtre de l'instrument chargé. */
class RythmAudioProcessorEditor final : public juce::AudioProcessorEditor,
                                        private juce::ComponentListener
{
public:
    explicit RythmAudioProcessorEditor (RythmAudioProcessor&);
    ~RythmAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    RythmFacade& getFacade() noexcept                   { return facade; }
    float getUiScale() const                            { return (float) getWidth() / (float) RythmFacade::width; }
    void setUiScale (float scale);

    void showSynth (bool show);
    bool isShowingSynth() const noexcept                { return showingSynth; }
    /** Appelé par le processeur : l'instrument va être remplacé, sa fenêtre doit partir. */
    void dropInnerEditor();
    /** Appelé par le processeur : l'instrument a changé. */
    void innerChanged();
    juce::Component* getInnerEditor() const noexcept    { return innerEditor.get(); }

    static constexpr int synthBarHeight = 48, synthMinWidth = 640;

private:
    void componentMovedOrResized (juce::Component&, bool wasMoved, bool wasResized) override;
    void layoutSynthView();
    void applyRythmSizeLimits();

    RythmAudioProcessor& audioProcessor;
    RythmFacade facade;
    RythmLookAndFeel barLook;
    juce::TextButton backButton { "< RYTHM" }, changeButton { "CHANGE" }, removeButton { "REMOVE" };
    std::unique_ptr<juce::AudioProcessorEditor> innerEditor;
    bool showingSynth = false, sized = false;
    int rythmWidth = RythmFacade::width;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (RythmAudioProcessorEditor)
};
