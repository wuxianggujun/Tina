# Audio decoder fixtures

Original generated 100 ms stereo sine waves (48 kHz, left 440 Hz / right 660 Hz).
The audio samples are dedicated to the public domain under CC0-1.0; no external music is used.
`manifest.json` records encoder, source metadata and SHA-256 of every committed binary.

Tests need no encoder, network, sound card or external media. To regenerate deliberately:

```text
python generate.py --ffmpeg <path-to-ffmpeg-with-libvorbis-libopus-libmp3lame>
```

The WAV/FLAC cases are lossless; the Vorbis/Opus/MP3 cases assert decoded duration, channel/rate,
finite non-silent PCM and bounded output rather than bit-identical lossy samples.
