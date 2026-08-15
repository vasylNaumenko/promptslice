# docs

What the README points at.

`screenshot.webp` is **lossless** WebP rather than PNG: the same pixels, a fifth
of the bytes. A screen capture of a dark interface is mostly flat colour and
small text, which is the case WebP's lossless mode is best at and the case a
lossy format is worst at — the artefacts land on the type. Regenerate it with:

```bash
cwebp -lossless -z 9 shot.png -o screenshot.webp
```

An image in a repository is there for good: it stays in the history after it is
replaced, so it is worth shrinking once, on the way in.
