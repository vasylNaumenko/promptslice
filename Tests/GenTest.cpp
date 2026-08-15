#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_audio_formats/juce_audio_formats.h>

#include "Generator.h"

// Makes one real request to ElevenLabs and checks that what comes back is a wav
// the rest of the tool can open. Skips with a reason when there is no key, the
// way an integration test should -- the key belongs in the environment and
// never in the repository:
//
//   ELEVENLABS_API_KEY=... ./GenTest

namespace
{
    int failures = 0;

    void check (bool ok, const juce::String& what)
    {
        if (! ok)
            ++failures;

        std::cout << (ok ? "  ok    " : "  FAIL  ") << what << std::endl;
    }
}

int main()
{
    const auto key = juce::SystemStats::getEnvironmentVariable ("ELEVENLABS_API_KEY", {}).trim();

    if (key.isEmpty())
    {
        std::cout << "SKIP: no ELEVENLABS_API_KEY in the environment" << std::endl;
        return 0;
    }

    juce::ScopedJuceInitialiser_GUI juceInit;

    const auto dir = juce::File::getSpecialLocation (juce::File::tempDirectory)
                         .getChildFile ("promptslice-gen-test");
    dir.deleteRecursively();

    // Declared before the generator on purpose: the callbacks capture these by
    // reference, and ~Generator runs last, after a request that is still in the
    // air. The other order lets a late callback write into destroyed locals.
    juce::Array<juce::File> takes;
    bool finished = false, ok = false;
    int spent = 0;
    juce::String message;

    // Used further down, declared here for the same reason as the rest: every
    // local a callback captures has to outlive the generator.
    Generator::Balance balance;
    auto answered = false;

    Generator maker;

    maker.onTake = [&] (juce::File take, int, int, int credits)
    {
        takes.add (take);
        spent += credits;
    };

    maker.onFinished = [&] (bool good, juce::String text)
    {
        ok = good;
        message = text;
        finished = true;
    };

    // Asked at a rate that is not the one the wav writer used to be nailed to,
    // so a quality setting that quietly stopped travelling shows up as a file
    // at the wrong rate rather than as nothing at all.
    const auto askedRate = 44100;

    Generator::Request request;
    request.prompt = "short soft ui click";
    request.apiKey = key;
    request.dir = dir;
    request.count = 1;
    request.durationSeconds = 0.5;
    request.promptInfluence = 0.3;
    request.model = Generator::Model::v2;
    request.sampleRate = askedRate;

    std::cout << "one take, half a second" << std::endl;
    maker.start (request);

    // The hi-res counter is a double and does not wrap; the 32-bit one does,
    // roughly every 49 days of uptime, and a deadline computed across the wrap
    // is already in the past.
    const auto deadline = juce::Time::getMillisecondCounterHiRes() + 120000.0;

    while (! finished && juce::Time::getMillisecondCounterHiRes() < deadline)
        juce::MessageManager::getInstance()->runDispatchLoopUntil (50);

    check (finished, "the request finished inside two minutes");
    check (ok, "it succeeded" + juce::String (ok ? "" : ": " + message));
    check (takes.size() == 1, "one take was announced");

    // The service reports the cost per request; without it the plugin would be
    // guessing at a price list, so an absent header has to fail here.
    check (spent > 0, "the take reported what it cost (" + juce::String (spent) + " credits)");
    check (message.contains (juce::String (spent)),
           "and the finished message names it: " + message);

    if (! takes.isEmpty())
    {
        const auto file = takes[0];
        check (file.existsAsFile(), "the file is on disk");

        juce::AudioFormatManager formats;
        formats.registerBasicFormats();
        std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file));

        check (reader != nullptr, "the wav header we wrote is readable");

        if (reader != nullptr)
        {
            // Compared as a whole number: a rate is one, and comparing the
            // double directly is what the float-equal warning is about.
            check ((int) reader->sampleRate == askedRate,
                   "written at the rate that was asked for (" + juce::String (reader->sampleRate) + ")");
            check (reader->numChannels == 2,
                   "stereo (" + juce::String ((int) reader->numChannels) + ")");
            check (reader->bitsPerSample == 16,
                   "16-bit (" + juce::String ((int) reader->bitsPerSample) + ")");

            const auto seconds = (double) reader->lengthInSamples / reader->sampleRate;
            check (seconds > 0.2 && seconds < 1.5,
                   "about the length asked for (" + juce::String (seconds, 3) + " s)");

            juce::AudioBuffer<float> buffer (2, (int) reader->lengthInSamples);
            reader->read (&buffer, 0, (int) reader->lengthInSamples, 0, true, true);

            const auto peak = buffer.getMagnitude (0, buffer.getNumSamples());
            check (peak > 0.001f,
                   "there is sound in it, not silence (peak " + juce::String (peak, 4) + ")");
        }
    }

    dir.deleteRecursively();

    // What a key is allowed to read differs per key, so this checks that the
    // question is answered rather than which answer comes back: a figure, or a
    // reason there is none. Silence is the only failure.
    std::cout << "the balance" << std::endl;

    maker.onBalance = [&] (Generator::Balance b) { balance = std::move (b); answered = true; };
    maker.refreshBalance (key);

    const auto balanceDeadline = juce::Time::getMillisecondCounterHiRes() + 40000.0;

    while (! answered && juce::Time::getMillisecondCounterHiRes() < balanceDeadline)
        juce::MessageManager::getInstance()->runDispatchLoopUntil (50);

    check (answered, "the question was answered");

    using Kind = Generator::Balance::Kind;

    if (balance.kind == Kind::remaining)
        check (balance.limit > 0, "the account says what is left: "
                                      + juce::String (balance.left()) + " of "
                                      + juce::String (balance.limit));
    else if (balance.kind == Kind::spent)
        check (balance.days > 0, "the account is closed to this key, so what it spent: "
                                     + juce::String (balance.used) + " in "
                                     + juce::String (balance.days) + " days");
    else
        check (balance.note.isNotEmpty(), "nothing readable, and it says why: " + balance.note);

    std::cout << (failures == 0 ? "ALL PASS" : juce::String (failures) + " FAILED") << std::endl;
    return failures;
}
