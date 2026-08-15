#include "Look.h"

juce::String Look::formatTime (double seconds)
{
    if (std::isnan (seconds) || seconds < 0.0)
        seconds = 0.0;

    const auto total = (int) seconds;

    return juce::String::formatted ("%d:%02d.%03d",
                                    total / 60, total % 60,
                                    (int) ((seconds - total) * 1000.0));
}
