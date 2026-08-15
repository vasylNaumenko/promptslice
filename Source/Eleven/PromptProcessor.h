#pragma once

#include "../Shared/SliceProcessor.h"
#include "Generator.h"

/** PromptSlice: type what the sound should be, get several takes, keep the one
    that works.

    Everything after a wav exists is SliceProcessor's — playing, markers, the
    selection, cutting. What is here is the asking.
*/
class PromptProcessor final : public SliceProcessor
{
public:
    PromptProcessor();

    void generate (const juce::String& prompt, int count,
                   double durationSeconds, double promptInfluence);

    bool isGenerating() const { return maker.isBusy(); }

    /** The key lives in this plugin's settings file and nowhere else — never in
        a project, never in the repository. */
    juce::String apiKey() const;
    void setApiKey (const juce::String& key);
    void cancelGeneration();

    /** Which model answers, and at what rate the wav is written. Both are
        settled once rather than chosen per prompt, so they live in the settings
        file beside the key. */
    Generator::Model model() const;
    void setModel (Generator::Model);
    int outputRate() const;
    void setOutputRate (int rate);

    /** How many takes of the current batch have arrived. A count rather than a
        "still generating" flag: the last take is announced and the batch ends
        in the same breath, so anything watching a flag misses it. */
    int takesLanded() const { return landed; }

    /** What the batch has cost so far, as the service counted it. */
    int creditsSpent() const { return spent; }

    /** What the account has left, when the key is allowed to ask. */
    const Generator::Balance& balance() const { return accountBalance; }
    void refreshBalance();

    /** The folder the current batch of takes is in. Cut pieces go in beside
        them, so one row shows everything a person can drag out. */
    juce::File takesDir() const { return batchDir; }
    juce::String lastPrompt() const { return prompt; }

    const juce::String getName() const override { return "PromptSlice"; }
    juce::AudioProcessorEditor* createEditor() override;

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

private:
    Generator maker;
    juce::File batchDir;
    juce::String prompt;

    int landed = 0;
    int spent = 0;
    Generator::Balance accountBalance;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PromptProcessor)
};
