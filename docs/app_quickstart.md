# App-side quick start (iOS / Android)

Platform-specific notes for integrating the `rauc-ble-otad` client into
a mobile app. Assumes you have already read
[`protocol.md`](protocol.md) and generated protobuf bindings from
[`../proto/app.proto`](../proto/app.proto).


## Constants

```
Local name (adv):   OTA-STM32MP2
Service UUID:       12345678-1234-5678-1234-56789abcdef0
RX characteristic:  12345678-1234-5678-1234-56789abcdef1   (Write Without Response)
TX characteristic:  12345678-1234-5678-1234-56789abcdef2   (Notify)
ATT payload limit:  20 bytes  (hard-coded on the board; do not exceed)
```


## Connection sequence

1. **Scan.** Filter by service UUID (preferred) or by local name.
2. **Connect.**
3. **Negotiate MTU.** On Android, call `requestMtu(247)` and wait for
   `onMtuChanged`. iOS negotiates automatically. This does **not**
   change the frame size you send — still ≤ 20 B — but it makes the
   link-layer faster.
4. **Discover services.** Look up the service UUID and its two chars.
5. **Enable TX notifications.** Write `0x0001` to TX's CCCD. On
   Android this is `setCharacteristicNotification(tx,true)` followed
   by writing to the descriptor; on iOS this is
   `setNotifyValue(true, for: tx)`.
6. **Ready.** Send `GetVersion` as a smoke test; you should receive a
   `VersionReply` notification within 200 ms.

Do **not** pair or bond. The service does not require encryption. A
forced pairing attempt will fail (the board has no pairing agent) and
drop the link.


## Sending: framing and flow control

The protocol is strictly request/response. For each logical message:

```
for frame in frames_of(message, att_payload=20):
    write_without_response(rx_char, frame)
wait for notification(s) that reassemble into the reply
```

### iOS (Swift / CoreBluetooth)

`writeValue(_:for:type: .withoutResponse)` is rate-limited internally.
In tight loops (e.g. thousands of `OtaChunk` frames) you MUST gate on
the "ready" callback:

```swift
func peripheralIsReady(toSendWriteWithoutResponse peripheral: CBPeripheral) {
    // drain your frame queue here
    pumpTxQueue()
}

func pumpTxQueue() {
    while !txQueue.isEmpty && peripheral.canSendWriteWithoutResponse {
        let frame = txQueue.removeFirst()
        peripheral.writeValue(frame, for: rxChar, type: .withoutResponse)
    }
}
```

iOS silently drops Writes if you ignore back-pressure, so a naive
for-loop **will** lose frames on large transfers.

### Android (Kotlin / BluetoothGatt)

1. `rx.writeType = BluetoothGattCharacteristic.WRITE_TYPE_NO_RESPONSE`
2. Each call to `gatt.writeCharacteristic(rx)` must complete (via
   `onCharacteristicWrite`) **before** the next call on Android < 13.
   Newer API (`writeCharacteristic(char,value,writeType)` added in
   Android 13) returns a result code and handles the queue for you.
3. On Android 12+, declare `BLUETOOTH_SCAN` and `BLUETOOTH_CONNECT`
   runtime permissions. On 6–11, scanning also requires location
   permission.
4. `gatt.requestMtu(247)` — some stacks silently cap at 185 or 247;
   either is fine. Wait for `onMtuChanged` before issuing writes.


## Receiving: reassembly

TX notifications arrive as whole frames. You must maintain a tiny
state machine:

```
state = IDLE
buf   = []
total = 0

on_notify(pdu):
    ctrl = pdu[0]
    if ctrl == 0x7C:          # SINGLE
        L = u32_be(pdu[1:5])
        deliver(pdu[5:5+L])
    elif ctrl == 0x5E:        # FIRST
        total = u32_be(pdu[1:5])
        buf   = pdu[5:]
        state = REASM
    elif ctrl in (0x7E, 0x3B) and state == REASM:
        buf += pdu[1:]
        if ctrl == 0x3B:
            assert len(buf) == total
            deliver(buf)
            state = IDLE
            buf   = []
    else:
        # unknown ctrl, or continuation outside REASM → drop and reset
        state = IDLE
        buf   = []
```

`deliver(body)` then splits off the 2-byte TYPE, dispatches on the
TYPE table, and parses the remaining bytes as the matching protobuf
message.

A 5-second idle timeout is recommended: if no frame arrives for 5 s
while `state == REASM`, drop the in-flight buffer and surface a
timeout error to the caller.


## OTA flow — practical recipe

High-level pseudocode for uploading a RAUC bundle `foo.raucb`:

```
1. Pre-compute caibx + chunks on server:
     casync make --store foo.castr foo.caibx foo.raucb
   (server-side, out of scope for the app — the app downloads the
   artefacts from your backend)

2. Upload the caibx:
     OtaBegin  {filename="rootfs.caibx", total_size=S, chunk_size=C}
     → OtaBeginAck
     for seq, chunk in enumerate(split(caibx_bytes, C)):
         OtaChunk {seq, data=chunk}
         → OtaChunkAck(seq, ok=true)   # MUST wait before next
     OtaEnd {}
     → OtaEndAck
     → MissingChunks {content=<newline list of ab/hash.cacnk paths>}

3. For each path in MissingChunks.content:
     download the .cacnk from your backend
     OtaBegin  {filename=path, total_size=S, chunk_size=C}
     … same Chunk/End dance …

4. Install:
     OtaInstall {sha256=<expected SHA-256 of .raucb>}
     → OtaInstallReply(ok=true | error)
```

Recommendations:

- **Window size = 1.** Send the next `OtaChunk` only after the
  previous `OtaChunkAck`. The daemon does not tolerate out-of-order
  chunks. Pipelining is possible but risks cancellation if any ACK is
  lost.
- **Progress UI.** `sent_chunks / ceil(total_size / chunk_size)` per
  file, and count files as `1 + len(MissingChunks)`.
- **Chunk size.** 512 B is a reasonable default. Larger saves
  framing overhead but increases retransmit cost on packet loss.
  Must be ≤ 2 MB (daemon's reassembly buffer cap).
- **Retry on error.**
  - `OtaBeginAck.ok == false` → abort the file, surface the error.
  - `OtaChunkAck.ok == false` → retry that chunk once; if it fails
    again, restart the file with a fresh `OtaBegin`.
  - Connection drops mid-transfer → reconnect, restart the current
    file from chunk 0 (`/tmp` survives as long as the daemon does; it
    does **not** survive a board reboot).
- **Reboot.** After `OtaInstallReply.ok == true`, the board will
  reboot into the newly written slot. Expect the BLE link to drop.
  Your app should reconnect and confirm the new `VersionReply`.


## Smoke test (before writing the full client)

```
1. Connect.
2. Enable TX notify.
3. Write RX:   7C 00000002 0020           (GetVersion)
4. Expect TX:  7C xxxxxxxx 0021 0A LL …   (VersionReply)
5. Write RX:   7C 00000004 00010801       (Ping nonce=1)
6. Expect TX:  7C 00000004 00020801       (Pong nonce=1)
7. Disconnect.
```

If both round-trips succeed, your GATT + framing + TYPE decoding is
correct and the rest of the work is just wiring up the OTA state
machine.


## Gotchas observed in practice

- **iOS silent write drops.** See "iOS" section above — always gate
  on `peripheralIsReady(toSendWriteWithoutResponse:)` for bulk sends.
- **Android GATT queue serialization.** One outstanding GATT op at a
  time on older devices. If you interleave reads, writes, and
  descriptor writes without awaiting callbacks, only the first
  completes and the rest are silently lost.
- **Enabling notify before discovering services.** Some stacks NPE.
  Always wait for `didDiscoverCharacteristicsFor` / `onServicesDiscovered`.
- **Pairing popup.** Should never appear. If it does, the client is
  likely requesting encryption on one of the characteristics; make
  sure you did not accidentally bond the device in system settings.
  If the system already cached a bond (from an earlier attempt),
  remove it: on iOS "Forget this device", on Android "Unpair".
- **Reconnect after reboot.** Do not rely on stable Bluetooth
  addresses. Re-scan by name or service UUID after every disconnect.
