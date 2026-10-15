# Zyl OS App Store — Store Server Protocol

A store is **a directory of static files on any HTTPS host**. There is no server
program: `tools/build-store.py` makes the files, the device and the emulator
verify them. Trust comes from the store's signing key, not from the host — a
CDN or mirror can serve the files without being able to change them.

## Files

```
<store>/catalog.json          the apps on offer
<store>/catalog.json.sig      base64 RSA-SHA256 over SHA-256(catalog.json)
<store>/<id>-<version>.zpkg   one zip per app version
```

### catalog.json

```json
{
  "version": 1,
  "apps": [
    {
      "id": "com.example.hello",
      "name": "Hello",
      "version": "1.2.0",
      "description": "…",
      "developer": "Example Inc.",
      "category": "productivity",
      "permissions": ["network"],
      "package": "com.example.hello-1.2.0.zpkg",
      "size": 10240,
      "sha256": "<64 hex>",
      "signature": "<base64 RSA-SHA256 over the package's SHA-256>"
    }
  ]
}
```

- `package` is relative to `catalog.json` or an absolute `https://` URL
  (redirects are followed, to https only).
- `id`: reverse-domain, letters/digits/`.`/`_`/`-` — it names the app's
  directory and its container. `version`: digits and dots.
- Entries that break a rule are skipped; the first entry with an id wins.
  System app ids are never offered.

### Package (`.zpkg`)

A zip of the app directory with `app.json` at the root. Plain files and
folders only — absolute paths, `..`, links and devices make the whole package
invalid. At most 4096 files, 50 MiB packed, 200 MiB unpacked. Its `app.json`
must name the catalog's `id` and `version`, and its `entry` page must exist.

## Signatures

The same scheme as OTA updates (`tools/build-ota.sh`): RSA-SHA256 over the
file's SHA-256 digest. The device checks the catalog against the key before
reading it (`untrusted` otherwise), and each package against both the
catalog's `sha256` and `signature` before unpacking it (`bad_package`).

## Device configuration

`/etc/zyl-os/store.json` (from `system/store.json` in the image):

```json
{ "url": "https://store.example/v1/catalog.json", "publicKey": "-----BEGIN PUBLIC KEY-----\n…" }
```

Empty `url` or `publicKey` → the store is `unconfigured` (the Store app says so).

## Making a store

```bash
openssl genrsa -out store.key 3072
openssl rsa -in store.key -pubout -out store.pub        # → publicKey
python3 -I tools/build-store.py package path/to/app out/
python3 -I tools/build-store.py catalog --key store.key out/
# upload out/ to any https host; its catalog.json URL → url
```

## On the device (`org.zylos.AppStore`, `runtime/services/appstore`)

| Method | Result |
|---|---|
| `GetCatalog()` | `{ state: ok·unconfigured·unreachable·untrusted·invalid, apps: [{ id, name, version, description, developer, category, size, permissions, installed, installedVersion, update }] }` |
| `Install(app_id)` | `{ ok, reason }` — installs or updates (never a downgrade). Reasons: `unconfigured`, `unreachable`, `untrusted`, `not_in_store`, `system_app`, `up_to_date`, `download_failed`, `bad_package`, `invalid_package`, `io`, `busy` |
| `Uninstall(app_id)` | `{ ok, reason }` — removes the app **and its whole container** |
| signal `AppInstalled(app_id)`, `AppRemoved(app_id)` | the WAM registers/drops the app; services drop what they kept for it |

Only apps with `app.manage` (system apps: Store, Home) may call it.

## What an app leaves behind: nothing

Everything an app makes lives in its container `/data/appdata/<id>/`:
private data (`appdata`), its files (`files/` — a third-party app never sees
the user's shared folders), its web storage (`webkit/` — local storage,
IndexedDB, cookies, cache; each app is its own origin `zyl-app://<id>/`).
Uninstall closes the app, deletes the package and the container, and
`AppRemoved` makes the notification and alarm services drop the app's
entries. `tests/store_e2e.sh` checks that nothing with the app's id remains.
