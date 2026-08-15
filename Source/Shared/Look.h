#pragma once

#include <juce_graphics/juce_graphics.h>

/** The colours and the clock, in one place.

    Both lived twice before, once per view, and the two copies had drifted: the
    same name "textColour" meant a bright foreground in one file and a muted one
    in the other, so editing either was a coin toss.
*/
namespace Look
{
    inline const juce::Colour panel     { 0xff14161a };   ///< window background
    inline const juce::Colour sunken    { 0xff0a0c0f };   ///< video letterbox
    inline const juce::Colour waveBack  { 0xff101317 };   ///< behind the peaks
    inline const juce::Colour grid      { 0xff20262e };   ///< ruler bed, zero line
    inline const juce::Colour text      { 0xffe6e8ea };   ///< foreground
    inline const juce::Colour dim       { 0xff8a9099 };   ///< secondary text
    inline const juce::Colour error     { 0xffff6b6b };
    inline const juce::Colour wave      { 0xff4da3ff };   ///< the peaks
    inline const juce::Colour region    { 0x334da3ff };   ///< the selection
    inline const juce::Colour playhead  { 0xffff4d4d };

    /** Markers and the bands between them are deliberately one colour: the band
        is the piece that gets cut and the markers are its edges. */
    inline const juce::Colour marker    { 0xffffb03a };

    /** A chip is coloured by how its file was made, never by where it sits in
        the row: a colour that followed the position would change under every
        neighbour that got deleted, which is the thing the row must not do. Blue
        arrived whole — a take — and purple was cut out of one. */
    inline const juce::Colour chipSource { 0xff16273f };
    inline const juce::Colour chipCut    { 0xff2b1c40 };
    inline const juce::Colour chipEdge   { 0xff39414d };

    /** How much lighter a chip goes while it is being dragged. A number rather
        than a second pair of colours: the two pairs would have to be kept the
        same distance apart by eye, with nothing written down saying what that
        distance was meant to be. */
    inline constexpr float chipHeldLift = 0.35f;

    /** m:ss.mmm. Guards against NaN, which a transport with no file returns. */
    juce::String formatTime (double seconds);
}
