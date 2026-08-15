#include "SliceStrip.h"
#include "Look.h"

namespace
{
    constexpr int chipWidth = 122;
    constexpr int chipGap = 4;
    constexpr int dragStartPixels = 6;
}

//==============================================================================
class SliceStrip::Chip final : public juce::Component,
                               public juce::SettableTooltipClient
{
public:
    Chip (const juce::File& f, int number, SliceStrip& parent)
        : file (f), index (number), strip (parent)
    {
        setTooltip (file.getFullPathName());
    }

    void paint (juce::Graphics& g) override
    {
        const auto area = getLocalBounds().toFloat().reduced (1.0f);
        const auto isCurrent = file == strip.selected();

        g.setColour (dragging ? Look::chipHeld : Look::chip);
        g.fillRoundedRectangle (area, 4.0f);
        g.setColour (isCurrent ? Look::wave : Look::chipEdge);
        g.drawRoundedRectangle (area, 4.0f, isCurrent ? 2.0f : 1.0f);

        auto body = area.reduced (6.0f);
        auto header = body.removeFromTop (14.0f);
        const auto cross = crossArea().toFloat();
        header = header.withTrimmedRight (cross.getWidth());

        g.setColour (isCurrent ? Look::wave : Look::text);
        g.setFont (juce::FontOptions (12.0f));
        g.drawText (juce::String (index), header, juce::Justification::centredLeft, false);

        g.setColour (Look::dim);
        g.setFont (juce::FontOptions (10.0f));
        g.drawFittedText (file.getFileNameWithoutExtension(), body.toNearestInt(),
                          juce::Justification::topLeft, 2, 0.8f);

        g.setColour (overCross ? Look::error : Look::dim);
        const auto x = cross.reduced (5.0f);
        g.drawLine (x.getX(), x.getY(), x.getRight(), x.getBottom(), 1.4f);
        g.drawLine (x.getX(), x.getBottom(), x.getRight(), x.getY(), 1.4f);
    }

    void mouseMove (const juce::MouseEvent& e) override
    {
        const auto on = crossArea().contains (e.getPosition());

        if (on != overCross)
        {
            overCross = on;
            setMouseCursor (on ? juce::MouseCursor::NormalCursor
                               : juce::MouseCursor::PointingHandCursor);
            repaint();
        }
    }

    void mouseExit (const juce::MouseEvent&) override
    {
        overCross = false;
        repaint();
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        pressedCross = crossArea().contains (e.getPosition());
    }

    void mouseUp (const juce::MouseEvent& e) override
    {
        // Acting on release rather than on press, so a press that slides off
        // is a cancel like it is on every other button.
        if (! contains (e.getPosition()))
        {
            pressedCross = false;
            return;
        }

        if (pressedCross && crossArea().contains (e.getPosition()))
        {
            pressedCross = false;

            // The trash rather than deleteFile: a take that took a minute to
            // find should not be one misclick away from being gone.
            file.moveToTrash();
            strip.refresh();
            return;
        }

        pressedCross = false;

        if (! dragging && strip.onSelect != nullptr)
            strip.onSelect (file);
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        // A plain click has to stay a plain click, or pressing a chip drops
        // copies into whatever happens to be under the pointer.
        if (dragging || pressedCross || e.getDistanceFromDragStart() <= dragStartPixels)
            return;

        dragging = true;
        repaint();

        juce::DragAndDropContainer::performExternalDragDropOfFiles (
            { file.getFullPathName() }, false, this,
            [safe = juce::Component::SafePointer<Chip> (this)]
            {
                if (safe != nullptr)
                {
                    safe->dragging = false;
                    safe->repaint();
                }
            });
    }

private:
    juce::Rectangle<int> crossArea() const
    {
        return getLocalBounds().removeFromTop (20).removeFromRight (20);
    }

    juce::File file;
    int index = 0;
    bool dragging = false;
    bool overCross = false;
    bool pressedCross = false;
    SliceStrip& strip;
};

//==============================================================================
SliceStrip::SliceStrip()
{
    viewport.setViewedComponent (&row, false);
    viewport.setScrollBarsShown (false, true);
    addAndMakeVisible (viewport);
}

SliceStrip::~SliceStrip() = default;

void SliceStrip::setFolder (const juce::File& dir)
{
    folder = dir;
    shown.clear();      // a different folder always redraws
    refresh();
}

void SliceStrip::refresh()
{
    // Deferred because a chip asks for this from inside its own mouse handler
    // and rebuilding destroys that chip. Coalesced by AsyncUpdater rather than
    // posted with callAsync, because callers ask far more often than the folder
    // changes and every posted copy would list the directory again.
    triggerAsyncUpdate();
}

void SliceStrip::handleAsyncUpdate()
{
    rebuild();
}

void SliceStrip::setSelected (const juce::File& file)
{
    if (current != file)
    {
        current = file;
        repaint();

        for (auto* c : chips)
            c->repaint();
    }
}

void SliceStrip::rebuild()
{
    juce::Array<juce::File> files;

    if (folder.isDirectory())
    {
        files = folder.findChildFiles (juce::File::findFiles, false, "*.wav");
        files.sort();
    }

    // Rebuilding throws away every chip and its hover and press state, so it is
    // worth doing only when the row would actually come out different.
    if (files == shown)
        return;

    shown = files;
    chips.clear();

    for (int i = 0; i < files.size(); ++i)
        row.addAndMakeVisible (chips.add (new Chip (files[i], i + 1, *this)));

    resized();
    repaint();
}

void SliceStrip::paint (juce::Graphics& g)
{
    if (chips.isEmpty())
    {
        g.setColour (Look::dim);
        g.setFont (juce::FontOptions (12.0f));
        g.drawText ("Nothing here yet", getLocalBounds(), juce::Justification::centred);
    }
}

void SliceStrip::resized()
{
    viewport.setBounds (getLocalBounds());

    row.setSize (juce::jmax (viewport.getWidth(), chips.size() * (chipWidth + chipGap) + chipGap),
                 juce::jmax (10, viewport.getHeight() - 12));

    for (int i = 0; i < chips.size(); ++i)
        chips[i]->setBounds (chipGap + i * (chipWidth + chipGap), 2, chipWidth, row.getHeight() - 4);
}
