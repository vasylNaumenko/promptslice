#include "Config.h"

namespace
{
    constexpr auto keyDownloadDir     = "download_dir";
    constexpr auto keyAudioSampleRate = "audio_sample_rate";

    juce::File defaultDownloadDir (const juce::String& appName)
    {
        return juce::File::getSpecialLocation (juce::File::userMusicDirectory)
                   .getChildFile (appName);
    }
}

Config::Config (const juce::String& appName) : name (appName)
{
    const auto file = configFile();

    if (file.existsAsFile())
    {
        // The parsed var has to outlive the assignment. Written as one
        // expression it does not: the temporary dies at the end of the
        // condition, taking the object with it, and the pointer that survives
        // into the body points at freed memory.
        const auto parsed = juce::JSON::parse (file.loadFileAsString());

        if (auto* obj = parsed.getDynamicObject())
            settings = obj;
    }

    if (settings == nullptr)
        settings = new juce::DynamicObject();

    ensure (keyDownloadDir, defaultDownloadDir (appName).getFullPathName());
    ensure (keyAudioSampleRate, 48000);
}

void Config::ensure (const juce::String& key, const juce::var& defaultValue)
{
    declared.addIfNotAlreadyThere (key);

    // Filling in what the file did not say, so a hand-edited config can carry
    // one key and still be complete.
    if (! settings->hasProperty (key))
    {
        settings->setProperty (key, defaultValue);
        save();
    }
}

void Config::prune()
{
    juce::StringArray dead;

    for (const auto& property : settings->getProperties())
        if (! declared.contains (property.name.toString()))
            dead.add (property.name.toString());

    if (dead.isEmpty())
        return;

    // Gathered first and removed after: removing while walking the same set
    // skips entries, and a key that survived a prune would come back next time
    // looking deliberate.
    for (const auto& key : dead)
        settings->removeProperty (key);

    save();
}

juce::var Config::get (const juce::String& key) const
{
    return settings->getProperty (key);
}

void Config::set (const juce::String& key, const juce::var& value)
{
    settings->setProperty (key, value);
    save();
}

juce::String Config::text (const juce::String& key) const
{
    return get (key).toString().trim();
}

juce::File Config::configFile() const
{
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
               .getChildFile ("Application Support")
               .getChildFile (name)
               .getChildFile ("config.json");
}

void Config::save() const
{
    const auto file = configFile();
    file.getParentDirectory().createDirectory();
    file.replaceWithText (juce::JSON::toString (juce::var (settings.get()), false));
}

juce::File Config::downloadDir() const
{
    const auto path = text (keyDownloadDir);
    return path.isNotEmpty() ? juce::File (path) : defaultDownloadDir (name);
}

void Config::setDownloadDir (const juce::File& dir)
{
    set (keyDownloadDir, dir.getFullPathName());
}

int Config::audioSampleRate() const
{
    return juce::jlimit (8000, 192000, static_cast<int> (get (keyAudioSampleRate)));
}

void Config::setAudioSampleRate (int rate)
{
    set (keyAudioSampleRate, juce::jlimit (8000, 192000, rate));
}
