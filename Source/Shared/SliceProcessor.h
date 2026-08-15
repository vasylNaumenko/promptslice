#pragma once

#include <juce_audio_utils/juce_audio_utils.h>

#include "Config.h"

/** Everything both plugins do with a wav once they have one: play it, put a
    playhead and markers on it, hold a selection, and say what is going on.

    Where the wav comes from is the only thing the two differ in — one fetches a
    video, the other asks for a sound to be generated — so that part lives in
    the subclass and nothing else does.

    Audio is streamed from disk rather than held in memory: a wav is
    float-expanded at ~11 MB a minute per channel, and AudioTransportSource
    reads ahead on its own thread and converts to the host's rate for free.
*/
class SliceProcessor : public juce::AudioProcessor,
                       public juce::ChangeBroadcaster,
                       private juce::AsyncUpdater
{
public:
    /** appName names the settings file this plugin keeps. */
    explicit SliceProcessor (const juce::String& appName);
    ~SliceProcessor() override;

    //==============================================================================
    Config& settings() { return config; }

    /** What the plugin is doing and how it went. One owner, so a line written
        by a screen cannot be silently reverted by the next change message. */
    struct Status
    {
        juce::String text;
        bool error = false;
        bool busy = false;
        double progress = -1.0;    // below zero means there is nothing to measure
    };

    const Status& status() const { return currentStatus; }
    void setStatus (const juce::String& text, bool isError);
    void setBusy (const juce::String& text, double progress);

    //==============================================================================
    /** Points the transport and the waveform at a wav. */
    bool openAudio (const juce::File& dir, const juce::File& wav, const juce::String& title);
    void closeAudio();

    juce::File audioFile() const    { return loadedAudio; }
    juce::File mediaDir() const     { return loadedDir; }
    juce::String mediaTitle() const { return loadedTitle; }
    bool hasMedia() const           { return readerSource != nullptr; }

    double lengthSeconds() const;
    juce::AudioThumbnail& thumbnail() { return thumb; }

    //==============================================================================
    void play();
    void stop();
    bool isPlaying() const            { return transport.isPlaying(); }
    void setPosition (double seconds);
    double position() const;

    /** The stretch the transport is allowed to play. An empty region means the
        whole file. */
    void setRegion (double startSeconds, double endSeconds);
    void clearRegion();
    bool hasRegion() const            { return regionEnd.load() > regionStart.load(); }
    double regionStartSeconds() const { return regionStart.load(); }
    double regionEndSeconds() const   { return regionEnd.load(); }

    void setLooping (bool shouldLoop) { looping = shouldLoop; }
    bool isLooping() const            { return looping.load(); }

    //==============================================================================
    const juce::Array<double>& markers() const { return markerTimes; }
    void addMarker (double seconds);
    void removeMarkerAt (int index);
    void clearMarkers();

    /** Moves one marker without letting it pass or merge into its neighbours,
        and returns the index it still has. */
    int moveMarker (int index, double seconds);

    //==============================================================================
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    bool hasEditor() const override                            { return true; }
    bool acceptsMidi() const override                          { return true; }
    bool producesMidi() const override                         { return false; }
    bool isMidiEffect() const override                         { return false; }
    double getTailLengthSeconds() const override               { return 0.0; }

    int getNumPrograms() override                              { return 1; }
    int getCurrentProgram() override                           { return 0; }
    void setCurrentProgram (int) override                      {}
    const juce::String getProgramName (int) override           { return {}; }
    void changeProgramName (int, const juce::String&) override {}

protected:
    /** The part of the saved state both plugins share. A subclass writes its
        own fields into the same element. */
    void saveCommon (juce::XmlElement& state) const;
    void loadCommon (const juce::XmlElement& state);

    Config config;
    Status currentStatus;

private:
    void handleAsyncUpdate() override;
    double clampToFile (double seconds) const;

    juce::AudioFormatManager formats;
    juce::AudioThumbnailCache thumbCache { 8 };
    juce::AudioThumbnail thumb { 512, formats, thumbCache };

    // Read-ahead for the transport happens here rather than on the audio thread.
    juce::TimeSliceThread readAhead { "slice read-ahead" };
    juce::AudioTransportSource transport;
    std::unique_ptr<juce::AudioFormatReaderSource> readerSource;

    juce::File loadedDir, loadedAudio;
    juce::String loadedTitle;

    std::atomic<double> regionStart { 0.0 }, regionEnd { 0.0 };
    std::atomic<bool> looping { false };
    std::atomic<bool> regionEndReached { false };

    juce::Array<double> markerTimes;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SliceProcessor)
};
