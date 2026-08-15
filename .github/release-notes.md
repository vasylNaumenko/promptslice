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

### You need an ElevenLabs key

The plugin generates sound through [ElevenLabs](https://elevenlabs.io) and
spends your credits doing it. Press **Key…** and paste one. It is kept in the
plugin's own settings file and never written into a project.
