#include "PromptProcessor.h"
#include "PromptEditor.h"

namespace
{
    constexpr auto apiKeyKey = "api_key";
    constexpr auto modelKey = "model";

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

        // The first take is loaded as it lands, so it can be heard while the
        // rest are still being made.
        if (index == 1)
            openAudio (batchDir, take, prompt);
        else
            sendChangeMessage();       // the row has one more in it

        auto text = "Generating " + juce::String (index) + " of " + juce::String (total);

        if (spent > 0)
            text += ", " + juce::String (spent) + " credits so far";

        setBusy (text, (double) index / (double) total);
    };

    maker.onFinished = [this] (bool ok, juce::String message)
    {
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

    prompt = trimmed;
    batchDir = batchFolderFor (config.downloadDir(), trimmed);
    landed = 0;
    spent = 0;

    Generator::Request request;
    request.prompt = trimmed;
    request.apiKey = key;
    request.dir = batchDir;
    request.count = juce::jlimit (1, 5, count);
    request.durationSeconds = durationSeconds;
    request.promptInfluence = promptInfluence;
    request.model = model();
    request.sampleRate = outputRate();

    setBusy ("Starting...", -1.0);
    maker.start (request);
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
    state.setAttribute ("prompt", prompt);
    copyXmlToBinary (state, destData);
}

void PromptProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    const auto state = getXmlFromBinary (data, sizeInBytes);

    if (state == nullptr || ! state->hasTagName ("PROMPTSLICE"))
        return;

    batchDir = juce::File (state->getStringAttribute ("batchDir"));
    prompt = state->getStringAttribute ("prompt");
    loadCommon (*state);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new PromptProcessor();
}
