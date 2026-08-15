#pragma once

#include <juce_events/juce_events.h>

#include <atomic>
#include <memory>

/** Asks ElevenLabs for a sound and writes what comes back as a wav.

    One prompt gives one take, so several takes are several requests, made one
    after another. A take that lands is announced at once: the point of asking
    for five is to pick one, and the first can be heard while the rest are still
    being made.

    The endpoint returns raw PCM with no header when it is asked for lossless,
    so the header is written here. That is worth the trouble — the default is
    mp3 at 128 kbps, which smears exactly the transients a one-shot is made of.

    Cancelling **abandons** rather than waits. An HTTP request cannot be
    interrupted between chunks, and juce::Thread::stopThread kills by force when
    its wait runs out — its own comment calls that very bad karma. So the work
    runs on a detached thread holding its own state, and cancelling marks that
    state abandoned: the request finishes into a void, writes nothing and
    announces nothing, while the window closes immediately.
*/
class Generator final
{
public:
    /** The two models the endpoint offers, plus the choice of not naming one.

        Auto sends no model_id at all, which is what this plugin did before the
        choice existed — so the default keeps giving what it always gave, and
        picking a model is a decision rather than a side effect of an upgrade. */
    enum class Model { automatic, v2, v3 };

    static juce::String modelId (Model);          ///< empty for automatic
    static Model modelFromId (const juce::String&);

    /** The PCM rates the endpoint will return. All of them cost the same: the
        service charges by the second, so this trades disk space against
        nothing else. */
    static const juce::Array<int>& sampleRates();

    struct Request
    {
        juce::String prompt;
        juce::String apiKey;
        juce::File dir;              ///< where the takes are written
        int count = 1;               ///< how many takes to ask for
        double durationSeconds = 0;  ///< 0 lets the service choose
        double promptInfluence = 0.3;
        Model model = Model::automatic;
        int sampleRate = 48000;
    };

    /** What can be said about the account's credits, which depends on what the
        key is allowed to read.

        Three answers, kept apart because two of them are different facts and
        printing one as the other would be a lie: `remaining` is the account's
        own figure and needs the user_read permission; `spent` is what has gone
        out over a window, which a key scoped to generating sounds may read and
        which is **not** a balance; `unknown` carries the reason instead. */
    struct Balance
    {
        enum class Kind { unknown, remaining, spent };

        Kind kind = Kind::unknown;
        juce::int64 used = 0;        ///< this period for remaining, the window for spent
        juce::int64 limit = 0;       ///< remaining only
        int days = 0;                ///< spent only
        juce::String note;           ///< why there is no figure, when there is none

        juce::int64 left() const { return juce::jmax ((juce::int64) 0, limit - used); }
    };

    Generator();
    ~Generator();

    /** All four arrive on the message thread, and the first three only for the
        batch that is still current: a take from a cancelled batch is dropped
        rather than filed under its successor. */
    std::function<void (juce::String stage, double fraction)> onProgress;
    std::function<void (juce::File take, int index, int total, int credits)> onTake;
    std::function<void (bool ok, juce::String message)> onFinished;

    /** Not tied to a batch: a balance asked for by hand arrives even when
        nothing is being generated. */
    std::function<void (Balance)> onBalance;

    void start (const Request& request);

    /** Returns at once. Whatever is in flight finishes unheard. */
    void cancel();

    bool isBusy() const;

    /** Asks what the account has left, on a thread of its own. Does nothing
        without a key. */
    void refreshBalance (const juce::String& apiKey);

private:
    struct Job;

    static void work (juce::WeakReference<Generator> owner, std::shared_ptr<Job> job);

    /** Blocking. `credits` comes back as the service's own count of what the
        take cost, which is the only figure that is not a guess. */
    static bool requestTake (Job& job, int index, juce::File& written,
                             int& credits, juce::String& error);

    static Balance readBalance (const juce::String& apiKey);

    /** Runs fn on the message thread unless the batch has been abandoned or
        replaced by then. */
    static void post (juce::WeakReference<Generator> owner,
                      std::shared_ptr<Job> job,
                      std::function<void (Generator&)> fn);

    /** The same, for what does not belong to a batch: only the object's own
        life is checked. */
    static void postFree (juce::WeakReference<Generator> owner,
                          std::function<void (Generator&)> fn);

    std::shared_ptr<Job> current;

    JUCE_DECLARE_WEAK_REFERENCEABLE (Generator)
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Generator)
};
