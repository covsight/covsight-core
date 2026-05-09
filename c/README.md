# NCDB C Library

Native C99 reader/writer for the NCDB coverage database format.

## Build

```sh
cmake -S c -B c/build
cmake --build c/build
ctest --test-dir c/build --output-on-failure
```
