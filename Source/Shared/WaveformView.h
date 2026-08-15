#pragma once

#include <juce_audio_utils/juce_audio_utils.h>

class SliceProcessor;

/** The waveform under the video: a ruler, the peaks, the markers, the region
    between them and the playhead.

    The peaks are drawn into an image and reused, because the playhead moves
    sixty times a second and the waveform behind it does not. Redrawing the
    thumbnail on every one of those frames is the difference between a cheap
    overlay and re-rasterising the whole track.
*/
class WaveformView final : public juce::Component,
                           private juce::ChangeListener
{
public:
    explicit WaveformView (SliceProcessor&);
    ~WaveformView() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

    /** Called when a selection is picked up and pulled, so the owner can write
        it out and hand it to the host. */
    std::function<void()> onDragOutRegion;

    /** Puts the whole track back in view. */
    void zoomToFit();

    /** What to say when there is nothing loaded. The two plugins are told
        apart here and nowhere else in this class. */
    void setEmptyMessage (const juce::String& text);

private:
    void changeListenerCallback (juce::ChangeBroadcaster*) override;

    double xToTime (float x) const;
    float timeToX (double t) const;
    int markerNear (float x) const;
    void clampView();

    juce::Rectangle<int> rulerArea() const;
    juce::Rectangle<int> waveArea() const;
    double currentLength() const;

    /** The bar along the top of the selection that the selection is dragged
        out by. It exists so that pressing anywhere else -- including inside the
        selection -- can go on meaning "put the playhead here and drop the
        selection". A selection covers most of the view more often than not, so
        making the whole of it a grab area left no room to click. */
    juce::Rectangle<float> regionHandle() const;

    void paintRuler (juce::Graphics&, juce::Rectangle<int>);
    bool peaksNeedRedraw() const;
    void redrawPeaks();

    SliceProcessor& proc;

    juce::String emptyMessage { "Nothing loaded" };

    double viewStart = 0.0;
    double viewLength = 0.0;      // zero means "the whole file"

    juce::Image peaks;
    bool peaksDirty = true;

    // What the cached picture was drawn for. Rasterising a whole track is the
    // dearest thing this view does, and the thumbnail announces itself many
    // times a second while it reads the file, so a redraw has to be asked for
    // by a real change rather than by every announcement.
    juce::Rectangle<int> drawnArea;
    double drawnStart = -1.0, drawnLength = -1.0;
    juce::uint32 lastPeakDraw = 0;

    // Set while a drag is defining a region, so a click that only moves the
    // playhead is told apart from a drag that selects.
    double dragAnchor = 0.0;
    float pressX = 0.0f;
    bool draggingRegion = false;
    int draggingMarker = -1;

    // Pressing inside an existing selection means "pick this up", not "start a
    // new one", so the two gestures never have to be told apart by timing.
    bool dragOutArmed = false;

    bool overRegion (float x) const;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (WaveformView)
};
