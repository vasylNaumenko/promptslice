#include "SliceProcessor.h"

namespace
{
    /** Two markers closer than this are one marker. It is also how far a
        dragged marker stops short of its neighbour. */
    constexpr double markerGapSeconds = 0.001;
}

SliceProcessor::SliceProcessor (const juce::String& appName)
    : AudioProcessor (BusesProperties()
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      config (appName)
{
    formats.registerBasicFormats();
    readAhead.startThread (juce::Thread::Priority::normal);
}

SliceProcessor::~SliceProcessor()
{
    cancelPendingUpdate();
    transport.setSource (nullptr);
    readAhead.stopThread (2000);
}

//==============================================================================
void SliceProcessor::setStatus (const juce::String& text, bool isError)
{
    currentStatus = { text, isError, false, -1.0 };
    sendChangeMessage();
}

void SliceProcessor::setBusy (const juce::String& text, double progress)
{
    currentStatus = { text, false, true, progress };
    sendChangeMessage();
}

//==============================================================================
bool SliceProcessor::openAudio (const juce::File& dir, const juce::File& wav, const juce::String& title)
{
    transport.stop();
    transport.setSource (nullptr);
    readerSource.reset();
    thumb.clear();

    loadedDir = dir;
    loadedAudio = wav;
    loadedTitle = title.isNotEmpty() ? title : dir.getFileName();

    clearRegion();
    markerTimes.clear();

    std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (wav));

    if (reader == nullptr)
    {
        // Said out loud rather than stored and forgotten: an unreadable wav
        // would otherwise show as an empty waveform with no explanation.
        currentStatus = { "Could not open the audio: " + wav.getFullPathName(), true, false, -1.0 };
        sendChangeMessage();
        return false;
    }

    const auto rate = reader->sampleRate;
    auto source = std::make_unique<juce::AudioFormatReaderSource> (reader.release(), true);

    // 32k samples of read-ahead is JUCE's own suggestion for file playback and
    // is about a third of a second at 96k.
    transport.setSource (source.get(), 32768, &readAhead, rate);
    readerSource = std::move (source);

    thumb.setSource (new juce::FileInputSource (wav));
    transport.setPosition (0.0);

    sendChangeMessage();
    return true;
}

void SliceProcessor::closeAudio()
{
    transport.stop();
    transport.setSource (nullptr);
    readerSource.reset();
    thumb.clear();

    loadedDir = loadedAudio = juce::File();
    loadedTitle.clear();

    clearRegion();
    markerTimes.clear();
    sendChangeMessage();
}

double SliceProcessor::lengthSeconds() const
{
    return transport.getLengthInSeconds();
}

//==============================================================================
void SliceProcessor::play()
{
    if (readerSource == nullptr)
        return;

    // Restarting from the end is what a person means by pressing play there.
    const auto end = hasRegion() ? regionEnd.load() : lengthSeconds();

    if (transport.getCurrentPosition() >= end - 0.001)
        transport.setPosition (hasRegion() ? regionStart.load() : 0.0);

    transport.start();
}

void SliceProcessor::stop()
{
    transport.stop();
}

void SliceProcessor::setPosition (double seconds)
{
    transport.setPosition (juce::jlimit (0.0, juce::jmax (0.0, lengthSeconds()), seconds));
}

double SliceProcessor::position() const
{
    return transport.getCurrentPosition();
}

void SliceProcessor::setRegion (double startSeconds, double endSeconds)
{
    const auto had = hasRegion();

    const auto lo = juce::jmin (startSeconds, endSeconds);
    const auto hi = juce::jmax (startSeconds, endSeconds);
    regionStart = juce::jmax (0.0, lo);
    regionEnd = juce::jmin (juce::jmax (0.0, lengthSeconds()), hi);

    // Announced only when the selection appears or disappears. A drag calls
    // this on every mouse move, and the only listener that wants the exact
    // bounds is the waveform, which repaints itself.
    if (hasRegion() != had)
        sendChangeMessage();
}

void SliceProcessor::clearRegion()
{
    const auto had = hasRegion();
    regionStart = 0.0;
    regionEnd = 0.0;

    if (had)
        sendChangeMessage();
}

//==============================================================================
double SliceProcessor::clampToFile (double seconds) const
{
    const auto total = lengthSeconds();

    // A length of zero means no file is open, and clamping to it would put
    // every marker on top of the first one.
    return total > 0.0 ? juce::jlimit (0.0, total, seconds) : juce::jmax (0.0, seconds);
}

void SliceProcessor::addMarker (double seconds)
{
    const auto t = clampToFile (seconds);

    for (auto existing : markerTimes)
        if (std::abs (existing - t) < markerGapSeconds / 2.0)
            return;

    markerTimes.add (t);
    std::sort (markerTimes.begin(), markerTimes.end());
    sendChangeMessage();
}

int SliceProcessor::moveMarker (int index, double seconds)
{
    if (! juce::isPositiveAndBelow (index, markerTimes.size()))
        return -1;

    // Bounded by its neighbours, so markers keep their order and their count:
    // the index stays valid for the whole gesture and nothing can be merged
    // away by dragging.
    const auto lower = index > 0 ? markerTimes[index - 1] + markerGapSeconds : 0.0;
    const auto upper = index + 1 < markerTimes.size() ? markerTimes[index + 1] - markerGapSeconds
                                                      : juce::jmax (0.0, lengthSeconds());

    const auto t = juce::jlimit (lower, juce::jmax (lower, upper), clampToFile (seconds));

    if (std::abs (markerTimes[index] - t) > 1.0e-9)
    {
        markerTimes.set (index, t);
        sendChangeMessage();
    }

    return index;
}

void SliceProcessor::removeMarkerAt (int index)
{
    if (juce::isPositiveAndBelow (index, markerTimes.size()))
    {
        markerTimes.remove (index);
        sendChangeMessage();
    }
}

void SliceProcessor::clearMarkers()
{
    markerTimes.clear();
    sendChangeMessage();
}

//==============================================================================
void SliceProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    transport.prepareToPlay (samplesPerBlock, sampleRate);
}

void SliceProcessor::releaseResources()
{
    transport.releaseResources();
}

bool SliceProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& out = layouts.getMainOutputChannelSet();
    return out == juce::AudioChannelSet::mono() || out == juce::AudioChannelSet::stereo();
}

void SliceProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    buffer.clear();

    if (readerSource == nullptr)
        return;

    juce::AudioSourceChannelInfo info (buffer);
    transport.getNextAudioBlock (info);

    if (! transport.isPlaying() || ! hasRegion())
        return;

    const auto end = regionEnd.load();
    const auto here = transport.getCurrentPosition();

    if (here < end)
        return;

    // Silence exactly the part of the block that lies past the region's end,
    // which is the whole of what the audio thread has to do about it. Seeking
    // or stopping here would take AudioTransportSource's callback lock, held by
    // the message thread whenever it swaps the source.
    const auto rate = getSampleRate();
    const auto overshoot = rate > 0.0 ? (int) ((here - end) * rate) : buffer.getNumSamples();
    const auto keep = juce::jlimit (0, buffer.getNumSamples(), buffer.getNumSamples() - overshoot);

    for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
        buffer.clear (ch, keep, buffer.getNumSamples() - keep);

    if (! regionEndReached.exchange (true))
        triggerAsyncUpdate();
}

void SliceProcessor::handleAsyncUpdate()
{
    regionEndReached = false;

    if (! transport.isPlaying() || ! hasRegion())
        return;

    if (looping.load())
        transport.setPosition (regionStart.load());
    else
        transport.stop();
}

//==============================================================================
void SliceProcessor::saveCommon (juce::XmlElement& state) const
{
    state.setAttribute ("dir", loadedDir.getFullPathName());
    state.setAttribute ("audio", loadedAudio.getFullPathName());
    state.setAttribute ("title", loadedTitle);
    state.setAttribute ("regionStart", regionStart.load());
    state.setAttribute ("regionEnd", regionEnd.load());
    state.setAttribute ("looping", looping.load());

    juce::StringArray times;

    for (auto t : markerTimes)
        times.add (juce::String (t, 6));

    state.setAttribute ("markers", times.joinIntoString (","));
}

void SliceProcessor::loadCommon (const juce::XmlElement& state)
{
    const juce::File wav (state.getStringAttribute ("audio"));

    if (wav.existsAsFile())
        openAudio (juce::File (state.getStringAttribute ("dir")), wav,
                   state.getStringAttribute ("title"));

    setRegion (state.getDoubleAttribute ("regionStart"),
               state.getDoubleAttribute ("regionEnd"));
    looping = state.getBoolAttribute ("looping");

    juce::StringArray times;
    times.addTokens (state.getStringAttribute ("markers"), ",", {});

    for (const auto& t : times)
        if (t.isNotEmpty())
            addMarker (t.getDoubleValue());

    sendChangeMessage();
}
