# Mobius Roadmap

Proposed additions that have not landed yet. API names and examples are
proposals, not existing interfaces or a finished specification, and there are
no timeline commitments. What has landed is documented with its module: the
`cli`, `io` and `process` modules, fiber-aware stream and process waits, and
standalone bundles (`tools/bundle.mob`).

| Priority | Addition | Intended home |
|---|---|---|
| Next | File and directory watching | Existing `os` module |
| Next | CSV reading and writing | Bundled `csv` module |
| Later | Bundles that read their scripts and assets in place | Bundle launcher |
| Deferred | HTTPS, WSS and TLS transport | Existing networking modules; backend undecided |
| Low priority | Typed arrays and native data operations | Optional `data` package |

Keep the core interpreter small, use the existing module and package system,
and design native functionality for Linux, macOS and Windows.

## File watching in os

Extend `os` with file and directory watching, filtering and debouncing.
Deliver events through a channel, so scripts can rebuild templates, regenerate
code or reload assets.

Define recursive watching, rename events, atomic file replacement, overflow
notification and watcher shutdown. Normalise useful behaviour across the OS
backends (inotify, FSEvents, ReadDirectoryChangesW) without promising identical
low-level event sequences. Waiting for events should suspend only the waiting
fiber, as `io` and `process` waits already do.

## CSV module

Ship CSV as a general-purpose bundled module, independent of the optional
`data` package. Support incremental reading and writing through `io` streams,
quoting, embedded delimiters and newlines, configurable delimiters, headers,
and record and field locations in errors.

Keep fields as strings by default, with explicit conversion options. Define
how duplicate headers and rows of the wrong length are handled. A future `data`
adapter can read CSV in typed batches without making CSV depend on it.

## Bundles without extraction

A bundle extracts itself to a per-bundle cache directory and runs from there.
Reading scripts and assets directly from the embedded archive would remove the
extraction step and the cache. Native plugins still need to be on disk to be
loaded, so this applies to scripts and assets.

## TLS (deferred)

An open architectural decision. The existing HTTP and WebSocket code already
handles the protocols; adding HTTPS doesn't require replacing it.

The alternatives considered were a shared TLS stream under the existing
protocols, or libcurl-backed HTTP client requests. Libcurl would not provide
TLS listeners for the `web` server. If it were chosen, link it statically into
the native module; Windows could use Schannel, while Linux would need a chosen
TLS backend. Avoid adding distributed shared libraries.

Settle certificate trust, hostname verification, client and server
integration, shutdown and timeouts when this work resumes. Neither libcurl nor
OpenSSL is a settled dependency.

## Data package (low priority)

Specify this separately. The direction is an optional package with native bulk
operations over typed buffers and a convenient Mobius API. OpenBLAS (numerical
operations) and nanoarrow (columnar data interchange) are candidates, not
chosen dependencies.

Keep buffer ownership, view lifetimes and resizing rules explicit. Avoid
copying where layouts allow it, without promising zero-copy everywhere. CSV
stays useful on its own in the bundled library.
