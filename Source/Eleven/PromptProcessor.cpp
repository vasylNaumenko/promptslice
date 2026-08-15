#include "PromptProcessor.h"
#include "../Shared/SliceStrip.h"
#include "PromptEditor.h"

namespace
{
    constexpr auto apiKeyKey = "api_key";
    constexpr auto modelKey = "model";

    /** The last prompt that was actually generated from, kept in the plugin's
        own settings as well as in the batch's note. The note travels with the
        sounds and answers "what made these"; this answers "what was I working
        on", which is the question a freshly opened window asks — a new instance
        has no batch to read a note from and would otherwise start blank every
        time. */
    constexpr auto lastPromptKey = "last_prompt";

    /** A folder per batch, named after the prompt and stamped, so asking the
        same thing twice does not overwrite the first answer. */
    juce::File batchFolderFor (const juce::File& root, const juce::String& prompt)
    {
        auto legal = juce::File::createLegalFileName (prompt).trim();

        if (legal.isEmpty())
            legal = "prompt";

        const auto stamp = juce::Time::getCurrentTime().formatted ("%Y-%m-%d %H%M%S");
        return root.getChildFile (legal.substring (0, 60).trim() + " " + stamp);
    }
}

PromptProcessor::PromptProcessor() : SliceProcessor ("PromptSlice")
{
    config.ensure (apiKeyKey, "");
    config.ensure (modelKey, "");
    config.ensure (lastPromptKey, "");

    // Where a window with no batch behind it starts from. A project that
    // carries its own batch overwrites this in setStateInformation.
    note.prompt = config.text (lastPromptKey);

    // The generator answers to the processor rather than to the window, so
    // closing the editor mid-batch does not throw the takes away.
    maker.onProgress = [this] (juce::String stage, double fraction)
    {
        setBusy (stage, fraction);
    };

    maker.onTake = [this] (juce::File take, int index, int total, int credits)
    {
        // Counted before anything is announced: a window redraws its row from
        // this number, and a change message carrying a stale count would show
        // one take fewer than there are.
        ++landed;
        spent += credits;

        // Written on the first take rather than when the batch was asked for,
        // because the folder does not exist until then — and a batch that
        // produced nothing should leave nothing behind to reopen.
        if (index == 1)
        {
            note.writeTo (batchDir);
            openAudio (batchDir, take, note.prompt);   // heard while the rest arrive
        }
        else
        {
            sendChangeMessage();                       // the row has one more in it
        }

        auto text = "Generating " + juce::String (index) + " of " + juce::String (total);

        if (spent > 0)
            text += ", " + juce::String (spent) + " credits so far";

        setBusy (text, (double) index / (double) total);
    };

    maker.onFinished = [this] (bool ok, juce::String message)
    {
        // Rewritten with what it ended up costing, which is only known now.
        if (ok)
        {
            note.credits = spent;
            note.writeTo (batchDir);
        }

        setStatus (message, ! ok);
    };

    maker.onBalance = [this] (Generator::Balance b)
    {
        accountBalance = std::move (b);
        sendChangeMessage();
    };
}

juce::String PromptProcessor::apiKey() const
{
    return config.text (apiKeyKey);
}

void PromptProcessor::setApiKey (const juce::String& key)
{
    // Control characters are stripped where the key is stored rather than
    // where it is used: a key pasted from a wrapped line would otherwise carry
    // a newline into an HTTP header, and everything after it would be read by
    // the server as further headers.
    config.set (apiKeyKey, key.removeCharacters ("\r\n\t").trim());
}

Generator::Model PromptProcessor::model() const
{
    return Generator::modelFromId (config.text (modelKey));
}

void PromptProcessor::setModel (Generator::Model m)
{
    config.set (modelKey, Generator::modelId (m));
}

int PromptProcessor::outputRate() const
{
    const auto rate = config.audioSampleRate();
    return Generator::sampleRates().contains (rate) ? rate : 48000;
}

void PromptProcessor::setOutputRate (int rate)
{
    config.setAudioSampleRate (rate);
}

void PromptProcessor::refreshBalance()
{
    maker.refreshBalance (apiKey());
}

void PromptProcessor::generate (const juce::String& newPrompt, int count,
                                double durationSeconds, double promptInfluence)
{
    const auto trimmed = newPrompt.trim();

    if (trimmed.isEmpty())
        return setStatus ("Type what the sound should be.", true);

    const auto key = apiKey();

    if (key.isEmpty())
        return setStatus ("No API key yet. Press Key... and paste one.", true);

    batchDir = batchFolderFor (config.downloadDir(), trimmed);
    landed = 0;
    spent = 0;

    note = {};
    note.prompt = trimmed;
    note.takes = juce::jlimit (1, 5, count);
    note.lengthSeconds = durationSeconds;
    note.promptInfluence = promptInfluence;
    note.model = model();
    note.sampleRate = outputRate();
    note.created = juce::Time::getCurrentTime().formatted ("%Y-%m-%d %H:%M:%S");

    // Remembered on the way out rather than as the field is typed in: what is
    // worth reopening with is what was asked for, not every draft that was
    // half-written and thought better of.
    config.set (lastPromptKey, note.prompt);

    Generator::Request request;
    request.prompt = note.prompt;
    request.apiKey = key;
    request.dir = batchDir;
    request.count = note.takes;
    request.durationSeconds = note.lengthSeconds;
    request.promptInfluence = note.promptInfluence;
    request.model = note.model;
    request.sampleRate = note.sampleRate;

    setBusy ("Starting...", -1.0);
    maker.start (request);
}

void PromptProcessor::loadBatch (const juce::File& dir)
{
    const auto wavs = SliceStrip::wavsInOrder (dir);

    if (wavs.isEmpty())
        return setStatus ("No wavs in " + dir.getFileName(), true);

    // Whatever is in the air belongs to the batch being left, and its takes
    // would land in a folder nobody is looking at any more.
    maker.cancel();

    batchDir = dir;
    landed = wavs.size();
    spent = 0;

    if (note.readFrom (dir))
    {
        // The settings that made it become the settings in force, so pressing
        // Generate again asks for more of the same rather than for whatever was
        // last set by hand.
        setModel (note.model);
        setOutputRate (note.sampleRate);
    }
    else
    {
        // A folder from before notes existed, or one somebody put together
        // themselves. The folder's own name is the closest thing to a prompt it
        // has, and it is better than an empty field.
        note = {};
        note.prompt = BatchNote::promptFromFolderName (dir.getFileName());
    }

    // Opening a batch makes its prompt the one being worked on, so a window
    // opened later starts where this left off.
    config.set (lastPromptKey, note.prompt);

    openAudio (batchDir, wavs.getFirst(), note.prompt);

    auto text = "Opened " + juce::String (wavs.size())
                    + (wavs.size() == 1 ? " file" : " files");

    if (note.credits > 0)
        text += ", made for " + juce::String (note.credits) + " credits";

    setStatus (text, false);
}

void PromptProcessor::newBatch()
{
    maker.cancel();

    batchDir = juce::File();
    landed = 0;
    spent = 0;

    // The prompt and what the last batch cost go; the settings stay, because
    // they are what the next one will be made with. Cleared in the settings
    // file as well, or the next window would bring back exactly the prompt this
    // button was pressed to be rid of. Nothing is lost by it: the note beside
    // the takes still has it, and Library opens that.
    note.prompt.clear();
    note.credits = 0;
    note.created.clear();
    config.set (lastPromptKey, "");

    closeAudio();       // clears the waveform, the markers and the selection
    setStatus ("Type what the sound should be.", false);
}

void PromptProcessor::cancelGeneration()
{
    maker.cancel();

    auto text = juce::String ("Stopped.");

    if (spent > 0)
        text += " " + juce::String (spent) + " credits were spent.";

    setStatus (text, false);
}

juce::AudioProcessorEditor* PromptProcessor::createEditor()
{
    return new PromptEditor (*this);
}

void PromptProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    juce::XmlElement state ("PROMPTSLICE");
    saveCommon (state);
    state.setAttribute ("batchDir", batchDir.getFullPathName());
    state.setAttribute ("prompt", note.prompt);
    copyXmlToBinary (state, destData);
}

void PromptProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    const auto state = getXmlFromBinary (data, sizeInBytes);

    if (state == nullptr || ! state->hasTagName ("PROMPTSLICE"))
        return;

    batchDir = juce::File (state->getStringAttribute ("batchDir"));

    // The note beside the takes is the fuller record, so the project's own copy
    // of the prompt is only the fallback: a batch made before notes existed,
    // or one whose folder has since been moved away.
    if (! note.readFrom (batchDir))
        note.prompt = state->getStringAttribute ("prompt");

    loadCommon (*state);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new PromptProcessor();
}
