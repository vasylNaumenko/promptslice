# PromptSlice

An audio plugin that turns a written description into sound you can use.

Type what something should sound like, get several takes back from
[ElevenLabs](https://elevenlabs.io), audition them, mark up the one that works,
cut the piece you want and drag it into your project.

macOS, VST3 and AU, plus a standalone build. Written against FL Studio 2026,
though nothing in it is FL-specific.

## What it does

- **Several takes at once.** A prompt gives one sound, so asking for five is
  five requests — and the first plays while the rest are still arriving.
- **The whole editing bit.** Playhead, markers, a selection, looping, cutting.
  Cuts land on the nearest zero crossing within 10 ms and the last millisecond
  fades, so a piece does not click at either end.
- **Lossless.** Takes are requested as PCM rather than the endpoint's default of
  128 kbps mp3, which smears exactly the transients a one-shot is made of.
- **Honest about money.** Every request reports what it cost, and the plugin
  shows it rather than guessing against a price list.

## Why dragging, and not an Import button

There isn't one. VST3 gives a plugin no way to create a channel, place a clip,
or write anything into the host's project — that is not an omission here, it is
the format. What a plugin *can* do is write a file and let the host accept a drop
of it, which is how Serato Sample, XO and Portal all do it too. So every piece
you cut lands in the row at the bottom, and you drag it where you want it.

## Installing

Nothing is signed or notarised, so the only supported route is building it.

You need macOS 11 or newer, the Xcode command line tools, CMake 3.22+, and an
ElevenLabs API key.

```bash
git clone https://github.com/vasylNaumenko/promptslice.git
cd promptslice
cmake -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build
```

JUCE is fetched automatically at the version this was tested against. If you
already have a checkout, point at it and save the download:

```bash
cmake -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo -DJUCE_PATH=/path/to/JUCE
```

The build copies the plugin into `~/Library/Audio/Plug-Ins/` itself. Rescan
plugins in your DAW afterwards.

## Using it

PromptSlice is an **instrument**, not an effect — in FL Studio it goes in the
Channel Rack.

| | |
|---|---|
| Click the waveform | move the playhead, drop the selection |
| Drag the waveform | select a stretch |
| Drag the bar on top of a selection | cut it and drag it straight out |
| Double click | add a marker |
| Right click a marker | remove it |
| Drag a marker | move it, bounded by its neighbours |
| Wheel / ⌘+wheel | scroll / zoom about the pointer |
| Space, `M`, `Esc` | play, marker, drop the selection |

**Cut** slices the stretches between markers — two markers mean one piece.
Markers win over a selection, and the button says which it will do.

### Where files go

Everything lands under the folder in the settings, one subfolder per batch of
takes, named after the prompt and stamped. Change it with **Folder…**, or edit:

```
~/Library/Application Support/PromptSlice/config.json
```

### The API key

It lives in that settings file and nowhere else — never in a project, never in
this repository. Set it with the **Key…** button, or put it in `api_key` there.

### Quality

| | |
|---|---|
| **Model** | `Auto` names no model and lets ElevenLabs pick. `Sound v2` and `Sound v3` ask for one by name. |
| **Quality** | The rate the wav is written at, 22.05 to 48 kHz. |

Neither changes the price: the service charges by the second of audio, so the
rate trades disk space and nothing else.

### Credits

Every request comes back saying what it cost, and that figure — the service's
own, not a guess — is what the status line reports: *"Generated 3 takes for 60
credits."*

Beside it the plugin says what it can about the account, and what that is
depends on what the key is allowed to read:

| The key can read | It shows |
|---|---|
| the account (`user_read`) | `12 340 credits left` — the real balance |
| only its own usage | `1124 credits spent in 30 days` |
| neither | `balance unavailable`, with the reason in the tooltip |

The middle row is what a key scoped to sound generation gets, and it is worded
as what it is: **spent is not left**. To get the real balance, turn on the
`user_read` permission where the key is made.

One thing worth knowing before you spend: a **Length** of `auto` is charged as a
flat rate rather than by the second, and for a short sound that is markedly more
than naming the length. Measured against the live endpoint: half a second asked
for costs 5 credits, two seconds costs 20 — ten a second — while `auto` came
back as one second of audio for 50.

## Tests

```bash
cmake --build build --target SliceTest && ./build/SliceTest_artefacts/*/SliceTest
```

`SliceTest` checks the part that writes files you keep: the zero-crossing snap,
the fade, the numbering and the refusals, against a synthetic tone. It needs
nothing external.

`GenTest` makes one real request and checks the answer opens as a wav at the
rate it asked for, that the cost was reported, and that the balance query gives
either a figure or a reason. It skips when there is no key, so it is safe to run
in any state:

```bash
ELEVENLABS_API_KEY=... ./build/GenTest_artefacts/*/GenTest
```

## Licence

**AGPL-3.0-or-later.** See [LICENSE](LICENSE).

This is not a preference: the plugin links the JUCE framework, which is
[dual-licensed](https://github.com/juce-framework/JUCE/blob/master/LICENSE.md)
under AGPLv3 or a paid commercial licence. A build made without a commercial
JUCE licence is a derivative work and can only be distributed under AGPLv3. In
short — use it for anything you like, but if you distribute a modified version,
its source has to be available under the same terms.

## What it talks to

ElevenLabs, with your key, to ask for sounds and to ask what they cost. Nothing
else leaves the machine: no telemetry, no analytics, no update check.

Sounds you generate are yours to use under
[ElevenLabs' own terms](https://elevenlabs.io/terms), which are between you and
them.
