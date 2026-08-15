#include "SliceExporter.h"

namespace
{
    constexpr int copyChunkSamples = 1 << 15;
    constexpr double fadeOutSeconds = 0.001;

    juce::File uniqueFileIn (const juce::File& dir, const juce::String& stem)
    {
        auto candidate = dir.getChildFile (stem + ".wav");

        for (int n = 2; candidate.existsAsFile() && n < 1000; ++n)
            candidate = dir.getChildFile (stem + " (" + juce::String (n) + ").wav");

        return candidate;
    }

    /** The sample nearest to pos where the signal changes sign, or pos itself
        if it never does inside the window. Channels are summed, because a cut
        has to be one position for the whole file and a crossing that only one
        side agrees with is not a crossing. */
    juce::int64 snapToZeroCrossing (juce::AudioFormatReader& reader,
                                    juce::int64 pos,
                                    juce::int64 radius,
                                    juce::int64 total)
    {
        const auto from = juce::jmax<juce::int64> (0, pos - radius);
        const auto to   = juce::jmin (total, pos + radius);
        const auto n    = (int) (to - from);

        if (n < 3)
            return pos;

        const auto channels = juce::jlimit (1, 2, (int) reader.numChannels);
        juce::AudioBuffer<float> window (channels, n);
        reader.read (&window, 0, n, from, true, channels > 1);

        std::vector<float> mono ((size_t) n, 0.0f);

        for (int c = 0; c < channels; ++c)
        {
            const auto* src = window.getReadPointer (c);

            for (int i = 0; i < n; ++i)
                mono[(size_t) i] += src[i];
        }

        const auto centre = (int) (pos - from);

        for (int step = 0; step <= n; ++step)
        {
            for (const int i : { centre - step, centre + step })
            {
                if (i < 1 || i >= n)
                    continue;

                const auto a = mono[(size_t) i - 1];
                const auto b = mono[(size_t) i];

                if ((a <= 0.0f && b >= 0.0f) || (a >= 0.0f && b <= 0.0f))
                    return from + i;
            }
        }

        return pos;
    }
}

int SliceExporter::nextIndexIn (const juce::File& destDir, const juce::String& baseName)
{
    if (! destDir.isDirectory())
        return 1;

    const auto stem = juce::File::createLegalFileName (baseName);
    int highest = 0;

    for (const auto& f : destDir.findChildFiles (juce::File::findFiles, false, "*.wav"))
    {
        const auto name = f.getFileNameWithoutExtension();

        if (! name.startsWith (stem))
            continue;

        // What follows the stem is " NN" and possibly a " (2)" the old naming
        // left behind; only the first number counts.
        const auto digits = name.substring (stem.length()).trim()
                                .initialSectionContainingOnly ("0123456789");

        highest = juce::jmax (highest, digits.getIntValue());
    }

    return highest + 1;
}

juce::File SliceExporter::write (juce::AudioFormatManager& formats,
                                 const juce::File& sourceWav,
                                 const juce::File& destDir,
                                 const juce::String& baseName,
                                 double startSeconds,
                                 double endSeconds)
{
    if (endSeconds <= startSeconds)
        return {};

    std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (sourceWav));

    if (reader == nullptr || reader->sampleRate <= 0.0)
        return {};

    const auto rate = reader->sampleRate;
    const auto total = reader->lengthInSamples;
    const auto radius = (juce::int64) (snapSeconds * rate);

    const auto askedFirst = juce::jlimit<juce::int64> (0, total, (juce::int64) (startSeconds * rate));
    const auto askedLast  = juce::jlimit<juce::int64> (0, total, (juce::int64) (endSeconds * rate));

    // Each end may travel up to the radius, so on a slice shorter than twice
    // that they can cross and the cut inverts. Half the slice is the most
    // either end may move, and if the pair still comes out wrong the snap is
    // abandoned rather than the slice: an unsnapped cut clicks, a missing one
    // is a file the person asked for and did not get.
    const auto reach = juce::jmin (radius, juce::jmax<juce::int64> (0, (askedLast - askedFirst) / 2));

    auto first = snapToZeroCrossing (*reader, askedFirst, reach, total);
    auto last  = snapToZeroCrossing (*reader, askedLast,  reach, total);

    if (last <= first)
    {
        first = askedFirst;
        last = askedLast;
    }

    const auto count = last - first;

    if (count <= 0)
        return {};

    if (! destDir.createDirectory())
        return {};

    const auto target = uniqueFileIn (destDir, juce::File::createLegalFileName (baseName));

    auto fileStream = std::make_unique<juce::FileOutputStream> (target);

    if (! fileStream->openedOk())
        return {};

    fileStream->setPosition (0);
    fileStream->truncate();

    const auto channels = juce::jlimit (1, 2, (int) reader->numChannels);

    // The writer takes the stream over on success and leaves it alone on
    // failure, which is why it is handed across as a unique_ptr by reference.
    std::unique_ptr<juce::OutputStream> stream = std::move (fileStream);

    juce::WavAudioFormat wav;
    auto writer = wav.createWriterFor (stream, juce::AudioFormatWriterOptions{}
                                                   .withSampleRate (rate)
                                                   .withNumChannels (channels)
                                                   .withBitsPerSample (24));

    if (writer == nullptr)
        return {};

    const auto fadeSamples = juce::jmin (count / 4, (juce::int64) (fadeOutSeconds * rate));
    const auto fadeFrom = count - fadeSamples;

    juce::AudioBuffer<float> chunk (channels, copyChunkSamples);

    for (juce::int64 done = 0; done < count;)
    {
        const auto n = (int) juce::jmin<juce::int64> (copyChunkSamples, count - done);

        reader->read (&chunk, 0, n, first + done, true, channels > 1);

        // The fade lives in whichever chunk the tail falls in, which for a
        // short slice can be the only one.
        if (fadeSamples > 0 && done + n > fadeFrom)
        {
            const auto rampStart = (int) juce::jmax<juce::int64> (0, fadeFrom - done);
            const auto rampLength = n - rampStart;

            const auto gainAtStart = (float) ((double) (count - (done + rampStart)) / (double) fadeSamples);
            const auto gainAtEnd = (float) ((double) (count - (done + n)) / (double) fadeSamples);

            chunk.applyGainRamp (rampStart, rampLength,
                                 juce::jlimit (0.0f, 1.0f, gainAtStart),
                                 juce::jlimit (0.0f, 1.0f, gainAtEnd));
        }

        if (! writer->writeFromAudioSampleBuffer (chunk, 0, n))
        {
            // Closing the writer finalises a header over a truncated body, and
            // that file reads as a whole one -- it would turn up as a chip and
            // be dragged into a project.
            writer.reset();
            target.deleteFile();
            return {};
        }

        done += n;
    }

    writer->flush();
    writer.reset();

    return target.existsAsFile() ? target : juce::File();
}
