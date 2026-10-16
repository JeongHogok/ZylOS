# ZylOS Fuzz Testing

## Prerequisites

- Clang with libFuzzer support (`-fsanitize=fuzzer`)
- AddressSanitizer enabled for memory bug detection

## Harnesses

### fuzz_json_dispatch

Fuzzes the WAM bridge JSON dispatch parser. Ensures arbitrary JSON input
does not cause crashes, buffer overflows, or undefined behavior in the
service routing path.

```bash
clang -g -O1 -fsanitize=fuzzer,address fuzz_json_dispatch.c -o fuzz_json_dispatch
mkdir -p corpus/json_dispatch
echo '{"service":"fs","method":"getDirectory","params":{"path":"/"}}' > corpus/json_dispatch/seed.json
./fuzz_json_dispatch corpus/json_dispatch -max_len=4096 -runs=100000
```

### fuzz_fs_path

Fuzzes the filesystem path validation and normalization. Tests for:
- Path traversal (`../`)
- Null byte injection
- Double-slash collapsing
- Protected path prefix detection

```bash
clang -g -O1 -fsanitize=fuzzer,address fuzz_fs_path.c -o fuzz_fs_path
mkdir -p corpus/fs_path
echo '/data/user/test.txt' > corpus/fs_path/seed1.txt
echo '../../etc/passwd' > corpus/fs_path/seed2.txt
./fuzz_fs_path corpus/fs_path -max_len=1024 -runs=100000
```

## CI Integration

Already wired: the `fuzz` job in `.github/workflows/native.yml` builds every
`fuzz_*.c` with `-fsanitize=fuzzer,address` and runs it for 10000 iterations.
Run it locally with `tools/ci-local.sh fuzz` (natively — ASan does not work
under amd64 emulation on an ARM host).
