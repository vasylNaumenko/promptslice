#include "PromptEditor.h"
#include "../Shared/Look.h"
#include "../Shared/SliceExporter.h"

#include <array>
#include <utility>

namespace
{
    constexpr int barHeight = 40;
    constexpr int knobRowHeight = 30;
    constexpr int transportHeight = 40;
    constexpr int stripHeight = 90;
    constexpr int gap = 6;

    /** Below this the slider means "let the service choose", which is what it
        does when the field is absent from the request. One constant, because a
        label that says auto while the request asks for 0.4 s is a lie nothing
        would report. */
    constexpr double autoLengthBelow = 0.5;

    juce::String durationText (double seconds)
    {
        return seconds < autoLengthBelow ? "auto" : juce::String (seconds, 1) + " s";
    }

    double requestedLength (double sliderValue)
    {
        return sliderValue < autoLengthBelow ? 0.0 : sliderValue;
    }

    /** 48000 reads as "48 kHz" and 22050 as "22.05 kHz": the trailing zeros are
        trimmed rather than a decimal count chosen per rate. */
    juce::String rateText (int rate)
    {
        return juce::String (rate / 1000.0, 2).trimCharactersAtEnd ("0").trimCharactersAtEnd (".")
                   + " kHz";
    }

    /** What the model box offers, in the order it offers it. A table rather
        than arithmetic on the enum's own values: those decide nothing about
        this row, and a value inserted into the enum would otherwise relabel
        every entry silently — picking "Sound v2" and storing v3. */
    const std::array<std::pair<Generator::Model, const char*>, 3> modelChoices
    {{
        { Generator::Model::automatic, "Auto" },
        { Generator::Model::v2,        "Sound v2" },
        { Generator::Model::v3,        "Sound v3" },
    }};
}

PromptEditor::PromptEditor (PromptProcessor& p)
    : AudioProcessorEditor (&p)
{
    cutFormats.registerBasicFormats();

    wave = std::make_unique<WaveformView> (p);
    wave->onDragOutRegion = [this] { dragOutRegion(); };
    wave->setEmptyMessage ("Type what the sound should be and press Generate");

    promptField.setTextToShowWhenEmpty ("What should it sound like?", Look::dim);
    promptField.setSelectAllWhenFocused (true);
    promptField.setText (owner().lastPrompt(), juce::dontSendNotification);
    promptField.onReturnKey = [this] { generateButton.triggerClick(); };
    addAndMakeVisible (promptField);

    generateButton.onClick = [this]
    {
        if (owner().isGenerating())
            owner().cancelGeneration();
        else
            owner().generate (promptField.getText(),
                              (int) countSlider.getValue(),
                              requestedLength (durationSlider.getValue()),
                              influenceSlider.getValue());
    };

    newButton.onClick = [this] { owner().newBatch(); promptField.grabKeyboardFocus(); };
    newButton.setTooltip ("Let go of this batch and start another. The files stay on disk "
                          "and Library brings them back.");

    keyButton.onClick = [this] { askForKey(); };

    libraryButton.onClick = [this] { openLibrary(); };
    libraryButton.setTooltip ("Open a batch made earlier, with the settings that made it.");

    folderButton.onClick = [this] { chooseFolder(); };
    folderButton.setTooltip ("Where new batches are saved.");

    revealButton.onClick = [this]
    {
        const auto dir = owner().takesDir() != juce::File() ? owner().takesDir()
                                                            : owner().settings().downloadDir();
        dir.revealToUser();
    };

    countSlider.setRange (1.0, 5.0, 1.0);
    countSlider.setValue (3.0, juce::dontSendNotification);
    countSlider.setTooltip ("How many takes to ask for. Each one is a separate request.");

    // Zero is a real position on this slider and means the service decides.
    durationSlider.textFromValueFunction = [] (double v) { return durationText (v); };
    durationSlider.valueFromTextFunction = [] (const juce::String& t) { return t.getDoubleValue(); };
    durationSlider.setRange (0.0, 22.0, 0.1);
    durationSlider.setValue (0.0, juce::dontSendNotification);
    durationSlider.updateText();
    durationSlider.setTooltip ("How long the sound should be. Leftmost lets ElevenLabs choose.");

    influenceSlider.setRange (0.0, 1.0, 0.05);
    influenceSlider.setValue (0.3, juce::dontSendNotification);
    influenceSlider.setTooltip ("How closely to hold to the prompt. Higher is more literal, "
                                "lower leaves more room to invent.");

    for (auto* s : { &countSlider, &durationSlider, &influenceSlider })
    {
        s->setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
        addAndMakeVisible (s);
    }

    // Auto sends no model at all, which is what the plugin did before there was
    // a choice — so the entry that is not a model has to be first and selected,
    // or opening the window would quietly change what the next prompt asks for.
    for (int i = 0; i < (int) modelChoices.size(); ++i)
    {
        modelBox.addItem (modelChoices[(size_t) i].second, i + 1);

        if (modelChoices[(size_t) i].first == owner().model())
            modelBox.setSelectedId (i + 1, juce::dontSendNotification);
    }

    modelBox.setTooltip ("Which model answers. Auto leaves the choice to ElevenLabs. "
                         "All three cost the same.");
    modelBox.onChange = [this]
    {
        if (const auto i = modelBox.getSelectedId() - 1; juce::isPositiveAndBelow (i, (int) modelChoices.size()))
            owner().setModel (modelChoices[(size_t) i].first);
    };

    for (int i = 0; i < Generator::sampleRates().size(); ++i)
    {
        const auto rate = Generator::sampleRates()[i];
        rateBox.addItem (rateText (rate), i + 1);

        if (rate == owner().outputRate())
            rateBox.setSelectedId (i + 1, juce::dontSendNotification);
    }

    rateBox.setTooltip ("The rate the takes are written at. Lossless at every setting, "
                        "and the price is by the second, so this only trades disk space.");
    rateBox.onChange = [this]
    {
        // Nothing selected is id zero, and an unchecked index into the rates
        // would quietly store a rate the endpoint does not offer.
        if (const auto rate = Generator::sampleRates()[rateBox.getSelectedId() - 1]; rate > 0)
            owner().setOutputRate (rate);
    };

    for (auto* c : { &modelBox, &rateBox })
        addAndMakeVisible (c);

    countLabel.setText ("Takes", juce::dontSendNotification);
    durationLabel.setText ("Length", juce::dontSendNotification);
    influenceLabel.setText ("Follow prompt", juce::dontSendNotification);
    modelLabel.setText ("Model", juce::dontSendNotification);
    rateLabel.setText ("Quality", juce::dontSendNotification);

    for (auto* l : { &countLabel, &durationLabel, &influenceLabel, &modelLabel, &rateLabel })
    {
        l->setColour (juce::Label::textColourId, Look::dim);
        l->setFont (juce::FontOptions (12.0f));
        addAndMakeVisible (l);
    }

    creditsLabel.setColour (juce::Label::textColourId, Look::dim);
    creditsLabel.setFont (juce::FontOptions (12.0f));
    creditsLabel.setJustificationType (juce::Justification::centredRight);
    addAndMakeVisible (creditsLabel);

    playButton.onClick = [this]
    {
        if (owner().isPlaying()) owner().stop();
        else                     owner().play();
    };

    stopButton.onClick = [this]
    {
        owner().stop();
        owner().setPosition (owner().hasRegion() ? owner().regionStartSeconds() : 0.0);
    };

    loopButton.setClickingTogglesState (true);
    loopButton.setToggleState (owner().isLooping(), juce::dontSendNotification);
    loopButton.onClick = [this] { owner().setLooping (loopButton.getToggleState()); };

    markerButton.onClick = [this] { owner().addMarker (owner().position()); wave->repaint(); };
    clearButton.onClick  = [this] { owner().clearMarkers(); wave->repaint(); };
    sliceButton.onClick  = [this] { cutSlices(); };
    fitButton.onClick    = [this] { wave->zoomToFit(); };

    for (auto* b : { &newButton, &generateButton, &keyButton, &libraryButton,
                     &folderButton, &revealButton,
                     &playButton, &stopButton, &loopButton, &markerButton,
                     &sliceButton, &clearButton, &fitButton })
        addAndMakeVisible (b);

    statusLabel.setColour (juce::Label::textColourId, Look::dim);
    statusLabel.setFont (juce::FontOptions (12.0f));
    addAndMakeVisible (statusLabel);

    timeLabel.setColour (juce::Label::textColourId, Look::text);
    timeLabel.setFont (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), 13.0f, juce::Font::plain));
    timeLabel.setJustificationType (juce::Justification::centredRight);
    addAndMakeVisible (timeLabel);

    progressBar.setPercentageDisplay (false);
    addChildComponent (progressBar);

    addAndMakeVisible (*wave);

    // Pressing a take loads it, which is the whole point of asking for five.
    strip.onSelect = [this] (const juce::File& f)
    {
        owner().openAudio (owner().takesDir(), f, owner().lastPrompt());
    };

    addAndMakeVisible (strip);
    owner().addChangeListener (this);

    setResizable (true, true);
    // Wider than it was: the top row gained New and Library, and below about
    // this the prompt field is squeezed to nothing by its own buttons.
    setResizeLimits (860, 480, 3000, 2000);
    setSize (980, 640);

    shownAudio = owner().audioFile();
    shownFolder = owner().takesDir();
    shownLanded = owner().filesLanded();
    strip.setFolder (shownFolder);
    strip.setSelected (shownAudio);
    syncStatus();
    syncCredits();
    updateEnablement();

    // Asked when the window opens, because that is when somebody is looking at
    // the number. Without a key it does nothing.
    owner().refreshBalance();

    startTimerHz (30);
}

PromptEditor::~PromptEditor()
{
    stopTimer();
    owner().removeChangeListener (this);
}

//==============================================================================
void PromptEditor::askForKey()
{
    keyWindow = std::make_unique<juce::AlertWindow> ("ElevenLabs API key",
                                                     "The key is kept in this plugin's settings file "
                                                     "and is never written into a project.",
                                                     juce::MessageBoxIconType::NoIcon, this);

    keyWindow->addTextEditor ("key", owner().apiKey(), "Key:", true);
    keyWindow->addButton ("Save", 1, juce::KeyPress (juce::KeyPress::returnKey));
    keyWindow->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));

    keyWindow->enterModalState (true, juce::ModalCallbackFunction::create (
        [this] (int result)
        {
            if (result == 1 && keyWindow != nullptr)
            {
                owner().setApiKey (keyWindow->getTextEditorContents ("key"));
                owner().setStatus ("Key saved to "
                                       + owner().settings().configFile().getFullPathName(), false);

                // A new key has its own balance, and its own permission to be
                // asked for one.
                owner().refreshBalance();
            }

            keyWindow.reset();
            updateEnablement();
        }), false);
}

void PromptEditor::chooseDirectory (const juce::String& title,
                                    std::function<void (const juce::File&)> use)
{
    // One at a time. The panel is a sheet on its own window rather than modal to
    // the application, so the buttons behind it can still be pressed -- and
    // replacing the chooser here would destroy an object JUCE still has a
    // callback outstanding on.
    if (chooser != nullptr)
        return;

    chooser = std::make_unique<juce::FileChooser> (title, owner().settings().downloadDir());

    chooser->launchAsync (juce::FileBrowserComponent::openMode
                              | juce::FileBrowserComponent::canSelectDirectories,
                          [this, use] (const juce::FileChooser& fc)
    {
        const auto dir = fc.getResult();

        if (dir != juce::File() && dir.isDirectory())
            use (dir);

        // Let go on the next message rather than here: this runs from inside
        // the chooser's own callback, and freeing it under itself is the fault
        // the guard above exists to prevent.
        juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<PromptEditor> (this)]
        {
            if (safe != nullptr)
                safe->chooser.reset();
        });
    });
}

void PromptEditor::chooseFolder()
{
    chooseDirectory ("Where to keep the takes", [this] (const juce::File& dir)
    {
        owner().settings().setDownloadDir (dir);
        owner().setStatus ("Folder: " + dir.getFullPathName(), false);
    });
}

void PromptEditor::openLibrary()
{
    // Browsing folders rather than files: a batch is a folder, and its name
    // already carries the prompt and the moment it was made, which is what a
    // person recognises it by.
    chooseDirectory ("Open a batch", [this] (const juce::File& dir)
    {
        owner().loadBatch (dir);
    });
}

void PromptEditor::syncControls()
{
    const auto& b = owner().batch();

    promptField.setText (b.prompt, juce::dontSendNotification);
    countSlider.setValue (b.takes, juce::dontSendNotification);
    influenceSlider.setValue (b.promptInfluence, juce::dontSendNotification);

    // The length's text is written by a function of its own, and setting the
    // value without a notification does not re-run it — so a reopened batch
    // would show the right position under the wrong words.
    durationSlider.setValue (b.lengthSeconds, juce::dontSendNotification);
    durationSlider.updateText();

    // Model and rate are read from the settings rather than from the note:
    // loadBatch has already put the note's values there, and the settings are
    // what the next Generate will actually use.
    for (int i = 0; i < (int) modelChoices.size(); ++i)
        if (modelChoices[(size_t) i].first == owner().model())
            modelBox.setSelectedId (i + 1, juce::dontSendNotification);

    rateBox.setSelectedId (Generator::sampleRates().indexOf (owner().outputRate()) + 1,
                           juce::dontSendNotification);
}

//==============================================================================
juce::File PromptEditor::writeCut (double fromSeconds, double toSeconds, int index)
{
    // Cuts land beside the takes rather than in a folder of their own, so one
    // row holds everything a person can drag into the project.
    return SliceExporter::write (cutFormats, owner().audioFile(), owner().takesDir(),
                                 SliceExporter::cutBaseName + " "
                                     + juce::String (index).paddedLeft ('0', 2),
                                 fromSeconds, toSeconds);
}

void PromptEditor::dragOutRegion()
{
    if (! owner().hasMedia() || ! owner().hasRegion())
        return;

    const auto file = writeCut (owner().regionStartSeconds(), owner().regionEndSeconds(),
                                SliceExporter::nextIndexIn (owner().takesDir(), SliceExporter::cutBaseName));

    if (file == juce::File())
        return owner().setStatus ("Could not write the cut.", true);

    const auto started = juce::DragAndDropContainer::performExternalDragDropOfFiles (
        { file.getFullPathName() }, false, wave.get(),
        [safe = juce::Component::SafePointer<PromptEditor> (this)]
        {
            if (safe != nullptr)
                safe->strip.refresh();
        });

    if (! started)
    {
        strip.refresh();
        owner().setStatus ("The drag did not start. The cut is in the row below.", true);
    }
}

void PromptEditor::cutSlices()
{
    if (! owner().hasMedia())
        return;

    juce::Array<juce::Range<double>> cuts;
    const auto& marks = owner().markers();

    for (int i = 0; i + 1 < marks.size(); ++i)
        if (marks[i + 1] > marks[i] + 0.005)
            cuts.add ({ marks[i], marks[i + 1] });

    if (cuts.isEmpty() && owner().hasRegion())
        cuts.add ({ owner().regionStartSeconds(), owner().regionEndSeconds() });

    if (cuts.isEmpty())
        return owner().setStatus ("Nothing to cut: add markers or select a stretch.", true);

    int index = SliceExporter::nextIndexIn (owner().takesDir(), SliceExporter::cutBaseName);
    int written = 0;

    for (const auto& cut : cuts)
        if (writeCut (cut.getStart(), cut.getEnd(), index++) != juce::File())
            ++written;

    strip.refresh();

    const auto all = written == cuts.size();
    owner().setStatus (all ? "Cut " + juce::String (written) + ". Drag them into the playlist."
                           : "Wrote " + juce::String (written) + " of " + juce::String (cuts.size()),
                       ! all);
}

//==============================================================================
void PromptEditor::syncStatus()
{
    const auto& s = owner().status();

    statusLabel.setColour (juce::Label::textColourId, s.error ? Look::error : Look::dim);
    statusLabel.setText (s.text.upToFirstOccurrenceOf ("\n", false, false), juce::dontSendNotification);
    statusLabel.setTooltip (s.text);

    progress = s.progress < 0.0 ? -1.0 : s.progress;
    progressBar.setVisible (s.busy);
}

void PromptEditor::syncCredits()
{
    const auto& b = owner().balance();

    if (b.kind == Generator::Balance::Kind::remaining)
    {
        creditsLabel.setText (juce::String (b.left()) + " credits left", juce::dontSendNotification);
        creditsLabel.setTooltip (juce::String (b.used) + " of " + juce::String (b.limit)
                                     + " used this period.");
        return;
    }

    // Worded as what it is. "Spent" and "left" are different facts, and a
    // number of one printed in the place of the other is the one mistake this
    // corner of the window can make.
    if (b.kind == Generator::Balance::Kind::spent)
    {
        creditsLabel.setText (juce::String (b.used) + " credits spent in "
                                  + juce::String (b.days) + " days",
                              juce::dontSendNotification);
        creditsLabel.setTooltip (b.note);
        return;
    }

    // Blank rather than a zero when there is nothing to ask with: a number that
    // is not a balance is worse than no number.
    if (owner().apiKey().isEmpty())
    {
        creditsLabel.setText ({}, juce::dontSendNotification);
        creditsLabel.setTooltip ({});
        return;
    }

    // No answer yet is not the same as a refusal, and the first is what the
    // second or two after the window opens actually is.
    if (b.note.isEmpty())
    {
        creditsLabel.setText ("checking balance...", juce::dontSendNotification);
        creditsLabel.setTooltip ("Asking ElevenLabs what the account has left.");
        return;
    }

    creditsLabel.setText ("balance unavailable", juce::dontSendNotification);
    creditsLabel.setTooltip (b.note);
}

void PromptEditor::updateEnablement()
{
    const auto busy = owner().isGenerating();
    const auto has = owner().hasMedia();

    generateButton.setButtonText (busy ? "Stop" : "Generate");
    keyButton.setButtonText (owner().apiKey().isEmpty() ? "Key..." : "Key set");

    for (auto* b : { &playButton, &stopButton, &loopButton, &markerButton, &fitButton })
        b->setEnabled (has);

    clearButton.setEnabled (has && ! owner().markers().isEmpty());
    sliceButton.setEnabled (has && (owner().hasRegion() || owner().markers().size() >= 2));
}

void PromptEditor::changeListenerCallback (juce::ChangeBroadcaster*)
{
    syncStatus();
    syncCredits();

    if (owner().takesDir() != shownFolder)
    {
        shownFolder = owner().takesDir();
        shownLanded = owner().filesLanded();
        strip.setFolder (shownFolder);

        // The batch changed under the window, which is the one moment the
        // controls may be written to without arguing with the person using
        // them. Generating lands here too, harmlessly: the values it puts back
        // are the ones it was just given.
        syncControls();
    }
    else if (owner().filesLanded() != shownLanded)
    {
        // A take has landed in the folder already on show. Counted rather than
        // gated on "is it still generating": the generator stops being busy in
        // the same breath as it announces the last take, and the announcement
        // arrives afterwards -- so asking whether it is busy loses that take,
        // and the row ends one short of what is on disk. Every other change
        // message -- a marker moving, the status changing -- leaves the count
        // alone, and the editor's own writes and deletes ask for a refresh
        // where they happen.
        shownLanded = owner().filesLanded();
        strip.refresh();
    }

    if (owner().audioFile() != shownAudio)
    {
        shownAudio = owner().audioFile();
        strip.setSelected (shownAudio);
        wave->zoomToFit();
    }

    updateEnablement();
}

void PromptEditor::timerCallback()
{
    const auto pos = owner().position();
    const auto playing = owner().isPlaying();

    wave->repaint();
    playButton.setButtonText (playing ? "Pause" : "Play");
    timeLabel.setText (Look::formatTime (pos) + "  /  " + Look::formatTime (owner().lengthSeconds()),
                       juce::dontSendNotification);

    if (const auto busy = owner().isGenerating(); busy != shownBusy)
    {
        shownBusy = busy;
        updateEnablement();
    }
}

//==============================================================================
void PromptEditor::paint (juce::Graphics& g)
{
    g.fillAll (Look::panel);
}

bool PromptEditor::keyPressed (const juce::KeyPress& key)
{
    if (! owner().hasMedia())
        return false;

    if (key == juce::KeyPress::escapeKey)
    {
        owner().clearRegion();
        wave->repaint();
        return true;
    }

    if (key == juce::KeyPress::spaceKey)
    {
        if (owner().isPlaying()) owner().stop();
        else                     owner().play();
        return true;
    }

    if (key.getTextCharacter() == 'm' || key.getTextCharacter() == 'M')
    {
        owner().addMarker (owner().position());
        wave->repaint();
        return true;
    }

    return false;
}

void PromptEditor::resized()
{
    auto area = getLocalBounds().reduced (gap);

    auto top = area.removeFromTop (barHeight);
    newButton.setBounds (top.removeFromLeft (60).reduced (2));
    generateButton.setBounds (top.removeFromRight (110).reduced (2));
    keyButton.setBounds (top.removeFromRight (90).reduced (2));
    folderButton.setBounds (top.removeFromRight (90).reduced (2));
    revealButton.setBounds (top.removeFromRight (80).reduced (2));
    libraryButton.setBounds (top.removeFromRight (90).reduced (2));
    promptField.setBounds (top.reduced (2));

    area.removeFromTop (gap);

    auto knobs = area.removeFromTop (knobRowHeight);
    countLabel.setBounds (knobs.removeFromLeft (46));
    countSlider.setBounds (knobs.removeFromLeft (96).reduced (0, 2));
    knobs.removeFromLeft (gap * 2);
    durationLabel.setBounds (knobs.removeFromLeft (54));
    durationSlider.setBounds (knobs.removeFromLeft (200).reduced (0, 2));
    knobs.removeFromLeft (gap * 2);
    influenceLabel.setBounds (knobs.removeFromLeft (94));
    influenceSlider.setBounds (knobs.removeFromLeft (180).reduced (0, 2));

    area.removeFromTop (gap);

    // What is settled once sits on its own row, away from the three that are
    // turned per prompt. The balance goes on the right of it because it belongs
    // to the same question -- what a press of Generate is about to cost.
    auto quality = area.removeFromTop (knobRowHeight);
    creditsLabel.setBounds (quality.removeFromRight (240).reduced (0, 2));
    modelLabel.setBounds (quality.removeFromLeft (46));
    modelBox.setBounds (quality.removeFromLeft (120).reduced (0, 2));
    quality.removeFromLeft (gap * 2);
    rateLabel.setBounds (quality.removeFromLeft (54));
    rateBox.setBounds (quality.removeFromLeft (110).reduced (0, 2));

    area.removeFromTop (gap);

    auto statusRow = area.removeFromTop (18);
    progressBar.setBounds (statusRow.removeFromRight (160).reduced (0, 2));
    statusLabel.setBounds (statusRow);

    area.removeFromTop (gap);

    strip.setBounds (area.removeFromBottom (stripHeight));
    area.removeFromBottom (gap);

    auto transport = area.removeFromBottom (transportHeight);
    playButton.setBounds (transport.removeFromLeft (56).reduced (2));
    stopButton.setBounds (transport.removeFromLeft (56).reduced (2));
    loopButton.setBounds (transport.removeFromLeft (70).reduced (2));
    transport.removeFromLeft (gap);
    markerButton.setBounds (transport.removeFromLeft (80).reduced (2));
    sliceButton.setBounds (transport.removeFromLeft (70).reduced (2));
    clearButton.setBounds (transport.removeFromLeft (130).reduced (2));
    fitButton.setBounds (transport.removeFromLeft (70).reduced (2));
    timeLabel.setBounds (transport.reduced (4, 2));

    area.removeFromBottom (gap);
    wave->setBounds (area);
}
