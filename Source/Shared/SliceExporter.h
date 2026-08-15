#pragma once

#include <juce_audio_formats/juce_audio_formats.h>

/** Cuts a stretch of the working wav into a file of its own.

    Slices are read straight from the wav rather than from anything held in
    memory, so a cut is exact at the sample and costs nothing to keep: the file
    on disk is the only copy, and it is the same file that gets dragged into the
    project.
*/
namespace SliceExporter
{
    /** What a cut is called, and therefore what the row recognises as one when
        it picks a colour. One constant rather than a literal at each end: the
        name is the only thing telling a cut apart from the audio it came out
        of, and two copies of it would disagree in silence — the row would
        simply stop colouring, with nothing to say it had. */
    inline const juce::String cutBaseName { "cut" };

    /** How far either end of a cut may move to land on a zero crossing. A cut
        through the middle of a wave leaves a step, and a step is a click. Ten
        milliseconds is inaudible as a timing change and is more than enough to
        reach a crossing at any pitch a person can hear. */
    constexpr double snapSeconds = 0.010;

    /** Writes [startSeconds, endSeconds) of sourceWav into destDir under a name
        derived from baseName, never overwriting an existing file.

        Both ends are moved to the nearest zero crossing, and the last
        millisecond is faded out: snapping alone fixes the step at a cut, but
        the tail of a slice usually stops mid-sound and needs the ramp as well.
        The start is left sharp on purpose, so a transient stays a transient.

        Returns the file written, or a default File on failure.
    */
    juce::File write (juce::AudioFormatManager& formats,
                      const juce::File& sourceWav,
                      const juce::File& destDir,
                      const juce::String& baseName,
                      double startSeconds,
                      double endSeconds);

    /** The number to give the next slice so a second run of Slice continues the
        sequence instead of colliding with it and being renamed "(2)". */
    int nextIndexIn (const juce::File& destDir, const juce::String& baseName);
}
