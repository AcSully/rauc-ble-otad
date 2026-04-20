# Test vectors

Canonical byte samples for every message type in the protocol. Intended
for unit-testing your encoder / decoder **without a physical board**.

Format:

- **Input** — what the higher-level code feeds to the serializer
  (protobuf field values).
- **Wire** — the exact bytes that appear on the GATT characteristic,
  including BLE framing and 2-byte TYPE header. Multi-frame messages
  list each ATT Write separately.
- **Note** — any subtlety to watch for.

All bytes are hex, grouped for readability. All integers are
**big-endian** on the wire. proto3 default values (`0`, `""`, empty
bytes, `false`) are **not** emitted.

Framing recap (att_payload = 20 B, see `protocol.md` §2):

```
SINGLE  7C + LEN(4B BE) + body (≤15 B)
FIRST   5E + LEN(4B BE) + body (15 B)
MIDDLE  7E +              body (≤19 B)
END     3B +              body (≤19 B)
```


## 1. Ping / Pong (single frame)

### 1.1 Ping { nonce = 1 }  (phone → board)

```
7C 00000004 0001 08 01
```

| Bytes         | Meaning                                              |
|---------------|------------------------------------------------------|
| `7C`          | SINGLE frame                                         |
| `00 00 00 04` | total length = 4                                     |
| `00 01`       | TYPE = `PING` (0x0001)                               |
| `08`          | protobuf tag: field 1, wire type 0 (varint)          |
| `01`          | varint(1) → `nonce = 1`                              |

### 1.2 Pong { nonce = 1 }  (board → phone)

```
7C 00000004 0002 08 01
```

Identical shape, TYPE `0x0002` instead.


## 2. GetVersion / VersionReply (single frame)

### 2.1 GetVersion {}  (phone → board)

```
7C 00000002 0020
```

`GetVersion` has no fields so the body is empty.

### 2.2 VersionReply { version = "0.0.1" }  (board → phone)

```
7C 00000009 0021 0A 05 30 2E 30 2E 31
```

| Bytes       | Meaning                                              |
|-------------|------------------------------------------------------|
| `0A`        | tag: field 1, wire type 2 (length-delimited)         |
| `05`        | string length = 5                                    |
| `30 2E 30 2E 31` | UTF-8 `"0.0.1"`                                 |

### 2.3 VersionReply { version = "unknown" }

```
7C 0000000B 0021 0A 07 75 6E 6B 6E 6F 77 6E
```

Returned when `/data/os-release` is missing or has no `VERSION_ID=`.


## 3. OtaBegin / OtaBeginAck

### 3.1 OtaBegin { filename = "t.bin", total_size = 256, chunk_size = 64 }

```
7C 0000000E 0010 0A 05 74 2E 62 69 6E 10 80 02 18 40
```

| Bytes            | Meaning                                           |
|------------------|---------------------------------------------------|
| `0A 05 74…6E`    | field 1 (filename) = `"t.bin"`                    |
| `10 80 02`       | field 2 (total_size) = varint(256) = `80 02`      |
| `18 40`          | field 3 (chunk_size) = varint(64) = `40`          |

### 3.2 OtaBeginAck { ok = true }

```
7C 00000004 0011 08 01
```

### 3.3 OtaBeginAck { ok = false, error = "no such dir" }

```
7C 0000000F 0011 12 0B 6E 6F 20 73 75 63 68 20 64 69 72
```

`ok = false` is the proto3 default for `bool` → **not emitted**; only
the `error` field is serialized. `12` = tag field 2 wire 2, `0B` =
length 11.


## 4. OtaChunk / OtaChunkAck (multi-frame)

### 4.1 OtaChunk { seq = 1, data = 16 bytes "ABCDEFGHIJKLMNOP" }

Application payload (22 bytes):

```
0012 0801 1210 41424344 45464748 494A4B4C 4D4E4F50
 │    │    │  │└─ 16 data bytes "ABCDEFGHIJKLMNOP"
 │    │    │  └── length = 16
 │    │    └──── tag: field 2, wire 2 (bytes)
 │    └──────── seq = 1 (field 1, wire 0, varint)
 └──────────── TYPE = OTA_CHUNK (0x0012)
```

total_len = 22 > 15 → **FIRST + END**.

```
Write 1 (FIRST, 20 B):
  5E 00000016 0012 0801 1210 41 42 43 44 45 46 47 48 49

Write 2 (END, 8 B):
  3B 4A 4B 4C 4D 4E 4F 50
```

### 4.2 OtaChunkAck { seq = 1, ok = true }

```
7C 00000006 0013 08 01 10 01
```

| Bytes   | Meaning                                      |
|---------|----------------------------------------------|
| `08 01` | field 1 (seq) = 1                            |
| `10 01` | field 2 (ok) = true                          |

### 4.3 Larger example — OtaChunk { seq = 0, data = 64 B }

`seq = 0` is the proto3 default → **not emitted**. Body becomes
`12 40 <64 data bytes>`; app payload = `0012 1240 <64 data>` = 68 B.
Frame layout: FIRST + 2 × MIDDLE + END (15 + 19 + 19 + 15 = 68 B of
app).

```
FIRST   5E 00000044 [0012 1240 + 11 data bytes]
MIDDLE  7E [19 data bytes]
MIDDLE  7E [19 data bytes]
END     3B [15 data bytes]
```


## 5. OtaEnd / OtaEndAck

### 5.1 OtaEnd { crc64 = 1 }

```
7C 00000004 0014 08 01
```

`crc64` is currently unused by the board (integrity is verified via
`OtaInstall.sha256`) but the field must still be a valid varint. Use
`0` to omit it entirely, in which case the body is empty:

```
7C 00000002 0014
```

### 5.2 OtaEndAck { ok = true }

```
7C 00000004 0015 08 01
```


## 6. MissingChunks (multi-frame, board → phone spontaneous)

### 6.1 MissingChunks { ok = true, content = "ab/abc.cacnk\n" }

Application payload (19 bytes):

```
0022 0801 1A0D 61622F6162632E6361636E6B0A
 │    │    │  │└─ "ab/abc.cacnk\n"
 │    │    │  └── length = 13
 │    │    └──── tag: field 3 (content), wire 2 (bytes)
 │    └──────── ok = true
 └──────────── TYPE = MISSING_CHUNKS (0x0022)
```

Frames (19 > 15 → FIRST + END):

```
Write 1 (FIRST, 20 B):
  5E 00000013 0022 0801 1A0D 61 62 2F 61 62

Write 2 (END,  5 B):
  3B 63 6E 6B 0A
```

In real life `content` is the full `casync list-chunks` stdout — can
be tens of kilobytes, spanning hundreds of frames.


## 7. OtaInstall / OtaInstallReply (multi-frame)

### 7.1 OtaInstall { sha256 = 32 × 0x00 }  (test-only all-zero hash)

Application payload (36 bytes):

```
0023 0A20 00 00 … (32 zeros)
 │    │ │
 │    │ └── length = 32
 │    └──── tag: field 1 (sha256), wire 2 (bytes)
 └──────── TYPE = OTA_INSTALL (0x0023)
```

Frames (36 > 15, each continuation carries ≤19 → FIRST + MIDDLE + END):

```
Write 1 (FIRST, 20 B):
  5E 00000024 0023 0A20 00 00 00 00 00 00 00 00 00 00 00

Write 2 (MIDDLE, 20 B):
  7E 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00

Write 3 (END, 3 B):
  3B 00 00
```

### 7.2 OtaInstallReply { ok = true }

```
7C 00000004 0024 08 01
```

### 7.3 OtaInstallReply { ok = false, error = "rauc install failed" }

```
7C 00000017 0024 12 13 72 61 75 63 20 69 6E 73 74 61 6C 6C 20 66 61 69 6C 65 64
```

`ok = false` is default → only `error` emitted. `12` = field 2 wire 2,
`13` = length 19, followed by ASCII `"rauc install failed"`.


## 8. Quick sanity checks for an encoder

An encoder that passes these byte-level equality checks is almost
certainly correct:

| Input                                        | Expected bytes                          |
|----------------------------------------------|-----------------------------------------|
| `Ping{nonce=1}`                              | `7C0000000400010801`                    |
| `GetVersion{}`                               | `7C000000020020`                        |
| `OtaBegin{"t.bin",256,64}`                   | `7C0000000E00100A05742E62696E1080021840`|
| `OtaEnd{crc64=0}`                            | `7C000000020014`                        |
| `OtaChunkAck{seq=1,ok=true}`                 | `7C0000000600130801 1001`               |
| `VersionReply{"0.0.1"}`                      | `7C0000000900210A05302E302E31`          |

(Trailing spaces in the last column are for readability only.)


## 9. Corpus generator

If you need to produce more vectors automatically, the reference
encoder in `tools/ota_client.py` (methods `_pack_app` and `pack_frames`)
round-trips through the real `app_pb2` module. A ~20-line harness:

```python
from ota_client import OtaClient
import app_pb2

msg = app_pb2.OtaBegin(filename="t.bin", total_size=256, chunk_size=64)
body = OtaClient._pack_app(OtaClient.T_OTA_BEGIN, msg.SerializeToString())
for frame in OtaClient.pack_frames(body, att_payload=20):
    print(frame.hex())
```
