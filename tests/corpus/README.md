# Corpus

`make-corpus.sh` generates `data/` with deterministic, small files; `data/` is not
versioned. Random data and the 4 GiB file are regenerated on every run.

For tests with real-world material (varied formats, already-compressed files),
copy samples from the shared corpus into your own IA_LAB folder — **always copy,
never work in place**:

```sh
find /mnt/OSR_D3/fileFormatSamples/fileFormatSamples/document -type f -size -2M \
     | head -50 | while read -r f; do cp "$f" /mnt/IA_LAB/agentes/arc/corpus/; done
```
