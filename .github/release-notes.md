Download the zip for your system, below.

**macOS** — VST3, AU and a standalone app. Put them here:

```
PromptSlice.vst3       →  ~/Library/Audio/Plug-Ins/VST3/
PromptSlice.component  →  ~/Library/Audio/Plug-Ins/Components/
PromptSlice.app        →  anywhere you like
```

**Windows** — VST3 and a standalone. Put the VST3 folder in
`C:\Program Files\Common Files\VST3\` and keep the standalone wherever suits.

Then rescan plugins in your DAW. PromptSlice is an **instrument**, not an
effect — in FL Studio it goes in the Channel Rack.

---

### macOS will refuse to open it, and here is why

These builds are **not signed and not notarised** — that needs a paid Apple
developer account. macOS therefore marks anything downloaded from the internet
as quarantined, and refuses to load it with a message about unidentified
developers or damage.

Clear the mark on what you installed:

```bash
xattr -dr com.apple.quarantine ~/Library/Audio/Plug-Ins/VST3/PromptSlice.vst3
xattr -dr com.apple.quarantine ~/Library/Audio/Plug-Ins/Components/PromptSlice.component
```

That is you telling macOS you trust this, which is a decision worth making
knowingly: the honest alternative is to build it yourself from the source in
this repository, which is what the README describes.

Windows shows its own warning on the standalone for the same reason — unsigned
code — and the VST3 loads without complaint.

### You need an ElevenLabs key of your own

The plugin generates sound through [ElevenLabs](https://elevenlabs.io) and
spends **your** credits doing it, not anybody else's. The free tier gives 10 000
credits a month, which is about a thousand seconds of asked-for length.

Make one at
[elevenlabs.io/app/developers/api-keys](https://elevenlabs.io/app/developers/api-keys),
press **Key…** in the plugin and paste it. It is kept in the plugin's own
settings file and never written into a project.

Two accesses are worth knowing about, because the page offers a long list:

- **Sound generation** — required. Without it nothing works at all.
- **User Access → Read** — optional, and only so the plugin can show what the
  account has left. Without it it shows what the key has spent instead, and
  says which one it is showing.

Everything else can stay off — **History**, Models, Voices, Text to Speech.
History is the natural wrong guess: it is your library of past generations on
the site, and the plugin never opens it.
