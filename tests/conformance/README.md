# Conformance tests

Levels L0–L5; see [`docs/conformance/README.md`](../../docs/conformance/README.md)
for what each proves and how to tag a test.

```
l0_static/       registry integrity, schema validity, claim resolution
l1_roundtrip/    mechanical round trip over the corpus
l2_equivalence/  cross-backend + XML oracle
l3_laws/         merge algebra, identity laws
l4_spec_only/    SQL written from the document alone, no covsight import
fixtures/        builders for the feature-complete corpus
```

Only `l0_static/` is populated. The rest are placeholders whose content is
phases P4–P5 of
[`docs/ucis-mapping-conformance-plan.md`](../../docs/ucis-mapping-conformance-plan.md);
they plug into the existing machinery as new tags, not as new tooling.

**Tagged tests live wherever they belong.** Most current evidence comes from
`tests/parquet/`, and moving it here would be churn for no gain — the marker
carries the level, not the directory.

`coverage.json` is a per-run artifact and is gitignored. Produce it with:

```bash
pytest --conformance-json=tests/conformance/coverage.json
```

Note that a partial run (`-k`, `-m`, explicit paths) marks the evidence
incomplete on purpose, and the matrix generator will refuse it.

## What is deliberately not here

A test asserting the committed matrix is current. That check needs evidence,
which only exists after the suite has run, so it is a CI step *after* pytest. A
test that regenerated the matrix mid-run would rebuild it from a partial
collection — the exact failure `collected_full` exists to prevent.
