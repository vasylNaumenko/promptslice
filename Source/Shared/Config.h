#pragma once

#include <juce_core/juce_core.h>

/** Settings read from a JSON file when a plugin instance is created.

    The download folder is the one setting every plugin here hangs on:
    everything a run produces lands in a subfolder of it, so a project never
    keeps media of its own.

    Anything beyond that belongs to whichever plugin needs it, and is declared
    by that plugin with ensure(). A settings file should hold the keys its owner
    actually reads — three about downloading video in the generator's file
    would read as a promise the plugin does not keep.

    Each plugin instance holds its own copy. Two instances that both change a
    setting will overwrite each other, which is the honest behaviour for a file
    a person is also free to edit by hand.

    Not thread safe, and it does not need to be: work that leaves the message
    thread takes a copy of what it needs with it.
*/
class Config
{
public:
    /** appName picks the folder under Application Support, so each plugin keeps
        its own settings. */
    explicit Config (const juce::String& appName);

    /** Declares a key this plugin uses, writing the default if the file does
        not already carry it. */
    void ensure (const juce::String& key, const juce::var& defaultValue);

    juce::var get (const juce::String& key) const;
    void set (const juce::String& key, const juce::var& value);

    /** Convenience for the common case: a trimmed string, empty when unset. */
    juce::String text (const juce::String& key) const;

    juce::File downloadDir() const;
    void setDownloadDir (const juce::File& dir);

    /** The rate working wavs are written at. Slices are exported from them, so
        this is the rate they come out at too — which is why both plugins mean
        the same thing by it, one converting to it and the other asking for it. */
    int audioSampleRate() const;
    void setAudioSampleRate (int rate);

    juce::File configFile() const;

    void save() const;

private:
    juce::DynamicObject::Ptr settings;
    juce::String name;
};
