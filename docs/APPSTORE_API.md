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

Every package carries its **seal** in `.zyl-seal/`:

```
.zyl-seal/seal.json    {"version":1,"id":…,"appVersion":…,"files":{"<path>":"<sha256>",…}}
.zyl-seal/seal.sig     base64 RSA-SHA256 over SHA-256(seal.json)
.zyl-seal/cert.json    development builds only (below), with cert.sig
.zyl-seal/profile.json development builds only, with profile.sig
```

`files` lists every other file of the app with its SHA-256 — exactly those,
nothing more (`runtime/lib/app_seal.c`).

## Signatures

The same scheme as OTA updates (`tools/build-ota.sh`): RSA-SHA256 over the
file's SHA-256 digest. The device checks the catalog against the key before
reading it (`untrusted` otherwise), and each package against both the
catalog's `sha256` and `signature` before unpacking it (`bad_package`).

Then the seal: an app from the catalog must be sealed by the **store key**.
The device installs, registers and runs a user-installed app only while its
seal holds — every file present, none added, each byte for byte as sealed. The
WAM checks each file against the seal again as it serves it (from memory: the
bytes checked are the bytes the app gets), so code changed after install never
runs. The OS's own apps are under dm-verity instead.

## Development builds (provisioning)

Like iOS: a developer's build runs only on devices the store allowed.

| File | Signed by | Says |
|---|---|---|
| `cert.json` | store | `{developer, name, publicKey, notAfter}` — the store vouches for this developer key |
| `profile.json` | store | `{cert: sha256(cert.json), apps: ["com.acme.app" or "com.acme.*"], devices: [device IDs], notAfter}` |
| `seal.json` | the developer key in `cert.json` | the app's files |

The device installs it only from the user's files (`InstallFile`), never from
the catalog, and only if: both store signatures hold, neither has expired
(`expired`), the certificate is not in the catalog's `revoked` list
(`revoked`), and the profile names this app and **this device's ID**
(`not_provisioned`). The device ID is the board's (`/proc/device-tree/
serial-number`, set by the bootloader from the chip ID); Settings > About
shows it. A board without one runs no development build.

The catalog may list revoked developer certificates (their SHA-256s); the
device keeps the last list it verified and the WAM stops those builds:

```json
{ "version": 1, "apps": [ … ], "revoked": ["<sha256 of cert.json>"] }
```

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
python3 -I tools/build-store.py package --key store.key path/to/app out/
python3 -I tools/build-store.py catalog --key store.key [--revoked revoked.txt] out/
# upload out/ to any https host; its catalog.json URL → url

# A developer: their public key (tools/zyl keygen) and the devices they test on
python3 -I tools/build-store.py dev-cert --key store.key --developer acme --name "ACME" \
    --pubkey acme.pub --days 365 --out acme.cert.json
python3 -I tools/build-store.py profile --key store.key --cert acme.cert.json \
    --app 'com.acme.*' --device EMU-0123456789ABCDEF --days 30 --out acme.profile.json
# the developer: tools/zyl package --key acme.key --cert acme.cert.json --profile acme.profile.json
```

## On the device (`org.zylos.AppStore`, `runtime/services/appstore`)

| Method | Result |
|---|---|
| `GetCatalog()` | `{ state: ok·unconfigured·unreachable·untrusted·invalid, apps: [{ id, name, version, description, developer, category, size, permissions, installed, installedVersion, update }] }` |
| `Install(app_id)` | `{ ok, reason }` — installs or updates (never a downgrade). Reasons: `unconfigured`, `unreachable`, `untrusted`, `not_in_store`, `system_app`, `up_to_date`, `download_failed`, `bad_package` (also: not sealed by the store), `invalid_package`, `io`, `busy` |
| `InstallFile(path)` | `{ ok, reason, appId }` — a `.zpkg` from the user's files (relative path): a development build provisioned for this device, or a store-sealed package; the same version again is allowed, a downgrade is not. Extra reasons: `not_provisioned`, `expired`, `revoked`. The Store app does this when the Files app opens a `.zpkg` |
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
