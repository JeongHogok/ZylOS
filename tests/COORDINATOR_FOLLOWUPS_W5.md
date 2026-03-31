# W5 coordinator-only follow-ups

Shared files were intentionally left untouched by worker W5.

## 1. Root `meson.build`
Install the new range-scoped system assets added under `system/`:
- `system/apparmor/zyl-audio`
- `system/apparmor/zyl-bluetooth`
- `system/apparmor/zyl-camera`
- `system/apparmor/zyl-credential`
- `system/apparmor/zyl-telephony`
- `system/sysusers.d/zyl-wam.conf`
- `system/tmpfiles.d/zyl-wam.conf`

Without that shared-file follow-up, the new profiles/user provisioning exist in-tree
but are not yet installed by the top-level package build.

## 2. CI workflow(s)
If CI should exercise the new bridge test and fuzz harness, add shared-workflow steps for:

```yaml
- name: Run C unit tests
  run: ninja -C builddir test

- name: Build bridge dispatch unit test
  run: |
    cc -std=c11 -Wall -Wextra -DZYL_USE_WEBKIT2GTK \
      -Itests -Itests/include -I. \
      tests/test_bridge_dispatch.c \
      $(pkg-config --cflags --libs glib-2.0 gio-2.0 json-glib-1.0) \
      -o /tmp/test-bridge-dispatch
    /tmp/test-bridge-dispatch
```

Optional fuzz hardening:

```yaml
clang -g -O1 -fsanitize=fuzzer,address -DZYL_USE_WEBKIT2GTK \
  -Itests/fuzz/include -I. \
  tests/fuzz/fuzz_json_dispatch.c \
  $(pkg-config --cflags --libs glib-2.0 gio-2.0 json-glib-1.0) \
  -o fuzz_json_dispatch
```
