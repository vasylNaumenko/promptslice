#include "Generator.h"

#include <thread>

namespace
{
    constexpr auto endpoint = "https://api.elevenlabs.io/v1/sound-generation";

    /** What the account has spent and what it is allowed. Reading it needs the
        user_read permission, which a key scoped to generation alone lacks. */
    constexpr auto subscriptionEndpoint = "https://api.elevenlabs.io/v1/user/subscription";

    /** What has been spent day by day. A generation-scoped key may read this
        even when it may not read the account, so it is the second thing asked
        rather than the first: it says what went out, never what is left. */
    constexpr auto usageEndpoint = "https://api.elevenlabs.io/v1/usage/character-stats";
    constexpr int usageWindowDays = 30;

    /** The service reports what each request cost in a header, and exposes it
        to browsers by name — so this is its own figure, not arithmetic done
        here against a price list that could go stale. */
    constexpr auto costHeader = "character-cost";

    // What the service sends back for a pcm_* format, verified against the byte
    // count: signed 16-bit little-endian, two channels. Only the rate is
    // chosen; these two are not offered.
    constexpr int pcmChannels = 2;
    constexpr int pcmBits = 16;

    constexpr int requestTimeoutMs = 180000;
    constexpr int balanceTimeoutMs = 15000;
    constexpr int readChunkBytes = 1 << 14;

    /** The endpoint tops out at 22 seconds, which is 4.2 MB at the highest rate
        it offers. Four times that is room for anything legitimate and still
        refuses a gateway's error page before it is all in memory. */
    constexpr size_t maxResponseBytes = 17u * 1024u * 1024u;

    /** An answer about an account is a few hundred bytes. */
    constexpr size_t maxBalanceBytes = 64u * 1024u;

    /** Wraps headerless PCM in the smallest WAV container that describes it. */
    juce::MemoryBlock wrapPcmInWav (const juce::MemoryBlock& pcm, int sampleRate)
    {
        const auto blockAlign = pcmChannels * pcmBits / 8;
        const auto byteRate = sampleRate * blockAlign;
        const auto dataSize = (juce::uint32) pcm.getSize();

        juce::MemoryOutputStream out;

        // The four-character tags are bytes in order, not numbers: writeInt is
        // little-endian, so putting "RIFF" through it as an integer lays it
        // down as "FFIR" and nothing will open the file. Everything after them
        // genuinely is a little-endian number.
        out.write ("RIFF", 4);
        out.writeInt ((int) (36 + dataSize));
        out.write ("WAVE", 4);
        out.write ("fmt ", 4);
        out.writeInt (16);                              // size of the fmt chunk
        out.writeShort (1);                             // PCM
        out.writeShort ((short) pcmChannels);
        out.writeInt (sampleRate);
        out.writeInt (byteRate);
        out.writeShort ((short) blockAlign);
        out.writeShort ((short) pcmBits);
        out.write ("data", 4);
        out.writeInt ((int) dataSize);
        out.write (pcm.getData(), pcm.getSize());

        return out.getMemoryBlock();
    }

    juce::String describeFailure (int status, const juce::String& body)
    {
        if (status == 401 || status == 403)
            return "ElevenLabs refused the key (HTTP " + juce::String (status) + ").";

        if (status == 422)
            return "ElevenLabs did not accept the request (HTTP 422).\n" + body;

        if (status == 429)
            return "Too many requests, or the quota is spent (HTTP 429).";

        if (status == 0)
            return "No answer from ElevenLabs. Check the connection.";

        return "ElevenLabs answered HTTP " + juce::String (status) + ".\n" + body;
    }

    /** A header value may not carry a line ending: the extra-headers string is
        newline-delimited, so a key pasted with a wrap in it would inject
        whatever followed as further headers. */
    juce::String asHeaderValue (const juce::String& raw)
    {
        return raw.removeCharacters ("\r\n").trim();
    }

    /** Reads a stream to its end, or until it is longer than a sane answer.
        Returns false when the ceiling is hit. */
    bool readAll (juce::InputStream& stream, size_t ceiling, juce::MemoryBlock& into,
                  const std::atomic<bool>* abandoned = nullptr)
    {
        juce::HeapBlock<char> chunk (readChunkBytes);

        for (;;)
        {
            if (abandoned != nullptr && abandoned->load())
                return true;

            const auto read = stream.read (chunk.get(), readChunkBytes);

            if (read <= 0)
                return true;

            if (into.getSize() + (size_t) read > ceiling)
                return false;

            into.append (chunk.get(), (size_t) read);
        }
    }

    juce::String asText (const juce::MemoryBlock& block, int limit)
    {
        return juce::String::fromUTF8 ((const char*) block.getData(),
                                       (int) juce::jmin ((size_t) limit, block.getSize()));
    }

    /** GETs and parses. The status comes back for the caller to judge, because
        what a refusal means differs per question. */
    juce::var getJson (const juce::String& address, const juce::String& apiKey, int& status)
    {
        auto options = juce::URL::InputStreamOptions (juce::URL::ParameterHandling::inAddress)
                           .withExtraHeaders ("xi-api-key: " + asHeaderValue (apiKey))
                           .withConnectionTimeoutMs (balanceTimeoutMs)
                           .withStatusCode (&status);

        auto stream = juce::URL (address).createInputStream (options);

        if (stream == nullptr)
            return {};

        juce::MemoryBlock payload;

        if (! readAll (*stream, maxBalanceBytes, payload))
            return {};

        return juce::JSON::parse (asText (payload, (int) maxBalanceBytes));
    }

    /** The usage answer is a set of series keyed by whatever breakdown was
        asked for; with none asked for there is one, called All. Summed over
        every series that is there rather than over that name, so a breakdown
        arriving by default would still add up. Negative when the answer is not
        the shape it should be. */
    juce::int64 sumUsage (const juce::var& stats)
    {
        auto* obj = stats.getDynamicObject();

        if (obj == nullptr)
            return -1;

        const auto usage = obj->getProperty ("usage");
        auto* series = usage.getDynamicObject();

        if (series == nullptr)
            return -1;

        juce::int64 total = 0;
        auto sawSeries = false;

        for (const auto& entry : series->getProperties())
        {
            if (auto* points = entry.value.getArray())
            {
                sawSeries = true;

                for (const auto& point : *points)
                    total += (juce::int64) (double) point;
            }
        }

        return sawSeries ? total : -1;
    }
}

//==============================================================================
juce::String Generator::modelId (Model m)
{
    switch (m)
    {
        case Model::v2: return "eleven_text_to_sound_v2";
        case Model::v3: return "eleven_text_to_sound_v3";
        case Model::automatic: break;
    }

    return {};
}

Generator::Model Generator::modelFromId (const juce::String& id)
{
    if (id == modelId (Model::v2)) return Model::v2;
    if (id == modelId (Model::v3)) return Model::v3;

    return Model::automatic;
}

const juce::Array<int>& Generator::sampleRates()
{
    // The endpoint also offers 8000 and 16000. They are left out on purpose:
    // this is a tool for making one-shots, and telephone bandwidth is not a
    // quality setting anybody here wants offered.
    static const juce::Array<int> rates { 22050, 24000, 32000, 44100, 48000 };
    return rates;
}

//==============================================================================
struct Generator::Job
{
    Request req;

    /** Set when the batch is cancelled or replaced. The worker keeps going —
        there is no way to interrupt it — but stops writing and stops talking. */
    std::atomic<bool> abandoned { false };
    std::atomic<bool> running { true };
};

//==============================================================================
Generator::Generator() = default;

Generator::~Generator()
{
    cancel();
    masterReference.clear();
}

void Generator::start (const Request& request)
{
    cancel();

    auto job = std::make_shared<Job>();
    job->req = request;
    current = job;

    juce::WeakReference<Generator> self (this);

    // Detached on purpose: the object must be free to die while a request is
    // still in the air. The job keeps itself alive through the shared_ptr.
    std::thread ([self, job] { work (self, job); }).detach();
}

void Generator::cancel()
{
    if (current != nullptr)
        current->abandoned = true;

    current.reset();
}

bool Generator::isBusy() const
{
    return current != nullptr && current->running.load();
}

void Generator::post (juce::WeakReference<Generator> owner,
                      std::shared_ptr<Job> job,
                      std::function<void (Generator&)> fn)
{
    if (job->abandoned.load())
        return;

    juce::MessageManager::callAsync ([owner, job, fn]
    {
        // Checked again here, because between posting and running the batch may
        // have been cancelled or a new one started.
        if (owner != nullptr && ! job->abandoned.load() && owner->current == job)
            fn (*owner);
    });
}

void Generator::postFree (juce::WeakReference<Generator> owner,
                          std::function<void (Generator&)> fn)
{
    juce::MessageManager::callAsync ([owner, fn]
    {
        if (owner != nullptr)
            fn (*owner);
    });
}

//==============================================================================
bool Generator::requestTake (Job& job, int index, juce::File& written,
                             int& credits, juce::String& error)
{
    juce::DynamicObject::Ptr body (new juce::DynamicObject());
    body->setProperty ("text", job.req.prompt);

    if (job.req.durationSeconds > 0.0)
        body->setProperty ("duration_seconds", job.req.durationSeconds);

    body->setProperty ("prompt_influence", job.req.promptInfluence);

    // Absent rather than empty when the choice is automatic: the endpoint
    // refuses a model_id it does not recognise, and "" is one of those.
    if (const auto model = modelId (job.req.model); model.isNotEmpty())
        body->setProperty ("model_id", model);

    const auto rate = sampleRates().contains (job.req.sampleRate) ? job.req.sampleRate : 48000;

    const auto url = juce::URL (juce::String (endpoint) + "?output_format=pcm_" + juce::String (rate))
                         .withPOSTData (juce::JSON::toString (juce::var (body.get()), true));

    int status = 0;
    juce::StringPairArray headers;

    // inAddress, not inPostData. JUCE parses "?output_format=..." out of the
    // address into parameters, and inPostData would then send those parameters
    // as the body — overwriting the JSON and earning a 422 that says the body
    // could not be decoded.
    auto options = juce::URL::InputStreamOptions (juce::URL::ParameterHandling::inAddress)
                       .withExtraHeaders ("xi-api-key: " + asHeaderValue (job.req.apiKey)
                                          + "\r\nContent-Type: application/json"
                                          + "\r\nAccept: audio/pcm")
                       .withConnectionTimeoutMs (requestTimeoutMs)
                       .withResponseHeaders (&headers)
                       .withStatusCode (&status);

    auto stream = url.createInputStream (options);

    if (stream == nullptr)
    {
        error = describeFailure (status, {});
        return false;
    }

    // Read in chunks rather than in one gulp: it puts a ceiling on what an
    // unexpected answer can allocate inside the host, and it gives the loop a
    // place to notice that the batch was abandoned.
    juce::MemoryBlock payload;

    if (! readAll (*stream, maxResponseBytes, payload, &job.abandoned))
    {
        error = "The answer from ElevenLabs is far larger than a sound can be. "
                "Something other than the service is replying.";
        return false;
    }

    if (job.abandoned.load())
        return false;

    if (status != 200)
    {
        // The body of a failure is JSON saying what was wrong, and it is the
        // only thing that makes a 422 actionable.
        error = describeFailure (status, asText (payload, 400));
        return false;
    }

    if (payload.getSize() < (size_t) (pcmChannels * pcmBits / 8))
    {
        error = "ElevenLabs returned nothing for take " + juce::String (index) + ".";
        return false;
    }

    if (! job.req.dir.createDirectory())
    {
        error = "Could not create the folder:\n" + job.req.dir.getFullPathName();
        return false;
    }

    const auto file = job.req.dir.getChildFile ("take " + juce::String (index).paddedLeft ('0', 2) + ".wav");
    const auto wav = wrapPcmInWav (payload, rate);

    if (! file.replaceWithData (wav.getData(), wav.getSize()))
    {
        error = "Could not write:\n" + file.getFullPathName();
        return false;
    }

    // Zero when the header is absent, which reads as "not reported" rather than
    // as free: the totals below are only shown when something was counted.
    credits = headers[costHeader].getIntValue();
    written = file;
    return true;
}

Generator::Balance Generator::readBalance (const juce::String& apiKey)
{
    Balance balance;

    int status = 0;
    const auto account = getJson (subscriptionEndpoint, apiKey, status);

    if (status == 200)
    {
        if (auto* obj = account.getDynamicObject(); obj != nullptr && obj->hasProperty ("character_limit"))
        {
            balance.kind = Balance::Kind::remaining;
            balance.used = (juce::int64) obj->getProperty ("character_count");
            balance.limit = (juce::int64) obj->getProperty ("character_limit");
            return balance;
        }

        balance.note = "ElevenLabs answered without a balance in it.";
        return balance;
    }

    if (status == 0)
    {
        balance.note = "Could not reach ElevenLabs to ask.";
        return balance;
    }

    if (status != 401 && status != 403)
    {
        balance.note = describeFailure (status, {});
        return balance;
    }

    // The ordinary case for a key made for generating sounds: it may spend
    // credits and may not read the account. What is left cannot be had, but
    // what has gone out can be — a different fact, and better than a blank.
    const auto nowMs = juce::Time::getCurrentTime().toMilliseconds();
    const auto startMs = nowMs - (juce::int64) usageWindowDays * 24 * 60 * 60 * 1000;

    int usageStatus = 0;
    const auto stats = getJson (juce::String (usageEndpoint)
                                    + "?start_unix=" + juce::String (startMs)
                                    + "&end_unix=" + juce::String (nowMs),
                                apiKey, usageStatus);

    if (usageStatus == 200)
    {
        if (const auto total = sumUsage (stats); total >= 0)
        {
            balance.kind = Balance::Kind::spent;
            balance.used = total;
            balance.days = usageWindowDays;
            balance.note = "This key may not read what the account has left — that needs the "
                           "user_read permission, which is turned on where the key is made. "
                           "What it has spent is shown instead.";
            return balance;
        }
    }

    balance.note = "This key may not read the balance. A key with the user_read "
                   "permission can.";
    return balance;
}

void Generator::refreshBalance (const juce::String& apiKey)
{
    if (apiKey.trim().isEmpty())
        return;

    juce::WeakReference<Generator> self (this);

    std::thread ([self, key = apiKey]
    {
        const auto balance = readBalance (key);

        postFree (self, [balance] (Generator& g)
        {
            if (g.onBalance != nullptr)
                g.onBalance (balance);
        });
    }).detach();
}

//==============================================================================
void Generator::work (juce::WeakReference<Generator> owner, std::shared_ptr<Job> job)
{
    juce::String error;
    int done = 0;
    int spent = 0;
    const auto total = job->req.count;

    for (int i = 1; i <= total; ++i)
    {
        if (job->abandoned.load())
            break;

        post (owner, job, [i, total] (Generator& g)
        {
            if (g.onProgress != nullptr)
                g.onProgress ("Generating " + juce::String (i) + " of " + juce::String (total),
                              (double) (i - 1) / (double) total);
        });

        juce::File take;
        int credits = 0;

        if (! requestTake (*job, i, take, credits, error))
            break;

        ++done;
        spent += credits;

        post (owner, job, [take, i, total, credits] (Generator& g)
        {
            if (g.onTake != nullptr)
                g.onTake (take, i, total, credits);
        });
    }

    // This says the worker has stopped asking, and nothing more. It is **not**
    // safe to gate the handling of a take on it: post only queues, so the last
    // take's message is still on its way to the message thread when this turns
    // false, and a listener that redrew only "while busy" would drop exactly
    // that take. Count what has landed instead.
    job->running = false;

    if (job->abandoned.load())
        return;

    const auto ok = done > 0 && error.isEmpty();
    auto message = ok ? "Generated " + juce::String (done) + (done == 1 ? " take" : " takes")
                      : error;

    if (ok && spent > 0)
        message += " for " + juce::String (spent) + " credits";

    if (ok)
        message += ".";

    post (owner, job, [ok, message] (Generator& g)
    {
        if (g.onFinished != nullptr)
            g.onFinished (ok, message);
    });

    // Asked for here rather than by the window: this thread already holds the
    // key, and the moment a batch ends is exactly when the number has changed.
    if (! job->abandoned.load())
    {
        const auto balance = readBalance (job->req.apiKey);

        postFree (owner, [balance] (Generator& g)
        {
            if (g.onBalance != nullptr)
                g.onBalance (balance);
        });
    }
}
