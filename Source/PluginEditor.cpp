#include "PluginEditor.h"
#include "BinaryData.h"

using P = RythmAudioProcessor;
using L = RythmLookAndFeel;

namespace
{
    const char* noteNames[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };

    juce::String noteName (int note)
    {
        note = juce::jlimit (0, 127, note);
        return juce::String (noteNames[note % 12]) + juce::String (note / 12 - 2);   // C3 = 60, comme Live
    }

    juce::Colour fromProperty (const juce::var& v)  { return juce::Colour ((juce::uint32) (juce::int64) v); }

    /** Texte avec un point médian (UTF-8). */
    juce::String dot()                              { return juce::String (juce::CharPointer_UTF8 ("  \xc2\xb7  ")); }
}

//==============================================================================
// Look
RythmLookAndFeel::RythmLookAndFeel()
{
    semiBoldFace = juce::Typeface::createSystemTypefaceFor (BinaryData::ChakraPetchSemiBold_ttf, (size_t) BinaryData::ChakraPetchSemiBold_ttfSize);
    boldFace = juce::Typeface::createSystemTypefaceFor (BinaryData::ChakraPetchBold_ttf, (size_t) BinaryData::ChakraPetchBold_ttfSize);

    setColour (juce::PopupMenu::backgroundColourId, juce::Colour (0xff142229));
    setColour (juce::PopupMenu::textColourId, text());
    setColour (juce::PopupMenu::highlightedBackgroundColourId, accent());
    setColour (juce::ResizableWindow::backgroundColourId, ground());
}

juce::Font RythmLookAndFeel::font (float height, bool bold, float tracking) const
{
    const juce::Font base (juce::FontOptions (bold ? boldFace : semiBoldFace).withPointHeight (height));
    return tracking > 0.0f ? base.withExtraKerningFactor (tracking) : base;
}

juce::Path RythmLookAndFeel::chamfered (juce::Rectangle<float> r, float cut)
{
    cut = juce::jmin (cut, r.getWidth() * 0.5f, r.getHeight() * 0.5f);
    juce::Path p;
    p.startNewSubPath (r.getX() + cut, r.getY());
    p.lineTo (r.getRight() - cut, r.getY());
    p.lineTo (r.getRight(), r.getY() + cut);
    p.lineTo (r.getRight(), r.getBottom() - cut);
    p.lineTo (r.getRight() - cut, r.getBottom());
    p.lineTo (r.getX() + cut, r.getBottom());
    p.lineTo (r.getX(), r.getBottom() - cut);
    p.lineTo (r.getX(), r.getY() + cut);
    p.closeSubPath();
    return p;
}

void RythmLookAndFeel::drawPanel (juce::Graphics& g, juce::Rectangle<float> r, const juce::String& title, const juce::Font& f)
{
    const auto shape = chamfered (r, 8.0f);
    juce::DropShadow (juce::Colours::black.withAlpha (0.45f), 10, { 0, 3 }).drawForPath (g, shape);
    g.setGradientFill (juce::ColourGradient (panelTop(), 0.0f, r.getY(), panelBottom(), 0.0f, r.getBottom(), false));
    g.fillPath (shape);
    g.setColour (edge());
    g.strokePath (shape, juce::PathStrokeType (1.0f));
    g.setColour (juce::Colours::white.withAlpha (0.06f));
    g.drawHorizontalLine ((int) r.getY() + 1, r.getX() + 8.0f, r.getRight() - 8.0f);

    if (title.isNotEmpty())
    {
        g.setFont (f);
        g.setColour (lilac());
        g.drawText (title, r.withHeight (22.0f).translated (12.0f, 4.0f).withWidth (r.getWidth() - 24.0f), juce::Justification::centredLeft, false);
    }
}

void RythmLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos,
                                         float start, float end, juce::Slider& s)
{
    const bool mini = (bool) s.getProperties().getWithDefault ("mini", false);
    const bool bipolar = (bool) s.getProperties().getWithDefault ("bipolar", false);
    const auto colour = s.getProperties().contains ("colour") ? fromProperty (s.getProperties()["colour"]) : accent();
    const bool enabled = s.isEnabled();
    const float alpha = (float) s.getProperties().getWithDefault ("dim", 1.0f);

    auto area = juce::Rectangle<float> ((float) x, (float) y, (float) w, (float) h);

    const float nameH = mini ? 0.0f : 13.0f, valueH = mini ? 12.0f : 14.0f;
    auto nameArea = area.removeFromTop (nameH);
    auto valueArea = area.removeFromBottom (valueH);

    if (! mini)
    {
        g.setColour ((enabled ? dim() : dim().withAlpha (0.4f)).withMultipliedAlpha (alpha));
        g.setFont (font (9.5f, true, 0.18f));
        g.drawText (s.getName(), nameArea, juce::Justification::centred, false);
    }

    g.setColour ((enabled ? text() : text().withAlpha (0.35f)).withMultipliedAlpha (alpha));
    g.setFont (font (mini ? 9.0f : 10.5f, true, 0.04f));
    g.drawText (s.getTextFromValue (s.getValue()), valueArea, juce::Justification::centred, false);

    const float radius = juce::jmin (area.getWidth(), area.getHeight()) * 0.5f - (mini ? 2.0f : 3.0f);
    const auto c = area.getCentre();
    const float arcR = radius, bodyR = radius - (mini ? 4.5f : 5.5f);
    const float thick = mini ? 3.0f : 3.5f;

    juce::Path track;
    track.addCentredArc (c.x, c.y, arcR, arcR, 0.0f, start, end, true);
    g.setColour (juce::Colour (0xff081014).withMultipliedAlpha (alpha));
    g.strokePath (track, juce::PathStrokeType (thick, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    const float angle = start + pos * (end - start);
    const float from = bipolar ? start + 0.5f * (end - start) : start;
    if (std::abs (angle - from) > 0.001f)
    {
        juce::Path value;
        value.addCentredArc (c.x, c.y, arcR, arcR, 0.0f, juce::jmin (from, angle), juce::jmax (from, angle), true);
        juce::ColourGradient grad (colour, c.x - arcR, c.y + arcR, mauve(), c.x + arcR, c.y - arcR, false);
        if (colour != accent())
            grad = juce::ColourGradient (colour, c.x - arcR, c.y, colour.brighter (0.25f), c.x + arcR, c.y, false);
        g.setGradientFill (grad);
        g.setOpacity ((enabled ? 1.0f : 0.35f) * alpha);
        g.strokePath (value, juce::PathStrokeType (thick, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.setOpacity (1.0f);
    }

    juce::Path body;
    body.addEllipse (juce::Rectangle<float> (bodyR * 2.0f, bodyR * 2.0f).withCentre (c));
    juce::DropShadow (juce::Colours::black.withAlpha (0.6f * alpha), mini ? 4 : 6, { 0, 2 }).drawForPath (g, body);
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff2a3f4a).withMultipliedAlpha (alpha), c.x, c.y - bodyR,
                                             juce::Colour (0xff111c22).withMultipliedAlpha (alpha), c.x, c.y + bodyR, false));
    g.fillPath (body);
    g.setColour (lilac().withAlpha ((enabled ? 0.85f : 0.35f) * alpha));
    g.drawEllipse (juce::Rectangle<float> (bodyR * 2.0f, bodyR * 2.0f).withCentre (c), 1.0f);

    const auto tip = c.getPointOnCircumference (bodyR - (mini ? 2.0f : 3.0f), angle);
    const auto base = c.getPointOnCircumference (bodyR * 0.25f, angle);
    g.setColour ((enabled ? text() : text().withAlpha (0.35f)).withMultipliedAlpha (alpha));
    g.drawLine ({ base, tip }, mini ? 1.5f : 1.8f);
}

void RythmLookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour&, bool over, bool down)
{
    const auto area = b.getLocalBounds().toFloat().reduced (0.5f);
    const auto shape = chamfered (area, juce::jmin (5.0f, area.getHeight() * 0.25f));
    const bool on = b.getToggleState();
    const auto tint = b.getProperties().contains ("colour") ? fromProperty (b.getProperties()["colour"]) : accent();

    if (on)
    {
        juce::ColourGradient grad (tint, area.getX(), area.getY(), tint == accent() ? mauve() : tint.darker (0.3f), area.getRight(), area.getBottom(), false);
        g.setGradientFill (grad);
        g.fillPath (shape);
        g.setColour (juce::Colours::white.withAlpha (over ? 0.25f : 0.12f));
        g.strokePath (shape, juce::PathStrokeType (1.0f));
    }
    else
    {
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xff1a2a31), 0.0f, area.getY(), juce::Colour (0xff121e24), 0.0f, area.getBottom(), false));
        g.fillPath (shape);
        g.setColour (over ? tint.withAlpha (0.8f) : edge());
        g.strokePath (shape, juce::PathStrokeType (1.0f));
    }

    if (down)
    {
        g.setColour (juce::Colours::black.withAlpha (0.2f));
        g.fillPath (shape);
    }
}

void RythmLookAndFeel::drawButtonText (juce::Graphics& g, juce::TextButton& b, bool, bool)
{
    const bool on = b.getToggleState();
    auto colour = on ? juce::Colour (0xff0b1216) : text();
    if (! b.isEnabled())
        colour = colour.withAlpha (0.3f);
    g.setColour (colour);
    const float size = (float) b.getProperties().getWithDefault ("textSize", juce::jlimit (8.5f, 11.0f, (float) b.getHeight() * 0.42f));
    g.setFont (font (size, true, 0.14f));
    g.drawFittedText (b.getButtonText(), b.getLocalBounds().reduced (3, 1), juce::Justification::centred, 2, 0.8f);
}

void RythmLookAndFeel::drawPopupMenuBackground (juce::Graphics& g, int w, int h)
{
    g.fillAll (juce::Colour (0xff142229));
    g.setColour (edge());
    g.drawRect (0, 0, w, h, 1);
}

void RythmLookAndFeel::drawPopupMenuItem (juce::Graphics& g, const juce::Rectangle<int>& area, bool isSeparator, bool isActive,
                                          bool isHighlighted, bool isTicked, bool hasSubMenu, const juce::String& itemText,
                                          const juce::String&, const juce::Drawable*, const juce::Colour*)
{
    if (isSeparator)
    {
        g.setColour (edge());
        g.fillRect (area.reduced (10, 0).withHeight (1).withY (area.getCentreY()));
        return;
    }

    auto r = area.reduced (3, 1);
    if (isHighlighted && isActive)
    {
        g.setGradientFill (juce::ColourGradient (accent(), (float) r.getX(), 0.0f, mauve(), (float) r.getRight(), 0.0f, false));
        g.fillRect (r);
    }
    if (isTicked)
    {
        g.setColour (isHighlighted ? juce::Colours::white : accent());
        g.fillRect (juce::Rectangle<float> (6.0f, 6.0f).withCentre ({ (float) r.getX() + 12.0f, (float) r.getCentreY() }));
    }
    g.setColour (! isActive ? dim().withAlpha (0.6f) : isHighlighted ? juce::Colours::white : text());
    g.setFont (getPopupMenuFont());
    g.drawText (itemText, r.withTrimmedLeft (24).withTrimmedRight (hasSubMenu ? 22 : 10), juce::Justification::centredLeft, true);
    if (hasSubMenu)
        g.drawText (">", r.removeFromRight (18), juce::Justification::centred, false);
}

void RythmLookAndFeel::getIdealPopupMenuItemSize (const juce::String& itemText, bool isSeparator, int, int& w, int& h)
{
    if (isSeparator) { w = 60; h = 9; return; }
    h = 24;
    w = juce::jmax (150, (int) std::ceil (juce::GlyphArrangement::getStringWidth (getPopupMenuFont(), itemText)) + 52);
}

void RythmLookAndFeel::drawPopupMenuSectionHeader (juce::Graphics& g, const juce::Rectangle<int>& area, const juce::String& sectionName)
{
    g.setColour (lilac());
    g.setFont (font (10.0f, true, 0.2f));
    g.drawText (sectionName, area.reduced (12, 0), juce::Justification::bottomLeft, true);
}

//==============================================================================
// Écran
NoteStream::NoteStream (RythmAudioProcessor& p, RythmLookAndFeel& l) : processor (p), lf (l)
{
    setInterceptsMouseClicks (false, false);
    startTimerHz (30);
}

NoteStream::~NoteStream()
{
    stopTimer();
}

void NoteStream::timerCallback()
{
    std::vector<RythmAudioProcessor::DisplayNote> fresh;
    processor.popDisplayNotes (fresh);
    for (auto& n : fresh)
        notes.push_back (n);

    const double now = processor.getDisplayBeat();
    const double window = processor.getDisplayBarBeats() * 2.0;
    while (! notes.empty() && notes.front().beat + notes.front().length < now - window - 1.0)
        notes.pop_front();
    while (notes.size() > 600)
        notes.pop_front();

    // Hauteurs affichées : on suit les notes (avec un peu de marge), lentement
    float lo = 1000.0f, hi = -1000.0f;
    for (auto& n : notes)
    {
        lo = juce::jmin (lo, (float) n.note);
        hi = juce::jmax (hi, (float) n.note);
    }
    if (notes.empty())
    {
        lo = (float) processor.getTonic() - 6.0f;
        hi = (float) processor.getTonic() + 18.0f;
    }
    lo -= 3.0f;
    hi += 3.0f;
    if (hi - lo < 24.0f)
    {
        const float mid = 0.5f * (lo + hi);
        lo = mid - 12.0f;
        hi = mid + 12.0f;
    }
    low += (lo - low) * 0.15f;
    high += (hi - high) * 0.15f;
    repaint();
}

void NoteStream::paint (juce::Graphics& g)
{
    const auto area = getLocalBounds().toFloat();
    const auto shape = L::chamfered (area, 8.0f);
    g.setColour (L::screen());
    g.fillPath (shape);

    g.saveState();
    g.reduceClipRegion (shape);

    const auto plot = area.reduced (10.0f, 0.0f).withTrimmedTop (30.0f).withTrimmedBottom (10.0f);
    const double now = processor.getDisplayBeat();
    const double bar = processor.getDisplayBarBeats();
    const double window = bar * 2.0;
    const double left = now - window * 0.88, right = now + window * 0.12;
    auto xFor = [&] (double beat) { return plot.getX() + (float) ((beat - left) / (right - left)) * plot.getWidth(); };
    auto yFor = [&] (float note)  { return plot.getBottom() - (note - low) / juce::jmax (1.0f, high - low) * plot.getHeight(); };

    // Grille : temps et mesures (calées sur l'hôte quand il joue)
    const double phase = processor.getDisplayBarPhase();
    for (double b = std::floor (left + phase) - phase; b <= right; b += 0.25)
    {
        const double rel = b + phase;
        const bool isBar = std::abs (rel / bar - std::round (rel / bar)) < 1.0e-6;
        const bool isBeat = std::abs (rel - std::round (rel)) < 1.0e-6;
        g.setColour (isBar ? L::edge().brighter (0.3f) : isBeat ? L::edge() : L::edge().withAlpha (0.35f));
        g.fillRect (xFor (b), plot.getY(), isBar ? 1.5f : 1.0f, plot.getHeight());
    }

    // Lignes de la tonique
    const int tonic = processor.getTonic();
    for (int o = -4; o <= 6; ++o)
    {
        const float note = (float) (tonic + 12 * o);
        if (note < low || note > high)
            continue;
        const float y = yFor (note) - 2.0f;
        g.setColour (L::steel().withAlpha (o == 0 ? 0.35f : 0.16f));
        g.fillRect (plot.getX(), y, plot.getWidth(), 1.0f);
    }

    // Notes
    const float rowH = juce::jlimit (3.0f, 9.0f, plot.getHeight() / juce::jmax (1.0f, high - low) * 0.8f);
    const RythmAudioProcessor::DisplayNote* previous = nullptr;
    for (auto& n : notes)
    {
        const float x0 = xFor (n.beat), x1 = juce::jmax (x0 + 2.0f, xFor (juce::jmin (n.beat + n.length, now)));
        if (x1 < plot.getX() - 4.0f || x0 > plot.getRight())
        {
            previous = &n;
            continue;
        }
        const float y = yFor ((float) n.note) - rowH * 0.5f;
        const float level = 0.45f + 0.55f * (float) n.velocity / 127.0f;
        auto colour = n.looped ? L::mauve().interpolatedWith (L::lilac(), 0.3f) : L::accent();
        if (n.accent)
            colour = colour.brighter (0.5f);
        juce::Rectangle<float> r (x0, y, x1 - x0, rowH);
        g.setColour (colour.withAlpha (level * 0.25f));
        g.fillRoundedRectangle (r.expanded (1.5f), 2.5f);
        g.setGradientFill (juce::ColourGradient (colour.withAlpha (level), r.getX(), 0.0f, colour.interpolatedWith (L::mauve(), 0.5f).withAlpha (level), r.getRight(), 0.0f, false));
        g.fillRoundedRectangle (r, 1.5f);

        // Slide : un trait vers la note suivante
        if (previous != nullptr && previous->slide && n.beat - previous->beat < 1.0 && n.beat <= previous->beat + previous->length + 0.01)
        {
            const float xp = xFor (n.beat);
            g.setColour (L::lilac().withAlpha (0.8f));
            g.drawLine (xp - 4.0f, yFor ((float) previous->note), xp, y + rowH * 0.5f, 1.4f);
        }
        previous = &n;
    }

    // Tête de lecture
    const float xNow = xFor (now);
    g.setGradientFill (juce::ColourGradient (L::accent().withAlpha (0.0f), xNow - 40.0f, 0.0f, L::accent().withAlpha (0.18f), xNow, 0.0f, false));
    g.fillRect (xNow - 40.0f, plot.getY(), 40.0f, plot.getHeight());
    g.setColour (L::accent().withAlpha (0.9f));
    g.fillRect (xNow, plot.getY(), 1.5f, plot.getHeight());

    g.restoreState();

    // Texte
    const auto settings = processor.readSettings();
    juce::String line;
    if (processor.isRunning())
    {
        line = "TONIC " + noteName (tonic) + dot();
        line << (settings.mode == rythm::rhythmMode ? juce::String ("RHYTHM") : juce::String (rythm::scaleName (settings.scale)));
    }
    else
    {
        line = settings.mode == rythm::rhythmMode ? "HOLD A NOTE" + dot() + "RHYTHM ON ONE NOTE"
                                                  : "HOLD A NOTE" + dot() + "IT BECOMES THE TONIC OF " + juce::String (rythm::scaleName (settings.scale));
    }
    g.setColour (processor.isRunning() ? L::text() : L::dim());
    g.setFont (lf.font (11.0f, true, 0.16f));
    g.drawText (line, area.withHeight (28.0f).withTrimmedLeft (14.0f).withTrimmedRight (220.0f),
                juce::Justification::centredLeft, true);

    juce::String status;
    if (processor.isLooping())
        status = "LOOP  " + juce::String (processor.getLoopPosition() + 1) + " / " + juce::String (processor.getLoopSteps());
    else
        status = "LIVE";
    g.setColour (processor.isLooping() ? L::lilac() : L::dim());
    g.drawText (status, area.withHeight (28.0f).withTrimmedRight (14.0f), juce::Justification::centredRight, false);

    if (processor.isLooping() && processor.getLoopSteps() > 0)
    {
        const float t = (float) (processor.getLoopPosition() + 1) / (float) processor.getLoopSteps();
        auto barArea = juce::Rectangle<float> (area.getRight() - 214.0f, 12.0f, 100.0f, 4.0f);
        g.setColour (L::edge());
        g.fillRect (barArea);
        g.setGradientFill (juce::ColourGradient (L::accent(), barArea.getX(), 0.0f, L::mauve(), barArea.getRight(), 0.0f, false));
        g.fillRect (barArea.withWidth (barArea.getWidth() * t));
    }

    g.setColour (L::edge());
    g.strokePath (shape, juce::PathStrokeType (1.0f));
}

//==============================================================================
// Séquenceur
StepGrid::StepGrid (RythmAudioProcessor& p, RythmLookAndFeel& l) : processor (p), lf (l)
{
    for (int i = 0; i < rythm::maxSteps; ++i)
    {
        auto* prm = processor.apvts.getParameter (P::stepId (i));
        attachments.push_back (std::make_unique<juce::ParameterAttachment> (*prm, [this, i] (float v)
        {
            values[(size_t) i] = v > 0.5f;
            repaint();
        }));
        attachments.back()->sendInitialUpdate();
    }
    countAttachment = std::make_unique<juce::ParameterAttachment> (*processor.apvts.getParameter (P::numStepsId), [this] (float v)
    {
        numSteps = juce::jlimit (1, rythm::maxSteps, juce::roundToInt (v));
        repaint();
    });
    countAttachment->sendInitialUpdate();
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
    startTimerHz (30);
}

StepGrid::~StepGrid()
{
    stopTimer();
}

void StepGrid::timerCallback()
{
    const int next = processor.isRunning() && processor.readSettings().source == rythm::stepsSource ? processor.getCurrentStepIndex() : -1;
    if (next != current)
    {
        current = next;
        repaint();
    }
}

juce::Rectangle<float> StepGrid::cellBounds (int index) const
{
    const float gap = 4.0f;
    const float w = ((float) getWidth() - gap * 15.0f) / 16.0f;
    const float h = ((float) getHeight() - gap) / 2.0f;
    const int row = index / 16, col = index % 16;
    // un petit écart de plus tous les 4 pas
    return { (float) col * (w + gap), (float) row * (h + gap), w, h };
}

int StepGrid::cellAt (juce::Point<float> pt) const
{
    for (int i = 0; i < rythm::maxSteps; ++i)
        if (cellBounds (i).expanded (2.0f).contains (pt))
            return i;
    return -1;
}

void StepGrid::paint (juce::Graphics& g)
{
    const float alpha = isEnabled() ? 1.0f : 0.5f;
    for (int i = 0; i < rythm::maxSteps; ++i)
    {
        const auto r = cellBounds (i);
        const auto shape = L::chamfered (r, 3.0f);
        const bool active = i < numSteps;
        const bool on = values[(size_t) i];
        const float a = alpha * (active ? 1.0f : 0.3f);

        if (on)
        {
            g.setGradientFill (juce::ColourGradient (L::accent().withAlpha (a), r.getX(), r.getY(), L::mauve().withAlpha (a), r.getRight(), r.getBottom(), false));
            g.fillPath (shape);
        }
        else
        {
            g.setColour ((i % 4 == 0 ? juce::Colour (0xff1d2f37) : juce::Colour (0xff15232a)).withAlpha (a));
            g.fillPath (shape);
        }
        g.setColour ((i == current ? L::text() : L::edge()).withAlpha (a));
        g.strokePath (shape, juce::PathStrokeType (i == current ? 2.0f : 1.0f));

        g.setColour ((on ? juce::Colour (0xff0b1216) : L::dim()).withAlpha (a * 0.8f));
        g.setFont (lf.font (8.5f, true, 0.05f));
        g.drawText (juce::String (i + 1), r, juce::Justification::centred, false);
    }
}

void StepGrid::setStep (int index, bool on)
{
    if (! juce::isPositiveAndBelow (index, rythm::maxSteps))
        return;
    attachments[(size_t) index]->setValueAsCompleteGesture (on ? 1.0f : 0.0f);
}

void StepGrid::mouseDown (const juce::MouseEvent& e)
{
    const int i = cellAt (e.position);
    if (i < 0)
        return;
    paintValue = ! values[(size_t) i];
    lastPainted = i;
    setStep (i, paintValue);
}

void StepGrid::mouseDrag (const juce::MouseEvent& e)
{
    const int i = cellAt (e.position);
    if (i >= 0 && i != lastPainted)
    {
        lastPainted = i;
        setStep (i, paintValue);
    }
}

void StepGrid::mouseUp (const juce::MouseEvent&)
{
    lastPainted = -1;
}

//==============================================================================
// Gamme
ScaleKeys::ScaleKeys (RythmAudioProcessor& p, RythmLookAndFeel& l) : processor (p), lf (l)
{
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
    startTimerHz (15);
}

ScaleKeys::~ScaleKeys()
{
    stopTimer();
}

void ScaleKeys::timerCallback()
{
    const auto s = processor.readSettings();
    // Sans note tenue, on montre la gamme choisie (HELD NOTES : seulement la tonique)
    const uint16_t mask = processor.isRunning() ? processor.getScaleMask() : rythm::Engine::effectiveMask (s, {});
    const int tonic = processor.getTonic();
    if (mask != shownMask || tonic != shownTonic)
    {
        shownMask = mask;
        shownTonic = tonic;
        repaint();
    }
}

int ScaleKeys::keyAt (juce::Point<float> pt) const
{
    const float w = (float) getWidth() / 12.0f;
    return juce::jlimit (0, 11, (int) (pt.x / w));
}

void ScaleKeys::paint (juce::Graphics& g)
{
    const float w = (float) getWidth() / 12.0f;
    for (int k = 0; k < 12; ++k)
    {
        auto r = juce::Rectangle<float> ((float) k * w, 0.0f, w, (float) getHeight()).reduced (1.0f, 0.0f);
        const bool on = ((shownMask >> k) & 1u) != 0;
        const int note = (shownTonic + k) % 12;
        const bool black = note == 1 || note == 3 || note == 6 || note == 8 || note == 10;
        const auto shape = L::chamfered (r, 3.0f);
        if (on)
        {
            g.setGradientFill (juce::ColourGradient (k == 0 ? L::lilac() : L::accent(), r.getX(), r.getY(), L::mauve(), r.getRight(), r.getBottom(), false));
            g.fillPath (shape);
        }
        else
        {
            g.setColour (black ? juce::Colour (0xff0d171b) : juce::Colour (0xff18272e));
            g.fillPath (shape);
        }
        g.setColour (L::edge());
        g.strokePath (shape, juce::PathStrokeType (1.0f));
        g.setColour (on ? juce::Colour (0xff0b1216) : L::dim());
        g.setFont (lf.font (8.5f, true, 0.02f));
        g.drawText (noteNames[note], r, juce::Justification::centred, false);
    }
}

void ScaleKeys::mouseDown (const juce::MouseEvent& e)
{
    const int k = keyAt (e.position);
    if (k == 0)
        return;   // la tonique reste dans la gamme

    // On part de la gamme affichée, on bascule la note, et on passe en CUSTOM
    auto& apvts = processor.apvts;
    const auto s = processor.readSettings();
    const uint16_t base = s.scale == rythm::custom ? s.customMask : rythm::Engine::effectiveMask (s, processor.isRunning() ? std::vector<int> {} : std::vector<int> {});
    const uint16_t mask = s.scale == rythm::heldNotes ? (uint16_t) processor.getScaleMask() : base;
    const uint16_t next = (uint16_t) (mask ^ (1u << k));

    for (int i = 1; i < 12; ++i)
        if (auto* prm = apvts.getParameter (P::keyId (i)))
        {
            const float v = ((next >> i) & 1u) ? 1.0f : 0.0f;
            if (std::abs (prm->getValue() - v) > 0.5f)
            {
                prm->beginChangeGesture();
                prm->setValueNotifyingHost (v);
                prm->endChangeGesture();
            }
        }
    if (auto* scale = dynamic_cast<juce::AudioParameterChoice*> (apvts.getParameter (P::scaleId)); scale != nullptr && scale->getIndex() != rythm::custom)
    {
        scale->beginChangeGesture();
        scale->setValueNotifyingHost (scale->convertTo0to1 ((float) rythm::custom));
        scale->endChangeGesture();
    }
    shownMask = next;
    repaint();
}

//==============================================================================
// Glisser le MIDI
DragZone::DragZone (RythmAudioProcessor& p, RythmLookAndFeel& l) : processor (p), lf (l)
{
    setMouseCursor (juce::MouseCursor::DraggingHandCursor);
    startTimerHz (10);
}

DragZone::~DragZone()
{
    stopTimer();
}

void DragZone::timerCallback()
{
    const bool over = isMouseOver();
    if (messageTicks > 0 && --messageTicks == 0)
        message = {};
    if (over != hover || messageTicks >= 0)
    {
        hover = over;
        repaint();
    }
}

void DragZone::paint (juce::Graphics& g)
{
    auto area = getLocalBounds().toFloat().reduced (0.5f);
    const auto shape = L::chamfered (area, 6.0f);
    g.setColour (L::screen());
    g.fillPath (shape);
    juce::Path dashed;
    const float dashes[] = { 4.0f, 3.0f };
    juce::PathStrokeType (1.2f).createDashedStroke (dashed, shape, dashes, 2);
    g.setColour (hover || dragging ? L::accent() : L::edge().brighter (0.4f));
    g.fillPath (dashed);

    // Flèche
    const auto c = area.getCentre().translated (0.0f, -10.0f);
    juce::Path arrow;
    arrow.startNewSubPath (c.x, c.y - 10.0f);
    arrow.lineTo (c.x, c.y + 8.0f);
    arrow.startNewSubPath (c.x - 7.0f, c.y + 1.0f);
    arrow.lineTo (c.x, c.y + 8.0f);
    arrow.lineTo (c.x + 7.0f, c.y + 1.0f);
    g.setColour (hover ? L::accent() : L::text());
    g.strokePath (arrow, juce::PathStrokeType (2.0f, juce::PathStrokeType::mitered, juce::PathStrokeType::rounded));

    g.setFont (lf.font (9.5f, true, 0.16f));
    g.setColour (message.isNotEmpty() ? L::lilac() : L::dim());
    const auto text = message.isNotEmpty() ? message : (processor.isLooping() ? juce::String ("DRAG LOOP") : juce::String ("DRAG MIDI"));
    g.drawFittedText (text, area.removeFromBottom (24.0f).toNearestInt().reduced (4, 0), juce::Justification::centred, 2, 0.8f);
}

void DragZone::mouseDown (const juce::MouseEvent&)
{
    dragging = false;
}

void DragZone::mouseDrag (const juce::MouseEvent& e)
{
    if (dragging || e.getDistanceFromDragStart() < 6)
        return;
    dragging = true;

    juce::String description;
    const auto file = processor.writeMidiForDrag (description);
    message = description;
    messageTicks = 25;
    repaint();
    if (! file.existsAsFile())
        return;

    juce::Component::SafePointer<DragZone> safe (this);
    juce::DragAndDropContainer::performExternalDragDropOfFiles ({ file.getFullPathName() }, false, this, [safe]
    {
        if (safe != nullptr)
        {
            safe->dragging = false;
            safe->repaint();
        }
    });
}

void DragZone::mouseUp (const juce::MouseEvent&)
{
    dragging = false;
    repaint();
}

//==============================================================================
// Façade
namespace
{
    const juce::Rectangle<int> screenArea  { 24, 76, 852, 196 };
    const juce::Rectangle<int> mainArea    { 888, 76, 328, 196 };
    const juce::Rectangle<int> rhythmArea  { 24, 284, 600, 236 };
    const juce::Rectangle<int> melodyArea  { 636, 284, 580, 236 };
    const juce::Rectangle<int> stepsArea   { 24, 532, 600, 116 };
    const juce::Rectangle<int> dynArea     { 24, 660, 600, 116 };
    const juce::Rectangle<int> bendArea    { 636, 532, 280, 244 };
    const juce::Rectangle<int> modArea     { 928, 532, 288, 244 };
}

RythmFacade::RythmFacade (RythmAudioProcessor& p)
    : audioProcessor (p),
      stream (p, lookAndFeel),
      stepGrid (p, lookAndFeel),
      scaleKeys (p, lookAndFeel),
      dragZone (p, lookAndFeel),
      presetBar (p.presets, [this] (float size, bool bold, float tracking) { return lookAndFeel.font (size, bold, tracking); })
{
    setLookAndFeel (&lookAndFeel);
    setSize (width, height);

    addAndMakeVisible (stream);
    stream.setBounds (screenArea);

    // PLAY
    {
        const auto a = mainArea;
        choiceSwitch (modeSwitch, P::modeId, { "RHYTHM", "MELODY" }, { a.getX() + 16, a.getY() + 34, a.getWidth() - 32, 32 });
        for (auto* b : modeSwitch.buttons)
            b->getProperties().set ("textSize", 12.0f);
        toggle (loopButton, P::loopId, { a.getX() + 16, a.getY() + 78, 96, 50 });
        loopButton.getProperties().set ("colour", (juce::int64) L::lilac().getARGB());
        loopButton.getProperties().set ("textSize", 14.0f);
        choiceMenu (loopLengthMenu, P::loopLengthId, { a.getX() + 16, a.getY() + 132, 96, 24 });
        addAndMakeVisible (dragZone);
        dragZone.setBounds (a.getX() + 120, a.getY() + 78, 88, 78);

        for (auto* b : { &synthLoadButton, &synthOpenButton, &synthRemoveButton })
        {
            b->setWantsKeyboardFocus (false);
            b->setMouseCursor (juce::MouseCursor::PointingHandCursor);
            addAndMakeVisible (*b);
        }
        synthLoadButton.setComponentID ("synthLoad");
        synthOpenButton.setComponentID ("synthOpen");
        synthRemoveButton.setComponentID ("synthRemove");
        synthLoadButton.setBounds (a.getX() + 216, a.getY() + 96, 96, 28);
        synthOpenButton.setBounds (a.getX() + 216, a.getY() + 128, 46, 28);
        synthRemoveButton.setBounds (a.getX() + 266, a.getY() + 128, 46, 28);
        synthOpenButton.setButtonText ("EDIT");
        synthOpenButton.getProperties().set ("textSize", 8.5f);
        synthRemoveButton.getProperties().set ("textSize", 8.0f);
        synthLoadButton.onClick = [this] { showLoadMenu(); };
        synthOpenButton.onClick = [this] { if (onOpenSynth) onOpenSynth(); };
        synthRemoveButton.onClick = [this] { audioProcessor.unloadPlugin(); };
    }

    // RHYTHM
    {
        const auto a = rhythmArea;
        choiceSwitch (rateSwitch, P::rateId, { "1/4", "1/8", "1/16", "1/32", "1/64" }, { a.getX() + 16, a.getY() + 36, 272, 28 });
        choiceSwitch (feelSwitch, P::feelId, { "STR", "TRIP", "DOT" }, { a.getX() + 300, a.getY() + 36, 150, 28 });
        choiceSwitch (sourceSwitch, P::sourceId, { "DENSITY", "STEPS" }, { a.getX() + 462, a.getY() + 36, 122, 28 });
        const int y = a.getY() + 84, w = 70, h = 136;
        knob (densityKnob, P::densityId, { a.getX() + 16 + 0 * 71, y, w, h }, "DENSITY");
        knob (fillKnob, P::fillId, { a.getX() + 16 + 1 * 71, y, w, h }, "FILL");
        knob (shuffleKnob, P::shuffleId, { a.getX() + 16 + 2 * 71, y, w, h }, "SHUFFLE");
        knob (humanKnob, P::humanId, { a.getX() + 16 + 3 * 71, y, w, h }, "HUMAN");
        knob (glitchKnob, P::glitchId, { a.getX() + 16 + 4 * 71, y, w, h }, "GLITCH");
        knob (groove2Knob, P::groove2Id, { a.getX() + 16 + 5 * 71, y, w, h }, "LONG x2");
        knob (groove4Knob, P::groove4Id, { a.getX() + 16 + 6 * 71, y, w, h }, "LONG x4");
        knob (splitKnob, P::splitId, { a.getX() + 16 + 7 * 71, y, w, h }, "SPLIT /2");
        for (auto* k : { &groove2Knob, &groove4Knob, &splitKnob })
            k->getProperties().set ("colour", (juce::int64) L::steel().getARGB());
    }

    // MELODY
    {
        const auto a = melodyArea;
        choiceMenu (scaleMenu, P::scaleId, { a.getX() + 16, a.getY() + 36, 172, 28 });
        addAndMakeVisible (scaleKeys);
        scaleKeys.setBounds (a.getX() + 196, a.getY() + 36, 264, 28);
        choiceMenu (stackMenu, P::stackTypeId, { a.getX() + 470, a.getY() + 36, 94, 28 }, "STACK ");
        const int y = a.getY() + 84, w = 74, h = 136;
        knob (chanceKnob, P::chanceId, { a.getX() + 16 + 0 * 78, y, w, h }, "CHANCE");
        knob (rangeKnob, P::rangeId, { a.getX() + 16 + 1 * 78, y, w, h }, "RANGE");
        knob (directionKnob, P::directionId, { a.getX() + 16 + 2 * 78, y, w, h }, "DIRECTION", true);
        knob (walkKnob, P::walkId, { a.getX() + 16 + 3 * 78, y, w, h }, "WALK");
        knob (octaveKnob, P::octaveId, { a.getX() + 16 + 4 * 78, y, w, h }, "OCTAVE");
        knob (slideKnob, P::slideId, { a.getX() + 16 + 5 * 78, y, w, h }, "SLIDE");
        knob (stackKnob, P::stackId, { a.getX() + 16 + 6 * 78, y, w, h }, "STACK");
        slideKnob.getProperties().set ("colour", (juce::int64) L::lilac().getARGB());
        stackKnob.getProperties().set ("colour", (juce::int64) L::steel().getARGB());
    }

    // STEPS
    {
        const auto a = stepsArea;
        knob (stepsKnob, P::numStepsId, { a.getX() + 12, a.getY() + 24, 62, 86 }, "LENGTH");
        addAndMakeVisible (stepGrid);
        stepGrid.setBounds (a.getX() + 84, a.getY() + 32, 452, 72);
        for (auto* b : { &randomStepsButton, &clearStepsButton })
        {
            b->setWantsKeyboardFocus (false);
            addAndMakeVisible (*b);
        }
        randomStepsButton.setComponentID ("randomSteps");
        clearStepsButton.setComponentID ("clearSteps");
        randomStepsButton.setBounds (a.getX() + 546, a.getY() + 32, 42, 33);
        clearStepsButton.setBounds (a.getX() + 546, a.getY() + 71, 42, 33);
        randomStepsButton.onClick = [this] { audioProcessor.randomiseSteps(); };
        clearStepsButton.onClick = [this]
        {
            for (int i = 0; i < rythm::maxSteps; ++i)
                if (auto* prm = audioProcessor.apvts.getParameter (P::stepId (i)))
                {
                    prm->beginChangeGesture();
                    prm->setValueNotifyingHost (0.0f);
                    prm->endChangeGesture();
                }
        };
    }

    // DYNAMICS
    {
        const auto a = dynArea;
        const int y = a.getY() + 24, w = 96, h = 88;
        knob (gateKnob, P::gateId, { a.getX() + 18 + 0 * 114, y, w, h }, "GATE");
        knob (gateRandomKnob, P::gateRandomId, { a.getX() + 18 + 1 * 114, y, w, h }, "GATE RND");
        knob (velocityKnob, P::velocityId, { a.getX() + 18 + 2 * 114, y, w, h }, "VELOCITY");
        knob (velocityRandomKnob, P::velocityRandomId, { a.getX() + 18 + 3 * 114, y, w, h }, "VEL RND");
        knob (accentKnob, P::accentId, { a.getX() + 18 + 4 * 114, y, w, h }, "ACCENT");
        accentKnob.getProperties().set ("colour", (juce::int64) L::lilac().getARGB());
    }

    // BEND
    {
        const auto a = bendArea;
        choiceSwitch (bendShapeSwitch, P::bendShapeId, { "DIVE", "SCOOP", "WOBBLE", "RND" }, { a.getX() + 16, a.getY() + 36, a.getWidth() - 32, 28 });
        const int y = a.getY() + 78, w = 80, h = 120;
        knob (bendKnob, P::bendId, { a.getX() + 16, y, w, h }, "CHANCE");
        knob (bendDepthKnob, P::bendDepthId, { a.getX() + 100, y, w, h }, "DEPTH", true);
        knob (bendCyclesKnob, P::bendCyclesId, { a.getX() + 184, y, w, h }, "WOBBLE");
    }

    // MODULATION
    {
        const auto a = modArea;
        const char* what[] = { "Min", "Max", "Chance", "Smooth" };
        for (int m = 0; m < rythm::numMods; ++m)
        {
            const int y = a.getY() + 48 + m * 48;
            auto& b = modDestButtons[(size_t) m];
            b.setComponentID ("modDest" + juce::String (m + 1));
            b.setWantsKeyboardFocus (false);
            b.setBounds (a.getX() + 12, y + 8, 98, 28);
            b.getProperties().set ("textSize", 9.0f);
            addAndMakeVisible (b);
            b.onClick = [this, m] { showModMenu (m); };

            auto* prm = audioProcessor.apvts.getParameter (P::modId (m, "Dest"));
            auto* button = &b;
            modDestAttachments[(size_t) m] = std::make_unique<juce::ParameterAttachment> (*prm, [this, button] (float v)
            {
                const int dest = juce::roundToInt (v);
                button->setButtonText (audioProcessor.modDestName (dest));
                button->setToggleState (dest > 0, juce::dontSendNotification);
                updateEnablement();
            });
            modDestAttachments[(size_t) m]->sendInitialUpdate();

            for (int k = 0; k < 4; ++k)
            {
                knob (modKnobs[(size_t) m][(size_t) k], P::modId (m, what[k]), { a.getX() + 114 + k * 42, y + 2, 40, 44 }, what[k], false, true);
                modKnobs[(size_t) m][(size_t) k].getProperties().set ("colour", (juce::int64) (m % 2 == 0 ? L::accent() : L::lilac()).getARGB());
            }
        }
    }

    addAndMakeVisible (presetBar);
    presetBar.setBounds (width - 110 - phragma::PresetBar::idealWidth, 24, phragma::PresetBar::idealWidth, 28);
    presetBar.attachSavePanelTo (*this);

    audioProcessor.addChangeListener (this);
    updateSynthSection();
    updateEnablement();
    startTimerHz (10);
}

RythmFacade::~RythmFacade()
{
    stopTimer();
    audioProcessor.removeChangeListener (this);
    for (auto& a : modDestAttachments)
        a.reset();
    setLookAndFeel (nullptr);
}

juce::Component* RythmFacade::findControl (const juce::String& componentId)
{
    for (auto* c : getChildren())
        if (c->getComponentID() == componentId)
            return c;
    return nullptr;
}

void RythmFacade::knob (juce::Slider& s, const juce::String& id, juce::Rectangle<int> area, const juce::String& label, bool bipolar, bool mini)
{
    s.setName (label);
    s.setComponentID (id);
    s.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    s.setTextBoxStyle (juce::Slider::NoTextBox, true, 0, 0);
    s.setRotaryParameters (juce::MathConstants<float>::pi * 1.2f, juce::MathConstants<float>::pi * 2.8f, true);
    s.setMouseCursor (juce::MouseCursor::UpDownResizeCursor);
    s.setMouseDragSensitivity (mini ? 160 : 220);
    s.getProperties().set ("bipolar", bipolar);
    s.getProperties().set ("mini", mini);
    s.setBounds (area);
    addAndMakeVisible (s);
    sliderAttachments.push_back (std::make_unique<APVTS::SliderAttachment> (audioProcessor.apvts, id, s));
    s.setDoubleClickReturnValue (true, audioProcessor.apvts.getParameter (id)->convertFrom0to1 (audioProcessor.apvts.getParameter (id)->getDefaultValue()));
}

void RythmFacade::toggle (juce::TextButton& b, const juce::String& id, juce::Rectangle<int> area)
{
    b.setComponentID (id);
    b.setClickingTogglesState (true);
    b.setBounds (area);
    b.setWantsKeyboardFocus (false);
    addAndMakeVisible (b);
    buttonAttachments.push_back (std::make_unique<APVTS::ButtonAttachment> (audioProcessor.apvts, id, b));
}

void RythmFacade::choiceSwitch (ChoiceSwitch& sw, const juce::String& id, const juce::StringArray& texts, juce::Rectangle<int> area)
{
    sw.attachment.reset();
    sw.buttons.clear();

    const int n = texts.size();
    const int gap = 4;
    const int w = (area.getWidth() - gap * (n - 1)) / n;
    for (int i = 0; i < n; ++i)
    {
        auto* b = sw.buttons.add (new juce::TextButton (texts[i]));
        b->setComponentID (id + "_" + juce::String (i));
        b->setClickingTogglesState (false);
        b->setWantsKeyboardFocus (false);
        b->setBounds (area.getX() + i * (w + gap), area.getY(), i == n - 1 ? area.getRight() - (area.getX() + i * (w + gap)) : w, area.getHeight());
        addAndMakeVisible (b);
    }

    auto* prm = audioProcessor.apvts.getParameter (id);
    auto* swPtr = &sw;
    sw.attachment = std::make_unique<juce::ParameterAttachment> (*prm, [this, swPtr] (float value)
    {
        const int index = juce::roundToInt (value);
        for (int i = 0; i < swPtr->buttons.size(); ++i)
            swPtr->buttons[i]->setToggleState (i == index, juce::dontSendNotification);
        updateEnablement();
        repaint();
    });

    for (int i = 0; i < n; ++i)
        sw.buttons[i]->onClick = [swPtr, i] { swPtr->attachment->setValueAsCompleteGesture ((float) i); };

    sw.attachment->sendInitialUpdate();
}

void RythmFacade::choiceMenu (ChoiceMenu& cm, const juce::String& id, juce::Rectangle<int> area, const juce::String& prefix)
{
    auto* prm = dynamic_cast<juce::AudioParameterChoice*> (audioProcessor.apvts.getParameter (id));
    cm.prefix = prefix;
    cm.button.setComponentID (id);
    cm.button.setBounds (area);
    cm.button.setWantsKeyboardFocus (false);
    addAndMakeVisible (cm.button);

    auto* cmPtr = &cm;
    cm.attachment = std::make_unique<juce::ParameterAttachment> (*prm, [this, cmPtr, prm] (float value)
    {
        cmPtr->button.setButtonText (cmPtr->prefix + prm->choices[juce::jlimit (0, prm->choices.size() - 1, juce::roundToInt (value))]
                                     + juce::String (juce::CharPointer_UTF8 (" \xe2\x96\xbe")));
        updateEnablement();
        repaint();
    });
    cm.attachment->sendInitialUpdate();

    cm.button.onClick = [this, cmPtr, prm]
    {
        juce::PopupMenu menu;
        menu.setLookAndFeel (&lookAndFeel);
        if (prm->paramID == P::scaleId)
        {
            // Gammes groupées
            auto add = [&] (int i) { menu.addItem (i + 1, prm->choices[i], true, i == prm->getIndex()); };
            menu.addSectionHeader ("MAJOR / MINOR");
            for (int i : { rythm::major, rythm::minor, rythm::harmonicMinor, rythm::melodicMinor }) add (i);
            menu.addSectionHeader ("MODES");
            for (int i : { rythm::dorian, rythm::phrygian, rythm::phrygianDominant, rythm::lydian, rythm::mixolydian, rythm::locrian }) add (i);
            menu.addSectionHeader ("EXOTIC");
            for (int i : { rythm::hungarianMinor, rythm::doubleHarmonic, rythm::hirajoshi, rythm::inSen, rythm::wholeTone, rythm::diminished }) add (i);
            menu.addSectionHeader ("PENTATONIC");
            for (int i : { rythm::minorPentatonic, rythm::majorPentatonic, rythm::blues }) add (i);
            menu.addSectionHeader ("SPECIAL");
            for (int i : { rythm::acid, rythm::chromatic, rythm::heldNotes, rythm::custom }) add (i);
        }
        else
        {
            for (int i = 0; i < prm->choices.size(); ++i)
                menu.addItem (i + 1, prm->choices[i], true, i == prm->getIndex());
        }
        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&cmPtr->button),
                            [cmPtr] (int r) { if (r > 0) cmPtr->attachment->setValueAsCompleteGesture ((float) (r - 1)); });
    };
}

void RythmFacade::showModMenu (int slot)
{
    auto* prm = audioProcessor.apvts.getParameter (P::modId (slot, "Dest"));
    const int current = juce::roundToInt (prm->convertFrom0to1 (prm->getValue()));

    juce::PopupMenu menu;
    menu.setLookAndFeel (&lookAndFeel);
    menu.addItem (1000, "OFF", true, current == 0);

    menu.addSectionHeader ("MIDI CC (MIDI OUT + SYNTH)");
    for (int cc : { 74, 71, 1, 11, 7, 10 })
        menu.addItem (1000 + cc, audioProcessor.modDestName (cc), true, current == cc);
    juce::PopupMenu all;
    for (int group = 0; group < 4; ++group)
    {
        juce::PopupMenu sub;
        for (int cc = group * 32 + 1; cc <= juce::jmin (127, group * 32 + 32); ++cc)
            sub.addItem (1000 + cc, audioProcessor.modDestName (cc), true, current == cc);
        all.addSubMenu ("CC " + juce::String (group * 32 + 1) + " - " + juce::String (juce::jmin (127, group * 32 + 32)), sub);
    }
    menu.addSubMenu ("ALL CC", all);

    const auto names = audioProcessor.getRelayedNames();
    menu.addSectionHeader (audioProcessor.getInner() != nullptr ? "SYNTH: " + audioProcessor.getInnerName().toUpperCase() : "SYNTH (LOAD ONE FIRST)");
    if (names.isEmpty())
    {
        menu.addItem (-1, "NO SYNTH LOADED", false);
    }
    else
    {
        juce::PopupMenu params;
        for (int i = 0; i < names.size(); ++i)
        {
            if (i > 0 && i % 32 == 0)
            {
                menu.addSubMenu ("PARAMETERS " + juce::String (i - 31) + " - " + juce::String (i), params);
                params = {};
            }
            params.addItem (1000 + 128 + i, names[i], true, current == 128 + i);
        }
        const int first = (names.size() - 1) / 32 * 32 + 1;
        menu.addSubMenu ("PARAMETERS " + juce::String (first) + " - " + juce::String (names.size()), params);
    }

    auto* attachment = modDestAttachments[(size_t) slot].get();
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&modDestButtons[(size_t) slot]),
                        [attachment, safe = juce::Component::SafePointer<RythmFacade> (this)] (int r)
                        {
                            if (safe != nullptr && r >= 1000)
                                attachment->setValueAsCompleteGesture ((float) (r - 1000));
                        });
}

void RythmFacade::showLoadMenu()
{
    const auto files = RythmAudioProcessor::findVst3Files();
    juce::PopupMenu m;
    m.setLookAndFeel (&lookAndFeel);
    m.addSectionHeader ("VST3 PLUGINS");
    for (int i = 0; i < files.size(); ++i)
        m.addItem (i + 1, files[i].getFileNameWithoutExtension(), true, files[i].getFileNameWithoutExtension() == audioProcessor.getInnerName());
    if (files.isEmpty())
        m.addItem (-1, "(NO VST3 FOUND)", false);
    m.addSeparator();
    m.addItem (100000, "BROWSE...");

    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (synthLoadButton),
                     [safe = juce::Component::SafePointer<RythmFacade> (this), files] (int r)
                     {
                         if (safe == nullptr || r == 0)
                             return;

                         auto load = [safe] (const juce::File& f)
                         {
                             if (safe == nullptr)
                                 return;
                             juce::String error;
                             if (! safe->audioProcessor.loadFile (f, error))
                                 juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Rythm", error);
                             else if (safe->onOpenSynth)
                                 safe->onOpenSynth();
                         };

                         if (r == 100000)
                         {
                             safe->chooser = std::make_unique<juce::FileChooser> ("Choose a VST3 instrument", juce::File(), "*.vst3");
                             safe->chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles
                                                             | juce::FileBrowserComponent::canSelectDirectories,
                                                         [load] (const juce::FileChooser& fc)
                                                         {
                                                             if (fc.getResult() != juce::File())
                                                                 load (fc.getResult());
                                                         });
                             return;
                         }

                         if (r > 0 && r <= files.size())
                             load (files[r - 1]);
                     });
}

void RythmFacade::updateSynthSection()
{
    const bool loaded = audioProcessor.getInner() != nullptr;
    synthName = loaded ? audioProcessor.getInnerName().toUpperCase() : audioProcessor.getLoadError();
    synthLoadButton.setButtonText (loaded ? "CHANGE" : "LOAD SYNTH");
    synthOpenButton.setEnabled (loaded);
    synthRemoveButton.setEnabled (loaded);
    // Les noms des destinations SYNTH changent avec l'instrument
    for (int m = 0; m < rythm::numMods; ++m)
        if (auto* prm = audioProcessor.apvts.getParameter (P::modId (m, "Dest")))
            modDestButtons[(size_t) m].setButtonText (audioProcessor.modDestName (juce::roundToInt (prm->convertFrom0to1 (prm->getValue()))));
    repaint (mainArea);
}

void RythmFacade::updateEnablement()
{
    const auto s = audioProcessor.readSettings();
    const bool melody = s.mode == rythm::melodyMode;
    for (auto* k : { &chanceKnob, &rangeKnob, &directionKnob, &walkKnob, &octaveKnob, &slideKnob })
        k->getProperties().set ("dim", melody ? 1.0f : 0.45f);
    scaleKeys.setAlpha (melody || s.stack > 0.0f ? 1.0f : 0.55f);

    const bool stepsMode = s.source == rythm::stepsSource;
    stepGrid.setAlpha (stepsMode ? 1.0f : 0.5f);
    fillKnob.getProperties().set ("dim", stepsMode ? 1.0f : 0.45f);
    stepsKnob.getProperties().set ("dim", stepsMode ? 1.0f : 0.45f);

    for (int m = 0; m < rythm::numMods; ++m)
        for (auto& k : modKnobs[(size_t) m])
            k.getProperties().set ("dim", modDestButtons[(size_t) m].getToggleState() ? 1.0f : 0.4f);

    acidShown = melody && s.scale == rythm::acid;
    repaint();
}

void RythmFacade::changeListenerCallback (juce::ChangeBroadcaster*)
{
    updateSynthSection();
}

void RythmFacade::timerCallback()
{
    // Les réglages changés par l'hôte (automation) : on remet à jour ce qui en dépend
    const auto s = audioProcessor.readSettings();
    int key = s.mode + 2 * s.source + 4 * s.scale + (s.stack > 0.0f ? 1 << 10 : 0);
    for (int m = 0; m < rythm::numMods; ++m)
        key += (s.mods[(size_t) m].active ? 1 : 0) << (11 + m);
    if (key != enablementKey)
    {
        enablementKey = key;
        updateEnablement();
    }
    const auto expected = audioProcessor.getInner() != nullptr ? audioProcessor.getInnerName().toUpperCase() : audioProcessor.getLoadError();
    if (expected != synthName)
        updateSynthSection();
}

void RythmFacade::paint (juce::Graphics& g)
{
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff111c22), 0.0f, 0.0f, L::ground(), 0.0f, (float) height, false));
    g.fillAll();

    // En-tête
    g.setColour (L::lilac());
    g.setFont (lookAndFeel.font (30.0f, true, 0.2f));
    g.drawText ("RYTHM", juce::Rectangle<float> (26.0f, 14.0f, 150.0f, 40.0f), juce::Justification::centredLeft, false);
    g.setColour (L::dim());
    g.setFont (lookAndFeel.font (9.0f, true, 0.22f));
    g.drawText ("RHYTHM & MELODY GENERATOR", juce::Rectangle<float> (176.0f, 20.0f, 220.0f, 14.0f), juce::Justification::centredLeft, false);
    g.drawText ("BY PHRAGMA", juce::Rectangle<float> (176.0f, 36.0f, 220.0f, 14.0f), juce::Justification::centredLeft, false);
    g.setColour (L::dim().withAlpha (0.8f));
    g.drawText ("V" RYTHM_VERSION, juce::Rectangle<float> ((float) width - 100.0f, 24.0f, 76.0f, 28.0f), juce::Justification::centredRight, false);

    g.setGradientFill (juce::ColourGradient (L::accent(), 24.0f, 0.0f, L::mauve(), (float) width - 24.0f, 0.0f, false));
    g.fillRect (24.0f, 64.0f, (float) width - 48.0f, 1.5f);

    const auto titleFont = lookAndFeel.font (10.5f, true, 0.28f);
    L::drawPanel (g, mainArea.toFloat(), "PLAY", titleFont);
    L::drawPanel (g, rhythmArea.toFloat(), "RHYTHM", titleFont);
    L::drawPanel (g, melodyArea.toFloat(), "MELODY", titleFont);
    L::drawPanel (g, stepsArea.toFloat(), "STEPS", titleFont);
    L::drawPanel (g, dynArea.toFloat(), "DYNAMICS", titleFont);
    L::drawPanel (g, bendArea.toFloat(), "PITCH BEND", titleFont);
    L::drawPanel (g, modArea.toFloat(), "MODULATION", titleFont);

    const auto s = audioProcessor.readSettings();
    const auto small = lookAndFeel.font (8.5f, true, 0.18f);
    g.setFont (small);

    // PLAY : instrument
    {
        const auto a = mainArea.toFloat();
        g.setColour (L::dim());
        g.drawText ("SYNTH", juce::Rectangle<float> (a.getX() + 216.0f, a.getY() + 78.0f, 96.0f, 14.0f), juce::Justification::centredLeft, false);
        juce::String line = synthName.isNotEmpty() ? synthName : "NONE" + dot() + "MIDI OUT ONLY";
        if (audioProcessor.getInner() != nullptr && audioProcessor.getInnerLatency() > 0)
            line << dot() << "LATENCY " << audioProcessor.getInnerLatency() << " SMP";
        g.setColour (audioProcessor.getInner() != nullptr ? L::accent() : L::dim());
        g.setFont (lookAndFeel.font (9.5f, true, 0.14f));
        g.drawText (line, juce::Rectangle<float> (a.getX() + 16.0f, a.getBottom() - 30.0f, a.getWidth() - 32.0f, 18.0f),
                    juce::Justification::centredLeft, true);
        g.setColour (L::dim().withAlpha (0.7f));
        g.setFont (small);
        g.drawText ("MIDI OUT", juce::Rectangle<float> (a.getRight() - 120.0f, a.getY() + 8.0f, 106.0f, 16.0f), juce::Justification::centredRight, false);
    }

    // RHYTHM : groove
    {
        const auto a = rhythmArea.toFloat();
        g.setColour (L::steel().withAlpha (0.8f));
        g.setFont (small);
        g.drawText ("GROOVE", juce::Rectangle<float> (a.getX() + 16.0f + 5 * 71, a.getY() + 70.0f, 3 * 71 - 1, 12.0f), juce::Justification::centred, false);
        g.fillRect (a.getX() + 22.0f + 5 * 71, a.getY() + 76.0f, 30.0f, 1.0f);
        g.fillRect (a.getX() + 16.0f + 8 * 71 - 36.0f, a.getY() + 76.0f, 30.0f, 1.0f);
    }

    // MELODY : rappel des règles ACID, mode
    {
        const auto a = melodyArea.toFloat();
        juce::String note;
        if (s.mode == rythm::rhythmMode)
            note = "MELODY MODE OFF" + dot() + "SCALE + STACK STILL USED FOR CHORDS";
        else if (s.scale == rythm::acid)
            note = "303 RULES: OCTAVE JUMPS, SLIDES, ACCENTS";
        else if (s.scale == rythm::heldNotes)
            note = "PLAYS THE NOTES OF THE HELD CHORD";
        if (note.isNotEmpty())
        {
            g.setColour (s.scale == rythm::acid && s.mode == rythm::melodyMode ? L::lilac() : L::dim());
            g.setFont (small);
            g.drawText (note, juce::Rectangle<float> (a.getX() + 100.0f, a.getY() + 8.0f, a.getWidth() - 114.0f, 16.0f),
                        juce::Justification::centredRight, true);
        }
    }

    // STEPS : rappel de la source
    if (s.source != rythm::stepsSource)
    {
        const auto a = stepsArea.toFloat();
        g.setColour (L::dim());
        g.setFont (small);
        g.drawText ("SOURCE = DENSITY" + dot() + "SWITCH TO STEPS TO USE THE GRID", juce::Rectangle<float> (a.getX() + 100.0f, a.getY() + 8.0f, a.getWidth() - 114.0f, 16.0f),
                    juce::Justification::centredRight, false);
    }

    // BEND : rappel
    {
        const auto a = bendArea.toFloat();
        g.setColour (L::dim().withAlpha (0.8f));
        g.setFont (small);
        g.drawText ("DEPTH 100 % = YOUR SYNTH'S BEND RANGE", juce::Rectangle<float> (a.getX() + 12.0f, a.getBottom() - 34.0f, a.getWidth() - 24.0f, 14.0f),
                    juce::Justification::centred, false);
        g.drawText ("WOBBLE = CYCLES PER NOTE", juce::Rectangle<float> (a.getX() + 12.0f, a.getBottom() - 20.0f, a.getWidth() - 24.0f, 14.0f),
                    juce::Justification::centred, false);
    }

    // MODULATION : en-têtes des colonnes
    {
        const auto a = modArea.toFloat();
        g.setColour (L::dim());
        g.setFont (lookAndFeel.font (8.0f, true, 0.04f));
        g.drawText ("DEST", juce::Rectangle<float> (a.getX() + 12.0f, a.getY() + 30.0f, 98.0f, 14.0f), juce::Justification::centred, false);
        const char* heads[] = { "MIN", "MAX", "CHANCE", "SMOOTH" };
        for (int k = 0; k < 4; ++k)
            g.drawText (heads[k], juce::Rectangle<float> (a.getX() + 114.0f + k * 42.0f - 2.0f, a.getY() + 30.0f, 44.0f, 14.0f), juce::Justification::centred, false);
    }
}

void RythmFacade::paintOverChildren (juce::Graphics& g)
{
    // LOOP allumé : un liseré qui respire
    if (audioProcessor.isLooping())
    {
        const auto r = loopButton.getBounds().toFloat().expanded (2.5f);
        g.setColour (L::lilac().withAlpha (0.5f));
        g.strokePath (L::chamfered (r, 6.0f), juce::PathStrokeType (1.5f));
    }
}

//==============================================================================
RythmAudioProcessorEditor::RythmAudioProcessorEditor (RythmAudioProcessor& p)
    : AudioProcessorEditor (&p), audioProcessor (p), facade (p)
{
    addAndMakeVisible (facade);
    for (auto* b : { &backButton, &changeButton, &removeButton })
    {
        b->setLookAndFeel (&barLook);
        b->setWantsKeyboardFocus (false);
        b->setMouseCursor (juce::MouseCursor::PointingHandCursor);
        addChildComponent (*b);
    }
    backButton.onClick = [this] { showSynth (false); };
    changeButton.onClick = [this]
    {
        showSynth (false);
        facade.showLoadMenu();
    };
    removeButton.onClick = [this]
    {
        showSynth (false);
        audioProcessor.unloadPlugin();
    };
    facade.onOpenSynth = [this] { showSynth (true); };

    const int savedWidth = (int) audioProcessor.apvts.state.getProperty ("editorWidth", juce::roundToInt (RythmFacade::width * RythmFacade::defaultScale));
    rythmWidth = juce::jlimit (juce::roundToInt (RythmFacade::width * 0.6), juce::roundToInt (RythmFacade::width * 1.5), savedWidth);

    facade.getPresetBar().getScale = [this] { return showingSynth ? (float) rythmWidth / (float) RythmFacade::width : getUiScale(); };
    facade.getPresetBar().setScale = [this] (float scale) { setUiScale (scale); };

    audioProcessor.setOpenEditor (this);
    applyRythmSizeLimits();
    setSize (rythmWidth, juce::roundToInt ((double) rythmWidth * RythmFacade::height / RythmFacade::width));
    sized = true;
    resized();
}

RythmAudioProcessorEditor::~RythmAudioProcessorEditor()
{
    dropInnerEditor();
    audioProcessor.setOpenEditor (nullptr);
    for (auto* b : { &backButton, &changeButton, &removeButton })
        b->setLookAndFeel (nullptr);
}

void RythmAudioProcessorEditor::applyRythmSizeLimits()
{
    setResizable (true, true);
    setResizeLimits (juce::roundToInt (RythmFacade::width * 0.6), juce::roundToInt (RythmFacade::height * 0.6),
                     juce::roundToInt (RythmFacade::width * 1.5), juce::roundToInt (RythmFacade::height * 1.5));
    getConstrainer()->setFixedAspectRatio ((double) RythmFacade::width / (double) RythmFacade::height);
}

void RythmAudioProcessorEditor::setUiScale (float scale)
{
    scale = juce::jlimit (0.6f, 1.5f, scale);
    if (showingSynth)
        showSynth (false);
    setSize (juce::roundToInt ((float) RythmFacade::width * scale), juce::roundToInt ((float) RythmFacade::height * scale));
}

void RythmAudioProcessorEditor::dropInnerEditor()
{
    if (innerEditor == nullptr)
        return;
    innerEditor->removeComponentListener (this);
    removeChildComponent (innerEditor.get());
    innerEditor.reset();
}

void RythmAudioProcessorEditor::innerChanged()
{
    if (showingSynth && audioProcessor.getInner() == nullptr)
        showSynth (false);
    else if (showingSynth)
        showSynth (true);
}

void RythmAudioProcessorEditor::showSynth (bool show)
{
    auto* inner = audioProcessor.getInner();
    if (show && inner == nullptr)
        show = false;

    if (show)
    {
        if (! showingSynth)
            rythmWidth = getWidth();
        if (innerEditor == nullptr || innerEditor->getAudioProcessor() != inner)
        {
            dropInnerEditor();
            juce::AudioProcessorEditor* e = inner->hasEditor() ? inner->createEditorAndMakeActive() : nullptr;
            if (e == nullptr)
                e = new juce::GenericAudioProcessorEditor (*inner);
            innerEditor.reset (e);
            addAndMakeVisible (*innerEditor);
            innerEditor->addComponentListener (this);
        }
        showingSynth = true;
        facade.setVisible (false);
        for (auto* b : { &backButton, &changeButton, &removeButton })
            b->setVisible (true);
        setResizable (false, false);
        layoutSynthView();
    }
    else
    {
        const bool wasShowing = showingSynth;
        const int width = rythmWidth;
        showingSynth = false;
        dropInnerEditor();
        for (auto* b : { &backButton, &changeButton, &removeButton })
            b->setVisible (false);
        facade.setVisible (true);
        sized = false;   // les limites peuvent redimensionner en passant : on garde la taille d'avant
        applyRythmSizeLimits();
        sized = true;
        rythmWidth = width;
        if (wasShowing)
            setSize (width, juce::roundToInt ((double) width * RythmFacade::height / RythmFacade::width));
        resized();
    }
    repaint();
}

void RythmAudioProcessorEditor::layoutSynthView()
{
    if (innerEditor == nullptr)
        return;
    const int w = juce::jmax (synthMinWidth, innerEditor->getWidth());
    const int h = synthBarHeight + innerEditor->getHeight();
    setResizeLimits (w, h, w, h);
    getConstrainer()->setFixedAspectRatio (0.0);
    if (w != getWidth() || h != getHeight())
        setSize (w, h);
    else
        resized();
}

void RythmAudioProcessorEditor::componentMovedOrResized (juce::Component& c, bool, bool wasResized)
{
    if (&c == innerEditor.get() && wasResized && showingSynth)
        layoutSynthView();
}

void RythmAudioProcessorEditor::paint (juce::Graphics& g)
{
    g.fillAll (L::ground());
    if (! showingSynth)
        return;

    const float w = (float) getWidth();
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff17252b), 0.0f, 0.0f, L::ground(), 0.0f, (float) synthBarHeight, false));
    g.fillRect (0.0f, 0.0f, w, (float) synthBarHeight);
    g.setColour (L::lilac());
    g.setFont (barLook.font (16.0f, true, 0.2f));
    g.drawText ("RYTHM", juce::Rectangle<float> (110.0f, 0.0f, 90.0f, (float) synthBarHeight), juce::Justification::centredLeft, false);
    g.setColour (L::accent());
    g.setFont (barLook.font (11.0f, true, 0.16f));
    juce::String line = audioProcessor.getInnerName().toUpperCase();
    if (audioProcessor.getInnerLatency() > 0)
        line << dot() << "LATENCY " << audioProcessor.getInnerLatency() << " SMP";
    g.drawText (line, juce::Rectangle<float> (200.0f, 0.0f, juce::jmax (40.0f, (float) changeButton.getX() - 212.0f), (float) synthBarHeight),
                juce::Justification::centredLeft, true);
    g.setGradientFill (juce::ColourGradient (L::accent(), 0.0f, 0.0f, L::mauve(), w, 0.0f, false));
    g.fillRect (0.0f, (float) synthBarHeight - 1.5f, w, 1.5f);
}

void RythmAudioProcessorEditor::resized()
{
    if (showingSynth)
    {
        const int w = getWidth();
        backButton.setBounds (12, 10, 88, 28);
        removeButton.setBounds (w - 12 - 80, 10, 80, 28);
        changeButton.setBounds (removeButton.getX() - 8 - 80, 10, 80, 28);
        if (innerEditor != nullptr)
            innerEditor->setTopLeftPosition (0, synthBarHeight);
        return;
    }

    facade.setTransform (juce::AffineTransform::scale (getUiScale()));
    facade.getPresetBar().refresh();
    if (sized)
    {
        rythmWidth = getWidth();
        audioProcessor.apvts.state.setProperty ("editorWidth", getWidth(), nullptr);
    }
}
