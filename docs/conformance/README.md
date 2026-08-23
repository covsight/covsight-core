# UCIS conformance registry

**Humans commit intent; tools compute status.**

A person decides that `L1.6 ucis_OpenReadStream` is out of scope for the Parquet
mapping and writes down why. Nobody ever writes "✅ tested" — that is derived
from collected tests and doc claims, and derived values are not hand-edited.

## The three inputs and the two outputs

```
docs/conformance/features/*.yaml   intent    ← reviewed like code
docs/ucis-parquet-mapping.md       claims    ← <!-- ucis-features: ... -->
tests/conformance/coverage.json    evidence  ← pytest --conformance-json
                    │
                    ▼  tools/gen_conformance_matrix.py
docs/ucis-parquet-feature-map.md   the matrix     ← GENERATED
docs/ucis-conformance-summary.md   the numbers    ← GENERATED
```

None of the three inputs contains a status. That is the whole design.

| File | Owner | Edited by hand? |
| ---- | ----- | --------------- |
| `features/*.yaml` | reviewers | **yes** — this is the source of truth for intent |
| `features.schema.json` | this repo | yes, rarely; it is also the published contract |
| `ucis-feature-catalog.md` | reviewers | yes — the human-readable narrative |
| `ucis-parquet-mapping.md` | reviewers | yes — the specification, incl. claim comments |
| `ucis-parquet-feature-map.md` | the generator | **no** |
| `ucis-conformance-summary.md` | the generator | **no** |
| `tests/conformance/coverage.json` | pytest | no; gitignored, per-run |

## Everyday tasks

### Add a UCIS feature

1. Add the row to `docs/ucis-feature-catalog.md` (the narrative).
2. Add the entry to the matching `features/NN-*.yaml`, with a `scope` per
   backend. `out_of_scope` and `deferred` **require** a `reason`; `deferred`
   also needs an `adr` or `issue` so something brings it back up for review.
   There is no way to skip this: an unclassified entry fails the generator.
3. Claim it in the mapping document, or accept the ❌/🟠 the matrix will show.
4. Tag a test.
5. Regenerate and commit the matrix (below).

### Tag a test as evidence

```python
from covsight.core.conformance import ucis_feature

@ucis_feature("S4.4", "X.1", level="L2", surface="merge")
def test_cross_with_exclusions_merges(...):
    ...
```

`level` is required. An unknown or superseded ID is a **collection error**, not
a warning — a typo must not silently produce an uncredited test.

Tagged tests do not have to live in `tests/conformance/`; most of the existing
ones are in `tests/parquet/` and should stay there.

### Claim a feature in the mapping document

```markdown
### `scopes`
<!-- ucis-features: S4.1, S4.2, ST.*, F10.3 -->
```

Invisible when rendered. `PREFIX.*` claims a whole prefix — necessary for the 39
scope-type IDs, which one column genuinely carries. The claiming heading's
anchor must be unique; the matrix links to it.

### Regenerate the matrix

```bash
pytest --conformance-json=tests/conformance/coverage.json
python3 tools/gen_conformance_matrix.py --evidence tests/conformance/coverage.json
```

CI runs the same two commands and then `--check`. **A PR that touches the
mapping, the registry, or a tagged test must include the regenerated matrix**,
or CI fails. That is the entire enforcement mechanism; it needs no reviewer
vigilance.

## Rules that are not obvious

**IDs are immutable and never reused.** Renaming a feature changes `title`, not
`id`. Withdrawal is `lifecycle: superseded` plus `superseded_by`, and the row
stays forever so old results remain interpretable.

**Evidence is collection-based, outcome-annotated.** The matrix is built from
which tests *exist*, not which ones *passed*. A test skipped for a missing extra
still proves a test exists; conflating the two would make the committed file
churn between environments. Failures are CI's job to report, and must not
silently rewrite a row to ❌.

**A partial run cannot regenerate the matrix.** `pytest -k cross` sets
`collected_full: false` and the generator refuses, rather than deleting 250
rows.

**Missing extras are also a partial run**, and a sneakier one. Several test
modules call `pytest.importorskip` at *module* scope, so a missing extra means
the file yields no collected items at all — invisible to `collected_full`. The
generator therefore requires `parquet`, `iceberg` and `duckdb` in the evidence's
recorded extras. Install with `.[dev,validate,parquet,iceberg,duckdb]`.

**Review the summary, not the matrix.** `ucis-conformance-summary.md` is the
review surface: a coverage percentage that drops in a PR needs an explanation in
the PR description. The matrix itself is generated output.

**Coverage by level is not coverage.** 200 IDs covered only at L1 is a different
claim from 200 covered at L2/L3. The summary reports both so they cannot be read
as the same thing.

**Promote Hypothesis failures.** A shrunk counterexample goes into
`testdata/conformance/hypothesis/` with a named regression test tagged to the
IDs it broke. A generated failure that vanishes is lost work.

**Audit the ⚪ rows per release.** Deferred entries that have quietly become
supported are the most common way a matrix drifts *pessimistically*, which
erodes trust as fast as a false ✅.

## Validation levels

| Level | What it proves |
| ----- | -------------- |
| L0 | static: registry integrity, schema validity, claim resolution |
| L1 | mechanical round trip — UCIS → Parquet → UCIS is lossless |
| L2 | cross-backend equivalence against an oracle backend |
| L3 | algebraic merge laws (idempotence, commutativity, associativity) |
| L4 | spec-only reader — SQL written from the document, no `covsight` import |
| L5 | real-corpus differential |

L4 is the one that catches the failure this whole apparatus exists for: a
feature absent from *both* the writer and the document, so the round trip passes
vacuously and only a reader written from the spec alone notices.

## Known gaps

- **Catalog §5 (source-language enum) and §8 (properties) carry no feature
  IDs** — they are prose enumerations, so nothing in them can be scored.
  §8 matters: it is the `prop_id` vocabulary, and the EAV property long tail is
  a large part of the mapping. `tools/sync_catalog.py` reports both on every
  run. Giving them IDs is tracked in
  [`../ucis-conformance-structure-plan.md`](../ucis-conformance-structure-plan.md).
- Only the `parquet` backend is classified. `backends` is a map, so adding
  `ncdb`, `c` or `ts` columns is data entry, not a schema change.

## Files

- `features.schema.json` — the published contract for the registry files. Kept
  in agreement with `covsight.core.conformance.registry` by an L0 test that runs
  both against every file.
- `features/NN-slug.yaml` — one per catalog section, numbered to match.
  `90-combinations.yaml` and `91-negative.yaml` are hand-authored and have no
  catalog counterpart.
- `tools/bootstrap_feature_registry.py` — one-shot catalog → stubs. Not part of
  any build; it refuses to overwrite classified files.
- `tools/sync_catalog.py --check` — catalog and registry agree.
- `tools/gen_conformance_matrix.py --check` — the committed matrix is current.
