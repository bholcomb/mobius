Drop vendored archive backends here.

Expected first-cut layout:

- `miniz/miniz.h`
- `miniz/miniz.c`
- `microtar/microtar.h`
- `microtar/microtar.c`

The current module integrates:

- `miniz` for zip reading/writing/extraction
- `microtar` for tar reading/writing/extraction

Combined `tar.gz` flows are built on top of the tar backend plus gzip, which
is written here around miniz's DEFLATE. Nothing is linked from the system.
