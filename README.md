# rauc-ble-otad

[中文文档](README_CN.md)

BLE OTA update daemon for RAUC-based STM32MP2 systems. The daemon runs as a BLE GATT peripheral, allowing a phone to push firmware updates over Bluetooth Low Energy and install them via RAUC.

## Architecture

```
Phone (BLE Central)
  │
  ▼
┌─────────────────────────────────────────┐
│  BLE Framing (ble_pack / ble_reasm)     │  C — ATT-sized PDU segmentation
│  ─ reassembles multi-frame messages     │     and reassembly
├─────────────────────────────────────────┤
│  Application Dispatch (app_dispatch)    │  C++ with C ABI — protobuf
│  ─ encode/decode typed messages         │     encode/decode, type routing
├─────────────────────────────────────────┤
│  OTA Handler + Casync Runner            │  C — file receive state machine,
│  ─ chunk receive, casync, rauc install  │     differential update, install
├─────────────────────────────────────────┤
│  GATT Server (gatt_server)              │  C/GLib — BlueZ D-Bus peripheral,
│  ─ BLE service, characteristics, adv    │     GATT application registration
└─────────────────────────────────────────┘
```

### BLE framing

Each logical message is split into BLE ATT-sized PDUs (default 20-byte payload) using a 1-byte control prefix:

| Prefix | Meaning |
|--------|---------|
| `^`    | First frame (includes 4-byte BE total length) |
| `~`    | Middle frame |
| `;`    | End frame |
| `\|`   | Single frame (includes 4-byte BE total length) |

### OTA transfer flow

```
Phone                          Board
  │  OtaBegin(filename, size, chunk_size)  │
  │───────────────────────────────────────►│  Opens file for writing
  │  OtaBeginAck(ok)                       │
  │◄───────────────────────────────────────│
  │                                        │
  │  OtaChunk(seq=0, data)                 │  ×N chunks
  │───────────────────────────────────────►│
  │  OtaChunkAck(seq, ok)                  │
  │◄───────────────────────────────────────│
  │                                        │
  │  OtaEnd(crc64)                         │
  │───────────────────────────────────────►│  Closes file, verifies size
  │  OtaEndAck(ok)                         │
  │◄───────────────────────────────────────│
  │                                        │
  │  (if rootfs.caibx)                     │
  │  MissingChunks(ok, content)            │  casync list-chunks output
  │◄───────────────────────────────────────│
  │                                        │
  │  OtaInstall(sha256)                    │  casync extract → verify → rauc
  │───────────────────────────────────────►│
  │  OtaInstallReply(ok, error)            │
  │◄───────────────────────────────────────│
```

### Other messages

- **Ping / Pong** — keep-alive / connectivity check
- **GetVersion / VersionReply** — read firmware version from `/data/os-release`

## Build

Requires `protoc`, libprotobuf, and GLib/GIO development headers on the host.

```sh
make            # builds daemon + all test binaries
make check      # builds and runs all tests
make clean      # removes build artifacts
```

Cross-compile variables:

```sh
make CC=aarch64-linux-gnu-gcc CXX=aarch64-linux-gnu-g++ PROTOC=protoc \
     GLIB_CFLAGS="..." GLIB_LIBS="..."
```

## Usage

```sh
# Start the daemon (requires BlueZ + system D-Bus)
sudo ./rauc-ble-otad

# Debug: hex-dump every BLE frame
sudo OTA_LOG_HEX=1 ./rauc-ble-otad

# Adjust reassembly timeout (default 5000 ms)
sudo OTA_REASM_TIMEOUT_MS=10000 ./rauc-ble-otad
```

The daemon advertises as **OTA-STM32MP2** and exposes one GATT service with two characteristics:

| Characteristic | UUID | Properties |
|----------------|------|------------|
| RX | `...def1` | WriteWithoutResponse |
| TX | `...def2` | Notify |

## Testing

```sh
make check
# or run individual tests:
make tests/test_ble_pack && ./tests/test_ble_pack
make tests/test_ble_reasm && ./tests/test_ble_reasm
make tests/test_app_dispatch && ./tests/test_app_dispatch
```

## Project structure

```
├── Makefile
├── proto/
│   └── app.proto              # Protobuf message definitions
├── src/
│   ├── ble_pack.{c,h}         # BLE frame packing (sender iterator)
│   ├── ble_reasm.{c,h}        # BLE frame reassembly (receiver state machine)
│   ├── app_dispatch.{cc,h}    # Protobuf encode/decode + C ABI wrappers
│   ├── ota_handler.{c,h}      # File receive state machine
│   ├── firmware_version.{c,h} # Read VERSION_ID from os-release
│   ├── casync_runner.{c,h}    # casync/rauc process wrappers
│   ├── gatt_server.{c,h}      # BlueZ GATT peripheral via D-Bus
│   └── main.c                 # Daemon entry point
└── tests/
    ├── test_ble_pack.c
    ├── test_ble_reasm.c
    └── test_app_dispatch.cc
```

## License

See individual source files for license information.
