Drop vendored archive backends here.

Expected first-cut layout:

- `miniz/miniz.h`
- `miniz/miniz.c`
- `microtar/microtar.h`
- `microtar/microtar.c`

The current module integrates:

- `miniz` for zip reading/writing/extraction
- `microtar` for tar reading/writing/extraction

Combined `tar.gz` and `tar.zst` flows are built on top of the tar backend plus
the native gzip/zstd stream support.
