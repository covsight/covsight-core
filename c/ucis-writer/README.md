# ucis_writer

Write schema-valid **UCIS-XML** coverage databases through the standard
**UCIS 1.0 write-streaming API**, from one vendorable C file with no
dependencies.

Built for tools that produce coverage — simulators, emulators, testbenches,
format converters — and want to export it somewhere other tools can read,
without adopting a coverage database.

## Why you might want it

- **One file.** Copy `include/ucis_writer.h` into your tree. No library, no
  build system changes, no zlib, no libxml.
- **Memory does not scale with the design.** Nothing is buffered except the
  source-file table and the history nodes, which the standard requires to be
  resident. A million coverage points cost the same resident memory as ten.
- **It is the standard's own API.** Not a bespoke builder that resembles it —
  `ucis_OpenWriteStream` / `ucis_CreateScope` / `ucis_CreateNextCover` /
  `ucis_WriteStreamScope` / `ucis_Close`, as specified in UCIS 1.0 Annex B.
  Code written against it ports to any conforming implementation.
- **It never takes your simulation down.** No aborts, no exceptions, no
  writes to stderr. Errors latch on the database and surface at `ucis_Close`.

## Using it

```c
#define UCIS_WRITER_IMPLEMENTATION   /* in exactly one translation unit */
#include "ucis_writer.h"

ucisT db = ucis_OpenWriteStream("coverage.xml");

ucisFileHandleT f = ucis_CreateFileHandle(db, "rtl/top.sv", NULL);
ucis_CreateHistoryNode(db, NULL, "run1", NULL, UCIS_HISTORYNODE_TEST);
ucis_CreateInstanceByName(db, NULL, "top", NULL, 1, UCIS_VLOG,
                          UCIS_INSTANCE, "work.top", UCIS_INST_ONCE);
ucis_WriteStreamScope(db);

if (ucis_Close(db) != 0) { /* something went wrong; see ucis_writer_error */ }
```

Every other header that includes `ucis_writer.h` gets declarations only.

Start from `examples/01_hello.c`; `examples/README.md` indexes the rest by
what you are trying to do.

## Rules you have to follow

UCIS write-streaming trades flexibility for memory, and the XML schema adds
ordering constraints of its own. Three that will bite:

1. **Create every file handle before the first instance.** The source-file
   table is written out at that point and sealed.
2. **Strings are not copied.** A `name` you pass in must stay valid until the
   next `ucis_Create*` or `ucis_WriteStream*` call. This is the standard's
   contract, and it is what makes streaming millions of names free.
3. **Group by instance, then by coverage kind.** The schema orders coverage
   kinds within an instance, and this library will not buffer to reorder for
   you — it reports the violation instead.

See the ordering reference in the published docs for the full list, each with
the exact error the library reports.

## Building and testing standalone

```
cmake -S . -B build && cmake --build build && ctest --test-dir build
```

No dependencies; a full configure, build and test run takes well under a
second.

## Layout

| Path | What it is |
| --- | --- |
| `include/ucis_writer.h` | **generated** single-header deliverable — the thing you vendor |
| `include/ucis_writer.hpp` | optional C++ RAII facade |
| `src/` | the sources it is generated from; develop here |
| `examples/` | worked examples, also the schema-validation corpus |
| `tests/` | unit and integration tests |

`include/ucis_writer.h` is produced by `tools/amalgamate.py` and checked for
freshness in CI. Edit `src/`, not the header.

## Status

Under construction; see `docs/ucis-writer-impl-plan.md` for what is done and
what is not. **The licence of this directory is not final** — Apache-2.0
matches the rest of the repository but constrains who can vendor it.
