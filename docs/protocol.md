# rauc-ble-otad wire protocol

This document specifies the over-the-air byte format used between a BLE
central (phone / PC) and the `rauc-ble-otad` daemon on the STM32MP2 target.

A reader of this document can implement an interoperable client without
reading the daemon source.

The stack has three independent layers. Each byte on air is interpreted
in this order:

```
+-----------------------------------------+
|  3. protobuf message body               |  proto/app.proto
+-----------------------------------------+
|  2. 2-byte big-endian application TYPE  |  this document §3
+-----------------------------------------+
|  1. BLE framing (CTRL + total length)   |  this document §2
+-----------------------------------------+
|  0. GATT transport (Write / Notify)     |  this document §1
+-----------------------------------------+
```

Only layer 0 (GATT) depends on Bluetooth. Layers 1–3 are transport-
agnostic and could be carried over any reliable datagram link of at
least 20 bytes MTU.


## 1. GATT transport

The daemon registers a single primary service with two characteristics.

| Role    | UUID                                     | Properties               |
|---------|------------------------------------------|--------------------------|
| Service | `12345678-1234-5678-1234-56789abcdef0`   | primary                  |
| RX char | `12345678-1234-5678-1234-56789abcdef1`   | Write Without Response   |
| TX char | `12345678-1234-5678-1234-56789abcdef2`   | Notify                   |

- **RX** = phone → board direction. The client writes whole BLE frames
  (one frame per Write).
- **TX** = board → phone direction. The client enables notifications
  (writes `0x0001` to the CCCD) and receives whole BLE frames, one per
  notification.

No encryption / pairing is required by the service. The daemon
advertises the local name `OTA-STM32MP2` and the service UUID in the
advertisement data.

The daemon uses a **fixed ATT payload of 20 bytes** (BLE 4.0 default,
MTU 23 − 3-byte ATT header). Clients may negotiate a larger MTU but the
daemon will still produce 20-byte outbound frames. Clients MUST NOT send
inbound frames larger than 20 bytes.


## 2. BLE framing layer

A layer-2 **message** is an opaque byte string of up to 4 294 967 295
bytes. It is transmitted as one or more **frames**, each carried in a
single GATT Write (RX) or Notify (TX).

Every frame starts with a 1-byte **control prefix**:

| Symbol | Byte   | Meaning                                     |
|--------|--------|---------------------------------------------|
| `\|`   | `0x7C` | SINGLE — whole message fits in one frame    |
| `^`    | `0x5E` | FIRST  — first frame of a multi-frame msg   |
| `~`    | `0x7E` | MIDDLE — intermediate frame                 |
| `;`    | `0x3B` | END    — last frame of a multi-frame msg    |

SINGLE and FIRST frames carry an additional 4-byte **big-endian
total length** (the byte length of the complete layer-2 message). MIDDLE
and END frames carry only the control byte.

Frame layout (at the default 20-byte ATT payload):

```
SINGLE  | 7C | LEN (4B BE) | body (≤ 15 bytes)  |         total ≤ 20 B
FIRST   | 5E | LEN (4B BE) | body (exactly 15B) |         total    20 B
MIDDLE  | 7E | body (≤ 19 bytes)                          total ≤ 20 B
END     | 3B | body (≤ 19 bytes)                          total ≤ 20 B
```

All multi-byte integers on the wire are **big-endian**. Use explicit
shift/mask, not `htonl`.

### Sender rules

1. If the complete message length `L ≤ 15`, emit one SINGLE frame with
   `LEN = L` followed by all `L` body bytes.
2. Otherwise emit a FIRST frame with `LEN = L` followed by 15 body
   bytes; then zero or more MIDDLE frames each carrying 19 body bytes;
   then an END frame carrying the final `1..19` remaining body bytes.
3. Never emit a zero-length message and never emit a MIDDLE/END outside
   a started message.

### Receiver rules

1. On SINGLE: deliver the body (length = LEN) immediately. Reset.
2. On FIRST: remember LEN, accumulate the 15 body bytes, enter
   `REASSEMBLING`.
3. On MIDDLE: append body bytes; drop if `REASSEMBLING` not active or
   if accumulated bytes would exceed LEN.
4. On END: append body bytes; if total accumulated == LEN deliver,
   else drop. Reset.
5. Drop and reset on unknown control byte, overflow, or optional idle
   timeout.

The reference receiver is `src/ble_reasm.c`. It uses a caller-supplied
buffer (no heap) and an injected `now_ms` clock so it can be exercised
deterministically from tests.


## 3. Application dispatch layer

The body delivered by the framing layer has the shape:

```
+--------+-----------------------------+
| TYPE   | protobuf-encoded body       |
| 2B BE  | 0..N bytes                  |
+--------+-----------------------------+
```

`TYPE` is a 16-bit big-endian message identifier. All defined values
are listed below. Receivers MUST treat an unknown TYPE as a protocol
error and MAY disconnect.

### TYPE table

| TYPE     | Name                  | Direction     | Protobuf message       |
|----------|-----------------------|---------------|------------------------|
| `0x0001` | `PING`                | phone → board | `ble.app.Ping`         |
| `0x0002` | `PONG`                | board → phone | `ble.app.Pong`         |
| `0x0010` | `OTA_BEGIN`           | phone → board | `ble.app.OtaBegin`     |
| `0x0011` | `OTA_BEGIN_ACK`       | board → phone | `ble.app.OtaBeginAck`  |
| `0x0012` | `OTA_CHUNK`           | phone → board | `ble.app.OtaChunk`     |
| `0x0013` | `OTA_CHUNK_ACK`       | board → phone | `ble.app.OtaChunkAck`  |
| `0x0014` | `OTA_END`             | phone → board | `ble.app.OtaEnd`       |
| `0x0015` | `OTA_END_ACK`         | board → phone | `ble.app.OtaEndAck`    |
| `0x0020` | `GET_VERSION`         | phone → board | `ble.app.GetVersion`   |
| `0x0021` | `VERSION_REPLY`       | board → phone | `ble.app.VersionReply` |
| `0x0022` | `MISSING_CHUNKS`      | board → phone | `ble.app.MissingChunks`|
| `0x0023` | `OTA_INSTALL`         | phone → board | `ble.app.OtaInstall`   |
| `0x0024` | `OTA_INSTALL_REPLY`   | board → phone | `ble.app.OtaInstallReply` |

The canonical constant definitions live in
[`src/app_dispatch.h`](../src/app_dispatch.h).


## 4. Protobuf schema

See [`proto/app.proto`](../proto/app.proto) for the authoritative
schema. A stable copy of field numbers (for reader convenience) follows.
**Field numbers are part of the wire format and MUST NOT be reused.**

```protobuf
syntax = "proto3";
package ble.app;

message Ping              { uint64 nonce = 1; }
message Pong              { uint64 nonce = 1; }
message GetVersion        {}
message VersionReply      { string version = 1; }

message OtaBegin {
    string filename    = 1;   // relative to /tmp, at most one sub-dir
    uint64 total_size  = 2;
    uint32 chunk_size  = 3;
}
message OtaBeginAck       { bool   ok = 1; string error = 2; }
message OtaChunk          { uint32 seq = 1; bytes data = 2; }
message OtaChunkAck       { uint32 seq = 1; bool  ok   = 2; }
message OtaEnd            { uint64 crc64 = 1; }           // crc64 unused
message OtaEndAck         { bool   ok = 1; string error = 2; }
message MissingChunks     { bool   ok = 1; string error = 2; bytes content = 3; }
message OtaInstall        { bytes  sha256 = 1; }          // 32 bytes
message OtaInstallReply   { bool   ok = 1; string error = 2; }
```

### Conventions

- proto3 default values (zero, empty string, empty bytes, false) are
  not serialized on the wire. A missing field means "default".
- `OtaBegin.filename` is resolved against `/tmp`. The daemon accepts at
  most one sub-directory component (e.g.
  `rootfs.castr/ab/abcdef.cacnk`); additional depth is rejected.
- `OtaChunk.seq` starts at 0 and increments by 1 per chunk. The daemon
  acks every chunk it accepts; it MAY tolerate duplicates but MUST NOT
  accept chunks out of order.
- `OtaInstall.sha256` is the expected SHA-256 (exactly 32 bytes) of the
  rebuilt `.raucb` bundle. Integrity is verified against this hash, not
  against `OtaEnd.crc64`.


## 5. Update flow

A successful OTA run on a single `.raucb` bundle is a fixed sequence of
message exchanges:

```
phone                                         board
  |                                             |
  |  OTA_BEGIN(filename=rootfs.caibx, ...)      |
  |-------------------------------------------->|
  |                          OTA_BEGIN_ACK(ok)  |
  |<--------------------------------------------|
  |  OTA_CHUNK(seq=0, data=...)                 |
  |-------------------------------------------->|
  |                   OTA_CHUNK_ACK(seq=0, ok)  |
  |<--------------------------------------------|
  |  ... (repeat for every chunk) ...           |
  |                                             |
  |  OTA_END(crc64=...)                         |
  |-------------------------------------------->|
  |                            OTA_END_ACK(ok)  |
  |<--------------------------------------------|
  |                                             |
  |     (spontaneous, only for rootfs.caibx)    |
  |                  MISSING_CHUNKS(content=…)  |
  |<--------------------------------------------|
  |                                             |
  |  — repeat BEGIN/CHUNK/END for each .cacnk   |
  |     listed in MISSING_CHUNKS —              |
  |                                             |
  |  OTA_INSTALL(sha256=...)                    |
  |-------------------------------------------->|
  |                      OTA_INSTALL_REPLY(ok)  |
  |<--------------------------------------------|
```

Notes:

- The first file uploaded is the casync index (`*.caibx`). After
  `OTA_END_ACK` for a `.caibx`, the board **spontaneously** emits
  `MISSING_CHUNKS` whose `content` field is the raw newline-separated
  output of `casync list-chunks` (one `ab/hash.cacnk` path per line).
- The phone uploads each missing chunk as a separate
  `OTA_BEGIN / CHUNK… / OTA_END` sequence with `filename` set to the
  `ab/hash.cacnk` path.
- After all chunks are present, the phone sends `OTA_INSTALL`. The
  board reassembles the `.raucb`, verifies SHA-256, hands it to RAUC,
  and returns `OTA_INSTALL_REPLY`.
- `PING` / `GET_VERSION` are stateless and may be sent at any time.


## 6. Worked example

The phone sends `GetVersion` and the board replies `VersionReply
{version="unknown"}`.

### Request (phone → RX)

```
7C 00000002 0020
```

| Bytes        | Meaning                                                   |
|--------------|-----------------------------------------------------------|
| `7C`         | SINGLE frame                                              |
| `00 00 00 02`| total length = 2                                          |
| `00 20`      | TYPE = `0x0020` (GET_VERSION)                             |
| (no body)    | `GetVersion` has no fields                                |

### Response (board → TX notification)

```
7C 0000000B 0021 0A 07 75 6E 6B 6E 6F 77 6E
```

| Bytes        | Meaning                                                   |
|--------------|-----------------------------------------------------------|
| `7C`         | SINGLE frame                                              |
| `00 00 00 0B`| total length = 11                                         |
| `00 21`      | TYPE = `0x0021` (VERSION_REPLY)                           |
| `0A`         | protobuf tag: field=1, wire type=2 (length-delimited)     |
| `07`         | string length = 7                                         |
| `75..6E`     | UTF-8 `"unknown"`                                         |

The protobuf tag byte is encoded as
`(field_number << 3) | wire_type`. Wire types used by this protocol:

| wire type | meaning             | field kinds in use here        |
|-----------|---------------------|--------------------------------|
| 0         | varint              | `uint32`, `uint64`, `bool`     |
| 2         | length-delimited    | `string`, `bytes`              |


## 7. Error handling

- Malformed frames: receiver drops the in-flight message and returns to
  IDLE. Sender is expected to re-send the full message from the start.
- Unknown `TYPE`: receiver MAY disconnect the link.
- `OtaBeginAck.ok == false`: previous `OtaBegin` is not active; the
  phone MUST NOT send `OtaChunk` afterwards. `error` carries a
  human-readable reason.
- `OtaEndAck.ok == false`: the accumulated file did not match
  `total_size` (or the parent directory could not be created). The
  phone should restart the file with a new `OtaBegin`.
- `OtaInstallReply.ok == false`: RAUC refused the bundle. `error` is
  the stderr tail from `rauc install`.


## 8. Reference implementations

- C framing and reassembly: `src/ble_pack.{c,h}`, `src/ble_reasm.{c,h}`
- C++ application dispatch: `src/app_dispatch.{cc,h}`
- GATT glue (BlueZ D-Bus): `src/gatt_server.c`
- Python reference client: `tools/ota_client.py`
- Minimal smoke test: `tools/ping_version.py`
