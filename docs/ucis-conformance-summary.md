<!-- GENERATED FILE -- DO NOT EDIT.
     Regenerate with:  python3 tools/gen_conformance_matrix.py
     Sources: docs/conformance/features/*.yaml (intent)
              docs/ucis-parquet-mapping.md (documentation claims)
              tests/conformance/coverage.json (test evidence)
     See docs/ucis-conformance-structure-plan.md. -->

# UCIS conformance summary

The review surface for [`ucis-parquet-feature-map.md`](ucis-parquet-feature-map.md). Kept separate so a coverage change is a few visible lines in a diff rather than a movement inside 280 rows. **A percentage that drops in a PR needs an explanation in the PR description.**

Backend: `parquet`

| Metric | Value |
| ------ | ----- |
| Registry entries | 378 |
| In scope (core + extended) | 318 |
| Out of scope / deferred | 60 |
| Documentation coverage | 313/318 (98.4%) |
| Test coverage | 178/318 (56.0%) |
| Conformance (documented **and** tested) | 178/318 (56.0%) |
| Tested but undocumented | 0 |

## By profile

`core` is required for conformance; `extended` MAY be omitted. A gap in core is a different kind of problem from a gap in extended, and a single coverage percentage hides that.

| Profile | Features | Documented | Tested | Both |
| ------- | -------: | ---------: | -----: | ---: |
| core | 297 | 292 (98.3%) | 167 (56.2%) | 167 (56.2%) |
| extended | 21 | 21 (100.0%) | 11 (52.4%) | 11 (52.4%) |

## By kind

| Kind | In scope | Documented | Tested | Both |
| ---- | -------: | ---------: | -----: | ---: |
| feature | 299 | 294 | 166 | 166 |
| combination | 12 | 12 | 10 | 10 |
| negative | 7 | 7 | 2 | 2 |

## Test coverage by level

Depth, not just breadth. 200 IDs covered only at L1 is a different claim from 200 covered at L2/L3, and these two numbers must not read the same.

| Level | What it proves | Features covered |
| ----- | -------------- | ---------------: |
| L0 | static registry / schema integrity | 7 |
| L1 | mechanical round trip | 156 |
| L2 | cross-backend equivalence against an oracle | 26 |
| L3 | algebraic merge laws | 18 |
| L4 | spec-only reader (no covsight import) | 32 |

## Status counts

| Status | Count |
| ------ | ----: |
| ✅ mapped + tested | 178 |
| 🟡 mapped, untested | 135 |
| 🟠 tested, undocumented | 0 |
| ❌ unmapped | 5 |
| ⚪ out of scope / deferred | 60 |
