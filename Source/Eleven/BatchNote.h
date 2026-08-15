#pragma once

#include "Generator.h"

/** What a batch was asked for, written beside the takes it produced.

    A folder of wavs says what came out and nothing about what was wanted: the
    prompt that found a sound is exactly the thing worth having a week later,
    and it lived only in a text field that the next prompt overwrote. So it is
    written down where the sound is, and travels with it — copy the folder and
    the settings go too.

    It is read back to reopen a batch, which is why every field is validated on
    the way in: this file is beside a person's own samples, and they are free to
    edit it or to hand-assemble a folder that has no note at all.
*/
struct BatchNote
{
    juce::String prompt;
    int takes = 3;
    double lengthSeconds = 0;      ///< 0 means the service chose
    double promptInfluence = 0.3;
    Generator::Model model = Generator::Model::automatic;
    int sampleRate = 48000;

    /** What the batch cost, as the service counted it. Zero until it finishes. */
    int credits = 0;
    juce::String created;

    static juce::File fileIn (const juce::File& batchDir);

    /** The best a folder with no note can offer: its own name, with the stamp
        taken off the end. Folders are named "<prompt> <date time>", so the name
        is a lossy copy of the prompt — cut to sixty characters and stripped of
        whatever the file system refused — but the stamp is not part of it and
        has no business in a field somebody may press Generate on.

        Left alone when the tail is not a stamp, because then the name is
        somebody's own and all of it is meant. */
    static juce::String promptFromFolderName (const juce::String& folderName);

    /** Replaces everything with what the note says, defaulting whatever it does
        not mention. False when there is no note to read, which is an ordinary
        answer: folders made before this existed have none. */
    bool readFrom (const juce::File& batchDir);

    void writeTo (const juce::File& batchDir) const;
};
