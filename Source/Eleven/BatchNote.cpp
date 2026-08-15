#include "BatchNote.h"

namespace
{
    /** Not a dotfile: it sits among a person's own samples, and something they
        can see is something they can read and edit. */
    constexpr auto noteName = "promptslice.cfg";

    /** Matched against a shape rather than parsed: a zero stands for any digit
        and everything else has to be itself. What is wanted is "does this tail
        look like something the plugin appended", not what date it was. */
    bool looksLikeStamp (const juce::String& text)
    {
        static const juce::String shape { "0000-00-00 000000" };

        if (text.length() != shape.length())
            return false;

        for (int i = 0; i < shape.length(); ++i)
            if (shape[i] == '0' ? ! juce::CharacterFunctions::isDigit (text[i])
                                : text[i] != shape[i])
                return false;

        return true;
    }
}

juce::String BatchNote::promptFromFolderName (const juce::String& folderName)
{
    // One for the space that joins them.
    const auto stampLength = juce::String ("0000-00-00 000000").length() + 1;

    if (folderName.length() > stampLength)
    {
        const auto tail = folderName.getLastCharacters (stampLength);

        if (tail.startsWithChar (' ') && looksLikeStamp (tail.substring (1)))
            return folderName.dropLastCharacters (stampLength).trim();
    }

    return folderName;
}

juce::File BatchNote::fileIn (const juce::File& batchDir)
{
    return batchDir.getChildFile (noteName);
}

bool BatchNote::readFrom (const juce::File& batchDir)
{
    const auto file = fileIn (batchDir);

    if (! file.existsAsFile())
        return false;

    // The parsed var has to outlive the pointer taken out of it: written as one
    // expression the temporary dies at the end of the condition and the pointer
    // that survives points at freed memory.
    const auto parsed = juce::JSON::parse (file.loadFileAsString());
    auto* obj = parsed.getDynamicObject();

    if (obj == nullptr)
        return false;

    // Every field starts from the default rather than from whatever this note
    // happened to be holding, so a half-written file cannot leave a mix of two
    // batches behind.
    *this = BatchNote {};

    if (obj->hasProperty ("prompt"))
        prompt = obj->getProperty ("prompt").toString();

    if (obj->hasProperty ("takes"))
        takes = juce::jlimit (1, 5, (int) obj->getProperty ("takes"));

    if (obj->hasProperty ("length_seconds"))
        lengthSeconds = juce::jlimit (0.0, 22.0, (double) obj->getProperty ("length_seconds"));

    if (obj->hasProperty ("prompt_influence"))
        promptInfluence = juce::jlimit (0.0, 1.0, (double) obj->getProperty ("prompt_influence"));

    if (obj->hasProperty ("model"))
        model = Generator::modelFromId (obj->getProperty ("model").toString());

    // Checked against the list rather than clamped: a rate between two the
    // endpoint offers is not a rate, and asking for one is a refusal.
    if (obj->hasProperty ("sample_rate"))
        if (const auto rate = (int) obj->getProperty ("sample_rate");
            Generator::sampleRates().contains (rate))
            sampleRate = rate;

    if (obj->hasProperty ("credits"))
        credits = juce::jmax (0, (int) obj->getProperty ("credits"));

    return true;
}

Generator::Request BatchNote::asRequest() const
{
    Generator::Request request;
    request.prompt = prompt;
    request.count = takes;
    request.durationSeconds = lengthSeconds;
    request.promptInfluence = promptInfluence;
    request.model = model;
    request.sampleRate = sampleRate;
    return request;
}

bool BatchNote::writeTo (const juce::File& batchDir) const
{
    if (! batchDir.isDirectory())
        return false;

    juce::DynamicObject::Ptr obj (new juce::DynamicObject());

    obj->setProperty ("prompt", prompt);
    obj->setProperty ("takes", takes);
    obj->setProperty ("length_seconds", lengthSeconds);
    obj->setProperty ("prompt_influence", promptInfluence);

    // The empty string is what "let the service choose" travels as, here and on
    // the wire, so a note written under Auto reopens as Auto.
    obj->setProperty ("model", Generator::modelId (model));
    obj->setProperty ("sample_rate", sampleRate);
    obj->setProperty ("credits", credits);

    // Multi-line: this is a file a person is meant to be able to read.
    return fileIn (batchDir).replaceWithText (juce::JSON::toString (juce::var (obj.get()), false));
}
