####################################
NCDB Format Reference
####################################

The **NCDB** (Native Coverage Database) format is a ZIP-based binary file format
with the file extension ``.cdb``.  It is the built-in storage format for
**covsight-core** and the successor to the pyucis CDB format.

.. contents::
   :local:
   :depth: 3

Overview
========

An NCDB file is a standard ZIP archive.  Each logical piece of the coverage
database is stored as a separate ZIP member (file entry within the archive).
This design makes it easy to inspect the contents with any ZIP tool and allows
readers to selectively load only the members they need.

Identifying an NCDB File
-------------------------

An NCDB archive is detected by reading the first four bytes of the file and
checking for the ZIP magic signature ``PK\x03\x04`` (normal archive) or
``PK\x05\x06`` (empty archive).  A file beginning with the SQLite magic
``SQLite format 3\x00`` is a SQLite database and is handled separately.

Archive Members
===============

The table below lists all ZIP members and whether they are required (R) or
optional (O).

.. list-table::
   :header-rows: 1
   :widths: 30 10 60

   * - Member name
     - R/O
     - Description
   * - ``manifest.json``
     - R
     - Format identity, version, and aggregate statistics.
   * - ``strings.bin``
     - R
     - Shared string table (symbol names, file paths).
   * - ``scope_tree.bin``
     - R
     - Complete scope hierarchy encoded as a DFS byte stream.
   * - ``counts.bin``
     - R
     - Cover-item hit counts in DFS order.
   * - ``history.json``
     - R
     - Test-run and merge history nodes (JSON array).
   * - ``sources.json``
     - R
     - Source file handles (file names referenced by scopes).
   * - ``attrs.bin``
     - O
     - Key-value attributes attached to scopes or cover items.
   * - ``tags.json``
     - O
     - String tags attached to scopes.
   * - ``properties.json``
     - O
     - Typed properties (int, str, real, handle) for scopes.
   * - ``toggle.bin``
     - O
     - Toggle-specific metadata (metric, type, direction).
   * - ``fsm.bin``
     - O
     - FSM state/transition metadata.
   * - ``cross.bin``
     - O
     - Cross-coverage product and expression metadata.
   * - ``formal.bin``
     - O
     - Formal verification result metadata.
   * - ``coveritem_flags.bin``
     - O
     - Per-cover-item flag overrides.
   * - ``design_units.json``
     - O
     - Design-unit index for instance-to-DU resolution.
   * - ``contrib/``
     - O
     - Directory of per-test contribution records (one file per test).
   * - ``testplan.json``
     - O
     - Testplan structure imported from an external file.
   * - ``waivers.json``
     - O
     - Coverage waiver rules.
   * - ``test_registry.bin``
     - O
     - v2 binary test registry (all test runs).
   * - ``test_stats.bin``
     - O
     - v2 per-test statistics table.
   * - ``history/bucket_index.bin``
     - O
     - v2 bucket index for the binary history store.
   * - ``history/<seq>.bin``
     - O
     - v2 sealed history buckets (one per 10 000 runs).
   * - ``contrib_index.bin``
     - O
     - v2 per-test contribution index.
   * - ``squash_log.bin``
     - O
     - v2 log of squashed (deduplicated) test runs.
   * - ``issues.bin``
     - O
     - Issue records: ids, severity/kind/state/resolution enums, and link tables.
   * - ``issues_meta.json``
     - O
     - Per-issue rich metadata (title, URL) — positional JSON array.
   * - ``issues_history.bin``
     - O
     - Columnar history of issue state transitions (compressed).

-----

manifest.json
=============

A JSON object identifying the archive and storing aggregate statistics.

.. code-block:: json

   {
     "format":          "NCDB",
     "version":         "2.0",
     "ucis_version":    "1.0",
     "created":         "2025-01-15T10:30:00Z",
     "path_separator":  "/",
     "scope_count":     1234,
     "coveritem_count": 5678,
     "test_count":      42,
     "total_hits":      98765,
     "covered_bins":    5400,
     "schema_hash":     "sha256:<hex>",
     "generator":       "pyucis-ncdb",
     "history_format":  "v1"
   }

Fields:

.. list-table::
   :header-rows: 1
   :widths: 25 75

   * - Field
     - Description
   * - ``format``
     - Always ``"NCDB"``.
   * - ``version``
     - Format version string (currently ``"2.0"``).
   * - ``ucis_version``
     - UCIS LRM version targeted (``"1.0"``).
   * - ``created``
     - ISO-8601 UTC timestamp of when the archive was written.
   * - ``path_separator``
     - Separator character used in scope paths (``"/"`` or ``"."``).
   * - ``scope_count``
     - Total number of scopes in ``scope_tree.bin``.
   * - ``coveritem_count``
     - Total number of cover items (length of ``counts.bin``).
   * - ``test_count``
     - Number of ``TEST`` history nodes in ``history.json``.
   * - ``total_hits``
     - Sum of all hit counts across all cover items.
   * - ``covered_bins``
     - Number of cover items with at least one hit.
   * - ``schema_hash``
     - ``sha256:<hex>`` of the uncompressed ``scope_tree.bin`` bytes.
       Used for fast same-schema merge detection.
   * - ``generator``
     - Tool that produced the archive (informational).
   * - ``history_format``
     - ``"v1"`` (JSON-only history) or ``"v2"`` (binary bucket store).

-----

strings.bin
===========

A flat byte stream encoding all symbol names and file paths referenced by
the scope tree.  Entries are length-prefixed using LEB128 varints followed by
UTF-8 bytes.

::

   strings.bin := (varint:length  utf8_bytes)*

Strings are referenced by zero-based integer index throughout the other
members.  The empty string is always stored at index 0.

-----

scope_tree.bin
==============

A depth-first pre-order byte stream encoding the entire scope hierarchy.  The
reader reconstructs the tree by maintaining a scope stack.

Record types
------------

Every scope record begins with a one-byte **marker**:

.. list-table::
   :header-rows: 1
   :widths: 20 80

   * - Marker value
     - Meaning
   * - ``0x00``
     - Regular scope record (full encoding).
   * - ``0x01``
     - Toggle-pair shorthand (BRANCH scope with the two standard toggle bins).
   * - ``0xFF``
     - End-of-children sentinel — pop one level from the scope stack.

Regular scope record (marker 0x00)
-----------------------------------

After the marker byte, the following fields are written in order:

::

   scope_type  : varint   (ScopeTypeT integer value)
   name_ref    : varint   (index into strings.bin)
   presence    : varint   (bitfield of optional fields present)

   if PRESENCE_FLAGS    (0x01): flags        : varint
   if PRESENCE_SOURCE   (0x02): file_id      : varint
                                line         : varint
                                token        : varint
   if PRESENCE_WEIGHT   (0x04): weight       : varint
   if PRESENCE_AT_LEAST (0x08): at_least     : varint
   if PRESENCE_GOAL     (0x20): goal         : varint
   if PRESENCE_SOURCE_TYPE (0x40): source_t  : varint

   cover_item_count : varint
   for each cover item:
       name_ref : varint   (index into strings.bin)
       # cover type is implicit from parent scope_type
       # count is NOT stored here — it comes from counts.bin

   # child scopes follow (recursively) until 0xFF sentinel

Fields omitted from the presence bitfield use type-level defaults (defined
in :data:`~covsight.core.ncdb.constants.COVER_TYPE_DEFAULTS`).

Toggle-pair record (marker 0x01)
----------------------------------

An optimized encoding for the extremely common case of a ``BRANCH`` scope
containing exactly two ``TOGGLEBIN`` bins named ``"0 -> 1"`` and
``"1 -> 0"``:

::

   0x01              : marker byte
   name_ref : varint : index into strings.bin for the signal name

Two counts are consumed from ``counts.bin`` for the ``0 -> 1`` and
``1 -> 0`` bins respectively.  No child records or sentinel byte follow.

-----

counts.bin
==========

A compact sequence of hit counts for every cover item in the database,
enumerated in the same DFS order as the cover items appear in
``scope_tree.bin``.

The first byte is the encoding mode:

.. list-table::
   :header-rows: 1
   :widths: 20 80

   * - Mode byte
     - Encoding
   * - ``0x00``
     - Fixed 4-byte little-endian ``uint32_t`` per count.
   * - ``0x01``
     - LEB128 unsigned varint per count (compact for sparse data).

-----

history.json
============

A JSON array of history node objects.  Each object represents one test run
(``kind = "TEST"``) or merge operation (``kind = "MERGE"``).

.. code-block:: json

   [
     {
       "logical_name":   "uart_smoke",
       "physical_name":  "uart_smoke.cdb",
       "kind":           "TEST",
       "test_status":    0,
       "sim_time":       100000,
       "time_unit":      "ns",
       "run_cwd":        "/work/sim",
       "cpu_time":       3.14,
       "seed":           "12345",
       "cmd":            "vsim tb_top",
       "args":           "+seed=12345",
       "compulsory":     false,
       "date":           "2025-01-15",
       "user":           "alice",
       "cost":           1.0,
       "tool_category":  "SIM",
       "vendor_id":      "Vendor",
       "vendor_tool":    "vsim",
       "vendor_version": "2024.1",
       "same_tests":     0,
       "comment":        ""
     }
   ]

``test_status`` values correspond to :class:`~covsight.core.api.enums.test_status.TestStatusT`:

.. list-table::
   :header-rows: 1
   :widths: 15 85

   * - Value
     - Meaning
   * - 0
     - ``OK`` — test passed.
   * - 1
     - ``FAILED`` — test failed.
   * - 2
     - ``UNRUN`` — test was not run.
   * - 3
     - ``COMPILE_ERROR`` — compilation failed.
   * - 4
     - ``ELAB_ERROR`` — elaboration failed.

-----

sources.json
============

A JSON array of source-file handle objects listing every source file
referenced by scopes in the database:

.. code-block:: json

   [
     { "file_name": "/rtl/uart.sv" },
     { "file_name": "/rtl/fifo.sv" }
   ]

File IDs are zero-based array indices into this list.

-----

design_units.json
=================

A JSON object mapping design-unit names to scope-path strings.  Used during
reading to resolve ``INSTANCE`` scope DU references:

.. code-block:: json

   {
     "top":  "top",
     "uart": "top.uart"
   }

-----

toggle.bin / fsm.bin / cross.bin
=================================

Binary members encoding coverage-type-specific metadata.  These members are
optional; when absent, defaults are used.

* **toggle.bin** — per-toggle scope: metric (``ToggleMetricT``), type
  (``ToggleTypeT``), and direction filter (``ToggleDirT``).
* **fsm.bin** — per-FSM scope: state and transition lists keyed by scope
  path.
* **cross.bin** — per-cross scope: crossing coverpoint names and cross
  expression details.

Each file is a sequence of ``(path_string, payload)`` pairs where
``path_string`` is the full scope path and ``payload`` is type-specific data.

-----

History Format v2
=================

When ``manifest.json`` contains ``"history_format": "v2"``, the archive
includes a binary bucket store for efficient large-scale test history.

v2 members
----------

.. list-table::
   :header-rows: 1
   :widths: 30 70

   * - Member
     - Description
   * - ``test_registry.bin``
     - Sequential log of every test run ever recorded.
   * - ``test_stats.bin``
     - Per-test aggregate statistics (pass count, fail count, flake score).
   * - ``history/bucket_index.bin``
     - Index of sealed bucket files (sequence number → member path).
   * - ``history/<seq>.bin``
     - Sealed bucket files, each holding up to 10 000 run records.
   * - ``contrib_index.bin``
     - Index mapping test IDs to their contribution ``contrib/*.bin`` entries.
   * - ``squash_log.bin``
     - Log of deduplicated (squashed) runs.

Status flags (v2 test runs)
---------------------------

Each v2 test-run record contains a ``status_flags`` byte split into two
nibbles:

* **High nibble** — status code:

  .. list-table::
     :header-rows: 1
     :widths: 15 85

     * - Value
       - Meaning
     * - 0
       - ``OK`` — run completed normally.
     * - 1
       - ``FAIL`` — run failed.
     * - 2
       - ``ERROR`` — run errored.
     * - 3
       - ``FATAL`` — run encountered a fatal error.
     * - 4
       - ``COMPILE`` — compilation failure.

* **Low nibble** — flag bits:

  .. list-table::
     :header-rows: 1
     :widths: 20 80

     * - Bit
       - Meaning
     * - 0x01
       - ``SEED_IS_HASH`` — seed field stores a hash, not a numeric value.
     * - 0x02
       - ``IS_RERUN`` — this run is a repeat of a prior failed run.
     * - 0x04
       - ``HAS_COVERAGE`` — coverage data is associated with this run.
     * - 0x08
       - ``WAS_SQUASHED`` — run was deduplicated into an earlier run.

-----

Varint Encoding
===============

Throughout ``scope_tree.bin``, ``strings.bin``, ``toggle.bin``, ``fsm.bin``,
``cross.bin``, and similar binary members, unsigned integers are encoded as
**LEB128 unsigned varints** (also called "base-128 varints"):

* Each byte contributes 7 bits of the integer value (least significant first).
* The high bit (0x80) of each byte is set if more bytes follow.
* A value in [0, 127] is encoded in a single byte.

Example::

   0         → 0x00
   127       → 0x7F
   128       → 0x80 0x01
   300       → 0xAC 0x02
   2097151   → 0xFF 0xFF 0x7F

-----

Python Constants
================

All format constants are centralised in
:mod:`covsight.core.ncdb.constants`:

.. automodule:: covsight.core.ncdb.constants
   :members:
   :undoc-members:
   :member-order: bysource

-----

issues.bin
==========

Binary issue-tracking data.  Present only when the database contains at least
one issue record.

**Magic / header (25 bytes)**::

   magic       u32 LE  0x49535342 ('ISSB')
   version     u8      1
   synced_at   u32 LE  global sync timestamp (0 = never synced)
   num_issues  u32 LE
   num_wl      u32 LE  number of waiver–issue links
   num_tl      u32 LE  number of testpoint–issue links
   num_cl      u32 LE  number of coverage–issue links

Immediately following the header is a **string table** in the same
varint-prefixed wire format as ``strings.bin`` (count then length-prefixed
UTF-8 entries).  All string fields in the records below are stored as
16-bit indices into this table.

**Issue record (18 bytes each)**::

   id_idx      u16 LE  index of the issue ID string
   ext_idx     u16 LE  index of the external tracker ID string (may be empty)
   enums       u16 LE  packed severity/kind/state/resolution (see below)
   created_at  u32 LE  creation Unix timestamp
   updated_at  u32 LE  last-update Unix timestamp
   synced_at   u32 LE  last-sync Unix timestamp

Enum packing within ``enums`` (u16):

* bits 15–13: severity  (0=info, 1=low, 2=medium, 3=high, 4=critical)
* bits 12–11: kind       (0=design-bug, 1=test-bug, 2=infra, 3=spec-gap)
* bits 10–8:  state      (0=open, 1=in-progress, 2=resolved, 3=closed, 4=wontfix)
* bits 7–5:   resolution (0=none, 1=fixed, 2=wont-fix, 3=duplicate, 4=not-a-bug)

**Waiver–issue link record (4 bytes each)**::

   waiver_id_idx  u16 LE
   issue_id_idx   u16 LE

**Testpoint–issue link record (5 bytes each)**::

   tp_name_idx    u16 LE
   issue_id_idx   u16 LE
   link_type      u8    (0=blocked-by, 1=caused-by, 2=related)

**Coverage–issue link record (7 bytes each)**::

   scope_path_idx u16 LE
   bin_name_idx   u16 LE
   issue_id_idx   u16 LE
   link_type      u8

-----

issues_meta.json
================

Optional JSON metadata for issues.  The top-level object is::

   {"v": 1, "m": [ <entry>, ... ]}

Each ``<entry>`` is positionally keyed by the issue's index in ``issues.bin``
(same order as the issue records).  An entry is either ``null`` (no rich
metadata for that issue) or an object::

   {"ti": "<title>", "ur": "<url>"}

Either ``"ti"`` or ``"ur"`` (or both) may be absent if not available.

-----

issues_history.bin
==================

Columnar history of issue state transitions.  The file is self-compressed:
sealed archives use LZMA (XZ magic ``\\xfd7zXZ\\x00``); live databases use
zlib DEFLATE.

After decompression, the binary layout is:

**Header (15 bytes)**::

   magic           u32 LE  0x49535348 ('ISSH')
   version         u8      1
   num_id_strings  u16 LE  size of the ID string table
   num_comments    u16 LE  size of the comment string table
   num_issues      u16 LE  number of issues with history rows
   num_records     u32 LE  total rows across all issues

**ID string table**: ``num_id_strings`` entries, each a ``u8`` length
followed by that many UTF-8 bytes.

**Comment string table**: ``num_comments`` entries in the same format.

**Issue index**: ``num_issues`` entries of 8 bytes each::

   id_str_idx  u16 LE  index into the ID string table
   start_row   u32 LE  first row index in the column arrays
   count       u16 LE  number of rows for this issue

**Column arrays** (one value per row, in ascending timestamp order):

* ``ts_base`` – u32 LE global minimum timestamp; delta encoding is relative
  to this value.
* ``ts_deltas`` – per-row unsigned LEB128 varints giving ``(ts - ts_base)``
  for each row.
* ``state_bytes`` – one u8 per row (state value 0–4).
* ``comment_idxs`` – one u16 LE per row (index into comment table, or
  ``0xFFFF`` if no comment).
