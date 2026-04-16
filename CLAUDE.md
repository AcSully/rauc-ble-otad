# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project purpose

`rauc-ble-otad` is the BLE-side transport/dispatcher for a RAUC-based OTA update daemon targeting STM32MP2. The code here implements the framing, reassembly, and application-message layer that sits between a BLE GATT characteristic and a RAUC update worker. The `dbus/`, `systemd/`, and `worker/` directories are placeholders for the eventual daemon glue and are currently empty — only the pure-C BLE codec and the C++ protobuf dispatcher layer are implemented.

## Build / test

Standard Make-driven build. There is no configure step and no package manager.

```sh
make            # builds all three test binaries (default target)
make check      # builds and runs every test in sequence; aborts on first failure
make clean      # removes objects, generated pb.{cc,h}, and test binaries

# run a single test
make tests/test_ble_pack && ./tests/test_ble_pack
```

Build requires `protoc` and libprotobuf on the host; `CC`, `CXX`, `PROTOC` can be overridden (relevant for cross-compiling to the STM32MP2 target). `app.pb.{cc,h}` are generated from `proto/app.proto` on demand — do not edit them.

The tests are plain assert-driven executables (no framework); there is no lint/format config in the tree. Warnings are `-Wall -Wextra -Werror`, so any new code must be warning-clean under both gcc and g++.

## Architecture — the three layers

Messages travel through three independent layers. Each layer has its own header and is tested in isolation; keep that separation when adding code.

1. **BLE framing (`src/ble_pack.{c,h}`, `src/ble_reasm.{c,h}` — pure C).**
   One logical message is chopped into BLE ATT-sized PDUs using a 1-byte control prefix: `^` (first), `~` (middle), `;` (end), `|` (single-frame). The first/single frame additionally carries a 4-byte big-endian total length. `ble_pack_iter_*` is the sender-side iterator; `ble_reasm_*` is the receiver state machine. The reassembler owns a caller-provided buffer, enforces `total_len`, and supports an optional idle timeout driven by a caller-supplied `now_ms` clock (no internal time source — this matters for testing and for the eventual integration with whatever event loop drives the BLE socket).

2. **Application dispatch (`src/app_dispatch.{c,h}` — C++ with a C ABI).**
   The payload delivered by reassembly is `TYPE(2B, BE) | protobuf_bytes`. `ble_app_encode`/`ble_app_decode` are exposed as `extern "C"` so the C transport layer can round-trip messages without linking protobuf itself. `ble_app_decode` allocates a protobuf object that the caller must release via `ble_app_free(type, msg)` — ownership is explicit and type-tagged.

3. **Protobuf schema (`proto/app.proto`).**
   Currently only `Ping`/`Pong` exist as scaffolding; real OTA commands (manifest push, chunk transfer, RAUC status, etc.) will be added here and wired into `app_dispatch.cc`'s type switch.

### Dependency direction

```
tests/test_ble_pack, test_ble_reasm  ── link only C objects
tests/test_app_dispatch              ── links C objects + C++ dispatch + libprotobuf
```

The C library must stay free of C++/protobuf dependencies so it can be reused in a minimal-footprint context (bootloader-adjacent or a stripped rootfs). When adding functionality, decide which layer it belongs to before writing it — leaking protobuf types into `ble_pack`/`ble_reasm` would break this split and the Makefile's linking strategy.

## Conventions worth knowing

- All multi-byte integers on the wire are **big-endian** (both the 4-byte total length in the first frame and the 2-byte app type). Use explicit shift/mask, not `htonl`/casts.
- `BLE_ATT_PAYLOAD_MIN` is `20` — the BLE 4.0 default ATT MTU minus the 3-byte ATT header. Packing APIs reject smaller values rather than silently producing oversized frames.
- Reassembly never allocates; the buffer is owned by the caller and its capacity bounds the max deliverable message. Preserve this property.
