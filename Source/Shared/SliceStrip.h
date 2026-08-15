#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

/** The row of wav files under the waveform.

    It is what a person actually leaves with: pressing one auditions it,
    dragging one hands it to the host, and the cross puts it in the trash.
    Dragging is the whole import mechanism, and not for want of trying — VST3
    gives a plugin no way to create a channel or place a clip in the host's
    project, so a file the host will accept a drop of is the only door.

    YTSlice fills it with the pieces cut out of a video; PromptSlice fills it
    with the takes that came back from a prompt. Same row either way.
*/
class SliceStrip final : public juce::Component,
                        private juce::AsyncUpdater
{
public:
    SliceStrip();
    ~SliceStrip() override;

    /** Lists the wav files in this folder. Safe to call from a chip's own
        handler and safe to call often: the work is deferred and coalesced, and
        it stops early when the folder holds what it held before. */
    void setFolder (const juce::File& dir);
    void refresh();

    /** Marks one as the current one, without telling anybody. */
    void setSelected (const juce::File& file);
    juce::File selected() const { return current; }

    /** A chip was pressed. */
    std::function<void (const juce::File&)> onSelect;

    /** Whether a file in the row was cut out of another one, which is the only
        thing the chips' colours say: purple was cut, blue arrived whole.

        Answered by default from the name SliceExporter writes cuts under, which
        is what PromptSlice needs — takes and cuts share one folder there and
        the name is all that separates them. It is a question rather than a
        setting because the other honest answer is not a name at all: YTSlice
        keeps its slices in a folder of their own, so every file in that row is
        a cut whatever it is called. */
    std::function<bool (const juce::File&)> isCut;

    void resized() override;
    void paint (juce::Graphics&) override;

private:
    class Chip;

    void handleAsyncUpdate() override;
    void rebuild();

    juce::Viewport viewport;
    juce::Component row;
    juce::OwnedArray<Chip> chips;
    juce::File folder, current;

    // What the row is currently showing, so an unchanged folder costs a
    // directory listing and nothing else.
    juce::Array<juce::File> shown;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SliceStrip)
};
