# BucketExplorer

**English** | [中文](README.zh-CN.md)

A desktop browser for S3 and S3-compatible object storage — UCloud US3, AWS S3,
MinIO, and anything else that speaks the S3 API.

This is a **Qt rewrite** of [pteich/us3ui](https://github.com/pteich/us3ui).
It is not a port: no line of the original Go/Fyne code was translated. The
feature set was reimplemented from scratch on top of Qt 6 and a hand-written
SigV4 signer, because the original's structure — a single 1150-line window that
called into a vendor SDK from a background goroutine — is what made its bugs
hard to fix in the first place.

## What it does

| | |
|---|---|
| **Connections** | Named profiles saved locally: endpoint, access key, secret key, bucket, region, prefix, TLS, addressing style. Test-connection button in the dialog. |
| **Browse** | Bucket list, then a folder-grouped object table with breadcrumb navigation and back/forward/up history (Alt+Left / Alt+Right / Alt+Up). |
| **Bucket root** | A connection that names no bucket opens on the account's bucket list — its own level, with its own breadcrumb, not a stand-in for a key prefix. Opening a bucket shows its objects and rewrites the path to `Buckets › bucket › folder › …`; the crumb and Alt+Up go back up to the list. |
| **Search & sort** | Client-side filter over the loaded objects; locale-aware, digit-aware sorting by name, size, or modified time. Folders always sort before files. |
| **Paginate** | Objects load 500 at a time, continuing from where the last page ended; "Load more" appends instead of replacing, so a 50 000-object bucket stays responsive. |
| **Details** | Key, size, modified time, ETag, and storage class for the selected row. |
| **Transfers** | Upload and download queue with per-item progress and cancel, run through `QNetworkAccessManager` so the UI thread is never blocked. |
| **Sharing** | Presigned GET URLs, valid for one hour, generated locally without a round trip. |
| **Buckets** | Create and delete buckets from a separate manager window, with client-side name validation. |
| **CLI** | `--endpoint`, `--access-key`, `--secret-key`, `--bucket`, `--prefix`, `--region`, `--target`, `--ssl` / `--no-ssl`. Each falls back to the `ENDPOINT`, `ACCESS_KEY`, `SECRET_KEY`, … environment variables the original used. |

## Differences from the original, and why

**No vendor SDK.** Signatures are computed with `QCryptographicHash` and
`QMessageAuthenticationCode` against the published SigV4 specification. The
signer is verified against AWS's own test vectors in the test suite, so a
provider that rejects a request can be distinguished from a signer that is
wrong.

**No worker threads.** Every request goes through the event loop; replies are
delivered to the GUI thread by construction. The original called its UI toolkit
from goroutines, which is the source of most of its stability problems.

**Credentials in DPAPI.** Secret keys are encrypted with
`CryptProtectData` (Windows Data Protection API), so they are readable only by
the user account that stored them. The original kept them in plaintext JSON.
`CredentialStore` is a single interface, so another platform's keychain can be
substituted without touching callers.

**Provider differences are data, not branches.** `TargetProfile` describes how a
given provider lays out hostnames and derives a region — US3's
`s3-<region>.ufileos.com`, AWS, MinIO, generic path-style — instead of scattering
`if provider ==` checks through the transport layer.

**A virtualised table.** `ObjectModel` keeps one index list over the loaded
objects and never copies them; `QTableView` renders only visible rows. Fixed row
height lets Qt compute visibility without querying the model per row, which is
what keeps very large listings usable.

## Build

Requires Qt 6 (Core, Gui, Widgets, Network, and Test for the suite), CMake 3.21+,
and a C++17 compiler. Verified with Qt 6.9.3 (mingw_64), GCC 13.1.0, CMake 4.0.1,
and Ninja on Windows 11.

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_PREFIX_PATH=C:/Qt/6.9.3/mingw_64
cmake --build build
```

The binary lands at `build/bucketexplorer.exe`. On Windows it is built as a GUI
subsystem executable, so it will not attach a console window.

## Tests

```sh
ctest --test-dir build --output-on-failure
```

The test binaries link Qt dynamically, so Qt's `bin` directory has to be on
`PATH` (`C:/Qt/6.9.3/mingw_64/bin`); without it they exit with `0xc0000135`
before running a single case.

Five suites, none of which need a display or a network:

- `test_sigv4` — the signer, against AWS's worked examples and test-suite vectors
- `test_list_parser` — XML listing responses, including provider quirks and
  error-code mapping
- `test_target_profile` — hostname layout and region derivation per provider
- `test_transfer_queue` — the transfer state machine
- `test_object_model` — filtering, folder grouping, and sorting

## Logs

Every run writes `bucketexplorer.log` beside `settings.json` — on Windows that is
`%LOCALAPPDATA%\bucketexplorer\bucketexplorer\`. The previous run is kept as
`bucketexplorer.log.1`.

Each request records the URL that was actually built, the host that was actually
signed, the port used, and the credential store's verdict on the secret. A
failure adds the HTTP status, the server's `<Code>` and request id, and the
response body, which for a signature problem is the only place the real reason
appears. Secrets are redacted to four characters and two.

`BUCKETEXPLORER_LOG` overrides the path, or turns logging off with `off`.

## Layout

```
src/core/     transport, signing, config, credentials, transfers  (no GUI)
src/compat/   provider profiles
src/ui/       widgets, models, theme
tests/        Qt Test suites
```

`core` and `compat` link only against Qt Core/Network, so all of the logic that
can be wrong without a user noticing is testable in isolation. `ui` is a
separate static library so the model and theme can be exercised under a
`QApplication` without opening the main window.

## License

MIT, as the original.
