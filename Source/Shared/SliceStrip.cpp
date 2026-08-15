#include "SliceStrip.h"
#include "Look.h"
#include "SliceExporter.h"

namespace
{
    constexpr int chipWidth = 122;
    constexpr int chipGap = 4;
    constexpr int dragStartPixels = 6;

    /** A cut is what SliceExporter wrote under its own name; anything else in
        the folder arrived whole. Read off the file rather than remembered,
        because the folder is the truth here — a person may delete from it, and
        the row is rebuilt from what is left. */
    bool isCut (const juce::File& file)
    {
        return SliceExporter::isNamed (file, SliceExporter::cutBaseName);
    }
}

//==============================================================================
class SliceStrip::Chip final : public juce::Component,
                               public juce::SettableTooltipClient
{
public:
    Chip (const juce::File& f, SliceStrip& parent)
        : file (f), strip (parent)
    {
        setTooltip (file.getFullPathName());
    }

    void paint (juce::Graphics& g) override
    {
        const auto area = getLocalBounds().toFloat().reduced (1.0f);
        const auto isCurrent = file == strip.selected();
        const auto cut = isCut (file);

        const auto base = cut ? Look::chipCut : Look::chipSource;

        g.setColour (dragging ? base.brighter (Look::chipHeldLift) : base);
        g.fillRoundedRectangle (area, 4.0f);
        g.setColour (isCurrent ? Look::wave : Look::chipEdge);
        g.drawRoundedRectangle (area, 4.0f, isCurrent ? 2.0f : 1.0f);

        auto body = area.reduced (6.0f);
        auto header = body.removeFromTop (16.0f);
        const auto cross = crossArea().toFloat();
        header = header.withTrimmedRight (cross.getWidth());

        // The file's own name, which is the one thing about a chip that does
        // not move when a neighbour is deleted. A position would: the row would
        // renumber itself under the cursor and there would be no telling which
        // one had just gone.
        g.setColour (isCurrent ? Look::wave : Look::text);
        g.setFont (juce::FontOptions (13.0f));
        g.drawText (file.getFileNameWithoutExtension(), header,
                    juce::Justification::centredLeft, true);

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
    const auto files = SliceExporter::wavsInOrder (folder);

    // Rebuilding throws away every chip and its hover and press state, so it is
    // worth doing only when the row would actually come out different.
    if (files == shown)
        return;

    shown = files;
    chips.clear();

    for (const auto& file : files)
        row.addAndMakeVisible (chips.add (new Chip (file, *this)));

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
