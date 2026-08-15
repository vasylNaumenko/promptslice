#pragma once

#include <juce_audio_utils/juce_audio_utils.h>

#include "../Shared/SliceStrip.h"
#include "../Shared/WaveformView.h"
#include "PromptProcessor.h"

/** A prompt on top, the takes in a row underneath, and the selected one on the
    waveform between them.
*/
class PromptEditor final : public juce::AudioProcessorEditor,
                           private juce::Timer,
                           private juce::ChangeListener
{
public:
    explicit PromptEditor (PromptProcessor&);
    ~PromptEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress&) override;

private:
    void timerCallback() override;
    void changeListenerCallback (juce::ChangeBroadcaster*) override;

    void askForKey();
    void chooseFolder();
    void cutSlices();
    void dragOutRegion();
    juce::File writeCut (double fromSeconds, double toSeconds, int index);
    void syncStatus();
    void syncCredits();
    void updateEnablement();

    PromptProcessor& owner() const { return static_cast<PromptProcessor&> (processor); }

    juce::TextEditor promptField;
    juce::TextButton generateButton { "Generate" };
    juce::TextButton keyButton { "Key..." };
    juce::TextButton folderButton { "Folder..." };
    juce::TextButton revealButton { "Reveal" };
    juce::Label statusLabel;

    juce::Slider countSlider { juce::Slider::IncDecButtons, juce::Slider::TextBoxLeft };
    juce::Slider durationSlider { juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight };
    juce::Slider influenceSlider { juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight };
    juce::Label countLabel, durationLabel, influenceLabel;

    juce::ComboBox modelBox, rateBox;
    juce::Label modelLabel, rateLabel, creditsLabel;

    std::unique_ptr<WaveformView> wave;

    juce::TextButton playButton { "Play" };
    juce::TextButton stopButton { "Stop" };
    juce::TextButton loopButton { "Loop" };
    juce::TextButton markerButton { "Marker" };
    juce::TextButton sliceButton { "Cut" };
    juce::TextButton clearButton { "Clear markers" };
    juce::TextButton fitButton { "Fit" };
    juce::Label timeLabel;

    juce::AudioFormatManager cutFormats;
    SliceStrip strip;

    double progress = 0.0;
    juce::ProgressBar progressBar { progress };

    juce::File shownAudio, shownFolder;
    int shownLanded = 0;
    bool shownBusy = false;

    std::unique_ptr<juce::FileChooser> chooser;
    std::unique_ptr<juce::AlertWindow> keyWindow;

    juce::TooltipWindow tooltips { this, 700 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PromptEditor)
};
