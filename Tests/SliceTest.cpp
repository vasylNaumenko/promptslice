#include <juce_audio_formats/juce_audio_formats.h>

#include "Config.h"
#include "SliceExporter.h"

// Checks the one piece of code that writes files a person keeps. Runs on a
// synthetic wav so it depends on nothing that was downloaded.

namespace
{
    int failures = 0;

    void check (bool ok, const juce::String& what)
    {
        if (! ok)
            ++failures;

        std::cout << (ok ? "  ok    " : "  FAIL  ") << what << std::endl;
    }

    void checkClose (double value, double expected, double tolerance, const juce::String& what)
    {
        check (std::abs (value - expected) <= tolerance,
               what + " (" + juce::String (value, 6) + " vs " + juce::String (expected, 6)
                   + " +/- " + juce::String (tolerance, 6) + ")");
    }

    constexpr double rate = 48000.0;
    constexpr double toneHz = 440.0;
    constexpr float amplitude = 0.5f;

    juce::File writeTone (const juce::File& dir, double seconds)
    {
        const auto file = dir.getChildFile ("source.wav");
        const auto frames = (int) (seconds * rate);

        juce::AudioBuffer<float> buffer (2, frames);

        for (int i = 0; i < frames; ++i)
        {
            const auto v = amplitude * std::sin (2.0 * juce::MathConstants<double>::pi
                                                     * toneHz * (double) i / rate);
            buffer.setSample (0, i, (float) v);
            buffer.setSample (1, i, (float) v);
        }

        std::unique_ptr<juce::OutputStream> stream = std::make_unique<juce::FileOutputStream> (file);

        juce::WavAudioFormat wav;
        auto writer = wav.createWriterFor (stream, juce::AudioFormatWriterOptions{}
                                                       .withSampleRate (rate)
                                                       .withNumChannels (2)
                                                       .withBitsPerSample (24));
        writer->writeFromAudioSampleBuffer (buffer, 0, frames);
        writer->flush();
        return file;
    }
}

int main()
{
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();

    const auto dir = juce::File::getSpecialLocation (juce::File::tempDirectory)
                         .getChildFile ("ytslice-slice-test");
    dir.deleteRecursively();
    dir.createDirectory();

    const auto source = writeTone (dir, 2.0);
    const auto slices = dir.getChildFile ("slices");

    // A start deliberately a quarter period past a crossing, so the requested
    // sample sits at the peak of the wave and a cut there would be a step.
    const auto quarterPeriod = 0.25 / toneHz;
    const auto askedStart = 0.5 + quarterPeriod;
    const auto askedEnd = 1.2 + quarterPeriod;

    std::cout << "zero-crossing snap" << std::endl;
    const auto cut = SliceExporter::write (formats, source, slices, "tone 01", askedStart, askedEnd);
    check (cut.existsAsFile(), "slice was written");

    if (cut.existsAsFile())
    {
        std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (cut));
        check (reader != nullptr, "slice can be read back");

        if (reader != nullptr)
        {
            const auto frames = (int) reader->lengthInSamples;
            juce::AudioBuffer<float> buffer (2, frames);
            reader->read (&buffer, 0, frames, 0, true, true);

            checkClose ((double) frames / rate, askedEnd - askedStart,
                        2.0 * SliceExporter::snapSeconds, "length is what was asked for");

            // One sample step of a 440 Hz wave is 0.058 rad, so a crossing can
            // be no further from zero than that times the amplitude.
            check (std::abs (buffer.getSample (0, 0)) < 0.05f,
                   "starts on a zero crossing, not mid-wave (first sample "
                       + juce::String (buffer.getSample (0, 0), 4) + ")");

            check (std::abs (buffer.getSample (0, frames - 1)) < 0.01f,
                   "ends silent after the fade (last sample "
                       + juce::String (buffer.getSample (0, frames - 1), 4) + ")");

            // The fade must not eat the slice: a millisecond out of seven
            // hundred is nothing. Measured as a peak over the middle third,
            // because any one sample of a sine can sit at zero by chance.
            const auto third = juce::jmax (1, frames / 3);
            const auto peak = buffer.findMinMax (0, third, third).getLength();

            checkClose (peak, 2.0f * amplitude, 0.02f, "the body of the slice is untouched");
        }
    }

    // A slice shorter than twice the snap window: each end used to be free to
    // travel the full radius, so they crossed and the write was abandoned.
    std::cout << "slices shorter than the snap window" << std::endl;

    for (const double span : { 0.004, 0.010, 0.019 })
    {
        const auto shortCut = SliceExporter::write (formats, source, slices, "short",
                                                    askedStart, askedStart + span);
        check (shortCut.existsAsFile(), "a " + juce::String (span * 1000.0, 0) + " ms slice is written");

        if (shortCut.existsAsFile())
        {
            std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (shortCut));

            if (reader != nullptr)
                checkClose ((double) reader->lengthInSamples / rate, span,
                            2.0 * SliceExporter::snapSeconds,
                            "  and it is about the length asked for");
        }

        shortCut.deleteFile();
    }

    std::cout << "numbering" << std::endl;
    check (SliceExporter::nextIndexIn (slices, "tone") == 2,
           "continues after the slice already there");

    SliceExporter::write (formats, source, slices, "tone 02", 0.1, 0.2);
    SliceExporter::write (formats, source, slices, "tone 03", 0.3, 0.4);
    check (SliceExporter::nextIndexIn (slices, "tone") == 4, "continues after three");
    check (SliceExporter::nextIndexIn (slices, "other") == 1, "an unused name starts at one");

    check (! slices.getChildFile ("tone 01 (2).wav").existsAsFile(),
           "no collision-renamed duplicates");

    std::cout << "refusals" << std::endl;
    check (SliceExporter::write (formats, source, slices, "bad", 1.0, 1.0) == juce::File(),
           "an empty range writes nothing");
    check (SliceExporter::write (formats, source, slices, "bad", 1.5, 0.5) == juce::File(),
           "a reversed range writes nothing");

    std::cout << "naming" << std::endl;
    check (SliceExporter::isNamed (slices.getChildFile ("tone 01.wav"), "tone"),
           "a file written under a name is recognised by it");
    check (SliceExporter::isNamed (slices.getChildFile ("tone.wav"), "tone"),
           "and so is the bare name");
    check (! SliceExporter::isNamed (slices.getChildFile ("tonearm loop.wav"), "tone"),
           "a name that merely starts the same is not");

    std::cout << "order" << std::endl;
    {
        const auto ordered = dir.getChildFile ("ordered");
        ordered.createDirectory();

        // Written oldest first with the alphabet against it: "take" sorts after
        // "cut", so a listing that came back in this order can only have used
        // the clock. A whole second apart because that is the resolution the
        // file system stamps a birth time at.
        SliceExporter::write (formats, source, ordered, "take 01", 0.0, 0.1);
        juce::Thread::sleep (1100);
        SliceExporter::write (formats, source, ordered, "cut 01", 0.2, 0.3);

        const auto files = SliceExporter::wavsInOrder (ordered);

        check (files.size() == 2, "both are listed");
        check (files.size() == 2
                   && files[0].getFileNameWithoutExtension() == "take 01"
                   && files[1].getFileNameWithoutExtension() == "cut 01",
               "oldest first, so the newer one is at the end even though it sorts first");

        check (SliceExporter::wavsInOrder (dir.getChildFile ("nowhere")).isEmpty(),
               "a folder that is not there lists nothing");
    }

    std::cout << "settings" << std::endl;
    {
        // Its own name under Application Support, so this cannot touch the
        // settings of an installed plugin.
        const juce::String appName { "SliceKitSelfTest" };

        {
            Config first (appName);
            first.set ("stale", "left over from a version that read it");
            first.set ("download_dir", "/tmp/slicekit-test");
        }

        Config second (appName);
        check (second.text ("stale").isNotEmpty(), "an undeclared key survives until pruned");

        second.ensure ("declared", 7);
        second.prune();

        check (second.text ("stale").isEmpty(), "prune drops what was never declared");
        check ((int) second.get ("declared") == 7, "and keeps what was");
        check (second.downloadDir().getFullPathName() == "/tmp/slicekit-test",
               "including the keys the base class declared for itself");

        // Read back from disk rather than from memory: prune has to have
        // written, or the keys come back on the next launch.
        Config reread (appName);
        check (reread.text ("stale").isEmpty(), "and the file on disk agrees");

        second.configFile().getParentDirectory().deleteRecursively();
    }

    dir.deleteRecursively();

    std::cout << (failures == 0 ? "ALL PASS" : juce::String (failures) + " FAILED") << std::endl;
    return failures;
}
