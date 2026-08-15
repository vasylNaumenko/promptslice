#include "WaveformView.h"
#include "Look.h"
#include "SliceProcessor.h"

namespace
{
    constexpr int rulerHeight = 22;
    constexpr float handleHeight = 18.0f;
    constexpr float handleMinWidth = 30.0f;
    constexpr float markerGrabPixels = 6.0f;
    constexpr float newRegionPixels = 4.0f;
    constexpr int dragOutPixels = 6;
    constexpr double minVisibleSeconds = 0.02;

    /** A step that gives readable ruler labels at the current zoom. */
    double rulerStepFor (double visibleSeconds, double pixels)
    {
        const double wanted = visibleSeconds / juce::jmax (1.0, pixels / 90.0);

        for (double step : { 0.01, 0.025, 0.05, 0.1, 0.25, 0.5, 1.0, 2.0, 5.0,
                             10.0, 15.0, 30.0, 60.0, 120.0, 300.0, 600.0 })
            if (step >= wanted)
                return step;

        return 900.0;
    }
}

WaveformView::WaveformView (SliceProcessor& p) : proc (p)
{
    proc.thumbnail().addChangeListener (this);
}

WaveformView::~WaveformView()
{
    proc.thumbnail().removeChangeListener (this);
}

void WaveformView::changeListenerCallback (juce::ChangeBroadcaster*)
{
    // The thumbnail fills in as it reads the file, so the picture is stale
    // until it stops talking.
    peaksDirty = true;
    repaint();
}

void WaveformView::setEmptyMessage (const juce::String& text)
{
    emptyMessage = text;
    repaint();
}

void WaveformView::zoomToFit()
{
    viewStart = 0.0;
    viewLength = 0.0;
    peaksDirty = true;
    repaint();
}

juce::Rectangle<int> WaveformView::rulerArea() const
{
    return getLocalBounds().removeFromTop (rulerHeight);
}

juce::Rectangle<int> WaveformView::waveArea() const
{
    return getLocalBounds().withTrimmedTop (rulerHeight);
}

void WaveformView::clampView()
{
    const auto total = proc.lengthSeconds();

    if (total <= 0.0)
    {
        viewStart = 0.0;
        viewLength = 0.0;
        return;
    }

    if (viewLength <= 0.0 || viewLength > total)
        viewLength = total;

    viewLength = juce::jmax (minVisibleSeconds, viewLength);
    viewStart = juce::jlimit (0.0, juce::jmax (0.0, total - viewLength), viewStart);
}

double WaveformView::xToTime (float x) const
{
    const auto area = waveArea();

    if (area.getWidth() <= 0)
        return 0.0;

    const auto length = currentLength();
    const auto fraction = ((double) x - (double) area.getX()) / (double) area.getWidth();
    return viewStart + fraction * length;
}

float WaveformView::timeToX (double t) const
{
    const auto area = waveArea();
    const auto length = currentLength();

    if (length <= 0.0)
        return (float) area.getX();

    const auto fraction = (t - viewStart) / length;
    return (float) area.getX() + (float) (fraction * area.getWidth());
}

int WaveformView::markerNear (float x) const
{
    const auto& marks = proc.markers();
    int best = -1;
    float bestDistance = markerGrabPixels;

    for (int i = 0; i < marks.size(); ++i)
    {
        const auto distance = std::abs (timeToX (marks[i]) - x);

        if (distance <= bestDistance)
        {
            bestDistance = distance;
            best = i;
        }
    }

    return best;
}

//==============================================================================
void WaveformView::resized()
{
    peaksDirty = true;
}

double WaveformView::currentLength() const
{
    return viewLength > 0.0 ? viewLength : proc.lengthSeconds();
}

bool WaveformView::peaksNeedRedraw() const
{
    if (peaks.isNull() || waveArea() != drawnArea)
        return true;

    // A pixel is worth about length/width seconds, so anything finer than that
    // cannot change the picture and must not cost a redraw.
    const auto perPixel = currentLength() / (double) juce::jmax (1, drawnArea.getWidth());

    if (std::abs (viewStart - drawnStart) > perPixel * 0.25
         || std::abs (currentLength() - drawnLength) > perPixel * 0.25)
        return true;

    // The peaks themselves changed, which while a file is being read means
    // "a little more of it arrived". Five times a second is enough to watch it
    // fill in, and it keeps a long track from being rasterised on every frame.
    if (peaksDirty)
        return juce::Time::getMillisecondCounter() - lastPeakDraw > 200;

    return false;
}

void WaveformView::redrawPeaks()
{
    const auto area = waveArea();

    if (area.isEmpty())
        return;

    // Drawn at the screen's own pixel density, or the cached picture would be
    // the one soft thing on a Retina display.
    const auto* display = juce::Desktop::getInstance().getDisplays().getPrimaryDisplay();
    const auto scale = display != nullptr ? display->scale : 1.0;

    peaks = juce::Image (juce::Image::ARGB,
                         juce::jmax (1, juce::roundToInt (area.getWidth() * scale)),
                         juce::jmax (1, juce::roundToInt (area.getHeight() * scale)),
                         true);

    juce::Graphics g (peaks);
    g.addTransform (juce::AffineTransform::scale ((float) scale));

    auto& thumb = proc.thumbnail();
    const auto length = currentLength();

    drawnArea = area;
    drawnStart = viewStart;
    drawnLength = length;
    lastPeakDraw = juce::Time::getMillisecondCounter();
    peaksDirty = false;

    if (thumb.getTotalLength() <= 0.0)
        return;

    g.setColour (Look::wave);
    thumb.drawChannels (g, area.withPosition (0, 0), viewStart, viewStart + length, 0.95f);
}

void WaveformView::paintRuler (juce::Graphics& g, juce::Rectangle<int> area)
{
    g.setColour (Look::grid);
    g.fillRect (area);

    const auto length = currentLength();

    if (length <= 0.0)
        return;

    const auto step = rulerStepFor (length, (double) area.getWidth());
    const auto first = std::floor (viewStart / step) * step;

    g.setFont (juce::FontOptions (11.0f));

    for (double t = first; t <= viewStart + length; t += step)
    {
        if (t < viewStart)
            continue;

        const auto x = timeToX (t);

        g.setColour (Look::dim.withAlpha (0.35f));
        g.drawVerticalLine (juce::roundToInt (x), (float) area.getBottom() - 5.0f, (float) area.getBottom());

        g.setColour (Look::dim);
        g.drawText (Look::formatTime (t),
                    juce::Rectangle<float> (x + 3.0f, (float) area.getY(), 80.0f, (float) area.getHeight()),
                    juce::Justification::centredLeft, false);
    }
}

void WaveformView::paint (juce::Graphics& g)
{
    clampView();

    g.fillAll (Look::waveBack);

    const auto area = waveArea();
    paintRuler (g, rulerArea());

    if (! proc.hasMedia())
    {
        g.setColour (Look::dim);
        g.setFont (juce::FontOptions (15.0f));
        g.drawText (emptyMessage, area, juce::Justification::centred);
        return;
    }

    if (peaksNeedRedraw())
        redrawPeaks();

    if (peaks.isValid())
        g.drawImage (peaks, area.toFloat());

    // Zero line, so a quiet passage still reads as audio rather than as a gap.
    g.setColour (Look::grid);
    g.drawHorizontalLine (area.getCentreY(), (float) area.getX(), (float) area.getRight());

    const auto& marks = proc.markers();

    // What Slice writes out is the stretch between one marker and the next, so
    // those stretches are what carry a number: the thing being cut is the gap,
    // not the line. Drawn even when a selection exists, because the markers are
    // what Slice uses and the selection is dragged out by hand.
    g.setFont (juce::FontOptions (11.0f));

    for (int i = 0; i + 1 < marks.size(); ++i)
    {
        const auto x1 = timeToX (marks[i]);
        const auto x2 = timeToX (marks[i + 1]);

        if (x2 < (float) area.getX() || x1 > (float) area.getRight())
            continue;

        juce::Rectangle<float> band (x1, (float) area.getY(),
                                     juce::jmax (1.0f, x2 - x1),
                                     (float) area.getHeight());

        g.setColour (Look::marker.withAlpha (i % 2 == 0 ? 0.10f : 0.16f));
        g.fillRect (band);

        if (band.getWidth() > 22.0f)
        {
            g.setColour (Look::marker);
            g.drawText (juce::String (i + 1),
                        band.removeFromTop (16.0f), juce::Justification::centred, false);
        }
    }

    if (proc.hasRegion())
    {
        const auto x1 = timeToX (proc.regionStartSeconds());
        const auto x2 = timeToX (proc.regionEndSeconds());

        g.setColour (Look::region);
        g.fillRect (juce::Rectangle<float> (x1, (float) area.getY(),
                                            juce::jmax (1.0f, x2 - x1), (float) area.getHeight()));

        // The bar that the selection is dragged out by. It says so in words
        // where there is room, because a stripe nobody recognises is furniture.
        const auto handle = regionHandle();

        g.setColour (Look::wave);
        g.fillRect (handle);

        g.setColour (Look::panel);

        if (handle.getWidth() > 64.0f)
        {
            g.setFont (juce::FontOptions (11.0f));
            g.drawText ("drag out", handle, juce::Justification::centred, false);
        }
        else
        {
            for (int i = -1; i <= 1; ++i)
                g.fillRect (handle.getCentreX() + (float) i * 4.0f - 0.5f,
                            handle.getY() + 5.0f, 1.0f, handle.getHeight() - 10.0f);
        }
    }

    for (auto t : marks)
    {
        const auto x = timeToX (t);

        if (x < (float) area.getX() - 2.0f || x > (float) area.getRight() + 2.0f)
            continue;

        g.setColour (Look::marker);
        g.drawLine (x, (float) area.getY(), x, (float) area.getBottom(), 1.0f);
        g.fillRect (juce::Rectangle<float> (x - 4.0f, (float) area.getY(), 8.0f, 6.0f));
    }

    const auto head = timeToX (proc.position());
    g.setColour (Look::playhead);
    g.drawLine (head, (float) area.getY(), head, (float) area.getBottom(), 1.5f);
}

//==============================================================================
bool WaveformView::overRegion (float x) const
{
    if (! proc.hasRegion())
        return false;

    const auto t = xToTime (x);
    return t >= proc.regionStartSeconds() && t <= proc.regionEndSeconds();
}

juce::Rectangle<float> WaveformView::regionHandle() const
{
    if (! proc.hasRegion())
        return {};

    const auto area = waveArea().toFloat();
    const auto x1 = timeToX (proc.regionStartSeconds());
    const auto x2 = timeToX (proc.regionEndSeconds());

    // A narrow selection still has to be grabbable, so the bar has a floor and
    // grows outwards from the middle of it.
    const auto centre = (x1 + x2) * 0.5f;
    const auto width = juce::jmax (handleMinWidth, x2 - x1);

    return juce::Rectangle<float> (centre - width * 0.5f, area.getY(), width, handleHeight)
               .constrainedWithin (area);
}

void WaveformView::mouseMove (const juce::MouseEvent& e)
{
    setMouseCursor (regionHandle().contains (e.position) ? juce::MouseCursor::DraggingHandCursor
                                                         : juce::MouseCursor::NormalCursor);
}

void WaveformView::mouseDown (const juce::MouseEvent& e)
{
    if (! proc.hasMedia())
        return;

    pressX = (float) e.position.x;
    draggingRegion = false;
    draggingMarker = -1;
    dragOutArmed = false;

    const auto hit = markerNear (pressX);

    if (e.mods.isPopupMenu())
    {
        if (hit >= 0)
            proc.removeMarkerAt (hit);
        else
            proc.clearRegion();

        repaint();
        return;
    }

    if (hit >= 0)
    {
        draggingMarker = hit;
        return;
    }

    // Only the bar at the top of the selection picks it up. Pressing anywhere
    // else -- including inside the selection -- drops it and moves the
    // playhead, because a selection usually covers most of the view and making
    // all of it a grab area left nowhere to click.
    if (regionHandle().contains (e.position))
    {
        dragOutArmed = true;
        return;
    }

    proc.clearRegion();
    dragAnchor = xToTime (pressX);
    proc.setPosition (dragAnchor);
    repaint();
}

void WaveformView::mouseDrag (const juce::MouseEvent& e)
{
    if (! proc.hasMedia())
        return;

    if (draggingMarker >= 0)
    {
        draggingMarker = proc.moveMarker (draggingMarker, xToTime ((float) e.position.x));
        repaint();
        return;
    }

    if (dragOutArmed)
    {
        if (e.getDistanceFromDragStart() > dragOutPixels && onDragOutRegion != nullptr)
        {
            dragOutArmed = false;
            onDragOutRegion();
        }

        return;
    }

    // A selection has to be asked for with the hand. Measured in pixels rather
    // than in seconds, because what counts as a twitch is a property of the
    // finger and not of the zoom.
    if (! draggingRegion && std::abs ((float) e.position.x - pressX) < newRegionPixels)
        return;

    draggingRegion = true;
    proc.setRegion (dragAnchor, xToTime ((float) e.position.x));
    repaint();
}

void WaveformView::mouseUp (const juce::MouseEvent&)
{
    draggingMarker = -1;
    draggingRegion = false;
    dragOutArmed = false;
}

void WaveformView::mouseDoubleClick (const juce::MouseEvent& e)
{
    if (! proc.hasMedia())
        return;

    if (markerNear ((float) e.position.x) < 0)
        proc.addMarker (xToTime ((float) e.position.x));

    repaint();
}

void WaveformView::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel)
{
    const auto total = proc.lengthSeconds();

    if (total <= 0.0)
        return;

    clampView();

    if (e.mods.isCommandDown() || e.mods.isCtrlDown())
    {
        // Zoom about the pointer: the moment under the pointer keeps the same
        // place on screen, so the view grows around it instead of around zero.
        const auto area = waveArea();
        const auto anchor = xToTime ((float) e.position.x);
        const auto across = ((double) e.position.x - (double) area.getX())
                                / (double) juce::jmax (1, area.getWidth());

        viewLength = juce::jlimit (minVisibleSeconds, total,
                                   viewLength * std::pow (1.25, -wheel.deltaY * 4.0));
        viewStart = anchor - across * viewLength;
    }
    else
    {
        const auto length = currentLength();
        viewStart -= wheel.deltaX * length * 0.5 + wheel.deltaY * length * 0.15;
    }

    clampView();
    peaksDirty = true;
    repaint();
}
