# Golden Fixtures

This directory contains committed golden `.cdb` files and their JSON sidecars.

Each `.json` file is the canonical representation of the corresponding `.cdb`,
produced by the Python reader (`NcdbReader`) and used as the expected output
for all three readers in the cross-compat tests.

## Naming Convention

- `py_{scenario}.cdb` — written by Python `NcdbWriter`
- `ts_{scenario}.cdb` — written by TypeScript `NcdbWriter`
- `c_{scenario}.cdb`  — written by C `ncdb_Write`

## Regenerating

```bash
cd /path/to/repo
# Build TypeScript first (if not already built)
cd ts && npm run build && cd ..
# Build C library (if not already built)
cmake -S c -B c/build && cmake --build c/build
# Regenerate
PYTHONPATH=python python tests/compat/generate_golden.py --update
```

## Format

See `tests/compat/schema.md` for the canonical JSON schema specification.
