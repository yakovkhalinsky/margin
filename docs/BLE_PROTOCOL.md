# margin BLE protocol, version 1

Status: **draft v1**. Reference implementations:
[`firmware/lib/MarginProtocol`](../firmware/lib/MarginProtocol) (C++17) and
[`android/.../dev/margin/protocol`](../android/app/src/main/java/dev/margin/protocol) (Kotlin).
Both pass the same golden vectors (§10).

Roles: the **X4 Pro is the GATT server / peripheral**; the **Android app is the GATT client / central**.
All multi-byte integers are **little-endian**. All text is **UTF-8**.

## 1. Versioning

- `version` (u8) is carried in every message header (§4) and in HELLO. This document is version **1**.
- Additive changes (new message types, new TLV tags, new features bits) do **not** bump the version:
  receivers ignore unknown message types and unknown TLV tags.
- Changing framing, CTRL layout or the meaning of an existing tag bumps the version. A receiver that
  gets a different version NACKs with `BAD_VERSION` (§6).

## 2. GATT layout

Service UUID: **`d0ed0001-b422-469c-8266-d0abd145eb37`** (custom 128-bit; only the first group varies
between attributes).

| Characteristic | UUID | Properties | Direction | Carries |
|---|---|---|---|---|
| RX | `d0ed0002-b422-469c-8266-d0abd145eb37` | Write, Write Without Response | app → device | fragmented messages (§3–4) |
| TX | `d0ed0003-b422-469c-8266-d0abd145eb37` | Notify | device → app | fragmented messages (§3–4) |
| CTRL | `d0ed0004-b422-469c-8266-d0abd145eb37` | Read, Write, Notify | both | ACK / NACK / FILE_ACK, status (§6) |
| FILE | `d0ed0005-b422-469c-8266-d0abd145eb37` | Write Without Response | app → device | file chunks (§8) |

Every attribute requires an **encrypted, authenticated (MITM) link from a bonded peer** (§9).

The device advertises the service UUID and the name `margin` (TODO: `margin-XXXX` with the last two
MAC bytes).

## 3. Frames, MTU and fragmentation

One GATT write (RX) or notification (TX) is one **frame**:

```
frame := flags:u8  msg_id:u8  frag_idx:u16  payload…
```

| flags bit | name | meaning |
|---|---|---|
| 0x01 | FIRST | first fragment of a message (`frag_idx` must be 0) |
| 0x02 | LAST | last fragment of a message |
| 0x04 | ACK_REQ | receiver must send an ACK on CTRL after processing this frame |

- `frame_size = ATT_MTU − 3`. The app requests MTU 247 → frame size **244**. Without an MTU
  exchange the frame size is **20** (minimum supported). Frames never exceed the negotiated size.
- `msg_id` increments by one (mod 256) per message per direction. A sender must never reuse the
  immediately preceding `msg_id`; that is what lets the receiver discard a whole retransmitted
  message after a lost ACK.
- `frag_idx` counts fragments within a message, from 0. A message has at most 65,535 fragments.

## 4. Messages

The payload of fragment 0 starts with an 8-byte **message header**, followed by the first bytes of
the body; later fragments carry only body bytes.

```
msg_header := version:u8  type:u8  reserved:u16 (=0)  body_len:u32
```

With frame size F, fragment 0 carries `F − 12` body bytes and every later fragment `F − 4`.
The receiver knows the body is complete when it sees LAST and has exactly `body_len` bytes.

Limits: `body_len` ≤ `max_body` advertised by the receiver (device default **65,536**). Larger
payloads (books) go over FILE (§8).

### Message types

| Code | Name | Direction | Purpose |
|---|---|---|---|
| 0x01 | HELLO | both | versions, features, device text metrics |
| 0x02 | ERROR | both | application-level error with code and text |
| 0x03 | DEVICE_STATUS | device → app | battery, charging, free SD space |
| 0x10 | CHAT_DELTA | app → device | a piece of streamed text for one turn |
| 0x11 | CHAT_DONE | app → device | turn finished (stop / length / error / cancelled) |
| 0x12 | THINKING_STATUS | app → device | "thinking", "running tool: …", idle |
| 0x13 | QUICK_REPLIES | app → device | up to 6 tappable reply options |
| 0x14 | USER_MESSAGE | device → app | text typed or quick reply chosen on the reader |
| 0x15 | CHAT_CANCEL | device → app | stop generating the current turn |
| 0x16 | CHAT_NEW | device → app | start a new conversation |
| 0x20 | PASSAGE_SHARE | device → app | selected passage + book context + intent |
| 0x30 | FILE_OFFER | app → device | announce a file push |
| 0x31 | FILE_ACCEPT | device → app | accept / reject, resume offset |
| 0x32 | FILE_COMPLETE | app → device | all bytes sent, whole-file CRC32 |
| 0x33 | FILE_RESULT | device → app | final status and stored path |
| 0x34 | FILE_ABORT | both | cancel a transfer |
| 0x40 | SYNC_REQUEST | app → device | send bookmark/highlight/progress records changed since `seq` |
| 0x41 | SYNC_RECORDS | device → app | a batch of records |
| 0x42 | SYNC_DONE | device → app | batch finished, highest `seq` included |
| 0x43 | SYNC_ACK | app → device | app has durably stored records up to `seq` |
| 0x50 | BIBLE_CONTEXT | device → app | current Bible passage context for study chat |
| 0x51 | BIBLE_NOTE_SYNC_REQ | app → device | request Bible notes/highlights changed since `seq` |
| 0x52 | BIBLE_NOTE_RECORDS | device → app | batch of Bible note/highlight records |
| 0x53 | BIBLE_NOTE_SYNC_DONE | device → app | Bible note sync batch complete |
| 0x54 | BIBLE_NOTE_PUSH | app → device | Bible notes edited on phone |
| 0x55 | BIBLE_NOTE_ACK | device → app | acknowledges pushed Bible notes |

## 5. Reassembly rules (receiver)

1. A frame with FIRST (re)starts a message, abandoning any partial one.
2. A non-FIRST frame must match the current `msg_id` and `frag_idx == next_expected`.
   - `frag_idx < next_expected`: duplicate, ignore it (but ACK again if ACK_REQ or LAST is set).
   - `frag_idx > next_expected`: gap. NACK `OUT_OF_ORDER` with `next_frag = next_expected` and keep
     the partial message so the sender can go back.
   - wrong `msg_id` or no message in progress: NACK `OUT_OF_ORDER`.
3. Any frame for the *last completed* `msg_id` while idle is a duplicate (lost ACK): ignore it and ACK
   again. It is never delivered twice.
4. On LAST the body length must equal `body_len`, otherwise NACK `BAD_LENGTH`.
5. After LAST the receiver always ACKs, with `next_frag = fragment count`.

## 6. CTRL: acknowledgements and flow control

CTRL packets are never fragmented. The device **notifies** CTRL to acknowledge RX/FILE traffic; the
app **writes** CTRL to acknowledge TX traffic. Reading CTRL returns the device STATUS.

```
ACK      := 0x01  msg_id:u8  next_frag:u16  credits:u8               (5 bytes)
NACK     := 0x02  msg_id:u8  reason:u8  next_frag:u16                (5 bytes)
FILE_ACK := 0x03  transfer_id:u16  next_offset:u32  credits:u8       (8 bytes)
STATUS   := 0x10  version:u8  max_frame:u16  window:u8  flags:u8  max_body:u32   (10 bytes, read)
```

STATUS flags: 0x01 bonded, 0x02 busy, 0x04 pairing window open.

NACK reasons: 1 BAD_VERSION, 2 OUT_OF_ORDER, 3 TOO_LARGE, 4 BAD_LENGTH, 5 BUSY, 6 UNSUPPORTED, 7 MALFORMED.

**Flow control is a go-back-N window counted in fragments.**

- The window `W` starts at 8 (or STATUS.window); each ACK's `credits` replaces it.
- The sender may have at most `W` unacknowledged fragments in flight. It sets ACK_REQ on the frame
  that fills the window and always on LAST.
- ACK `next_frag = n` acknowledges every fragment below `n`. Stale ACKs, or ACKs beyond what was sent,
  are ignored.
- NACK `OUT_OF_ORDER`: rewind and resend from `next_frag`.
- No ACK within **2 s** of the last progress: rewind to the last acknowledged fragment and resend.
- One message in flight per direction. Senders queue messages above this layer.

Why both Write Without Response and ACKs: write-without-response gives throughput; the explicit
window bounds memory on the device (frames go through a FreeRTOS queue of 24 entries > W), and the
same scheme works for notifications, which have no per-packet confirmation at all.

## 7. Message bodies (TLV)

Bodies are a sequence of `tag:u8 len:u16 value[len]`. Unknown tags must be skipped. A known tag with
the wrong length makes the message malformed. Tags are global:

| Tag | Name | Type | | Tag | Name | Type |
|---|---|---|---|---|---|---|
| 0x01 | CONV_ID | u32 | | 0x17 | RECORD | nested TLV, repeatable |
| 0x02 | TURN_ID | u32 | | 0x18 | RECORD_KIND | u8: 0 bookmark, 1 highlight, 2 progress |
| 0x03 | TEXT | utf8 | | 0x19 | RECORD_ID | u32 |
| 0x04 | ROLE | u8: 0 user, 1 assistant, 2 system, 3 tool | | 0x1A | SEQ | u32 |
| 0x05 | STATUS | u8: 0 idle, 1 thinking, 2 tool running, 3 streaming, 4 error | | 0x1B | TIMESTAMP | u32 unix s |
| 0x06 | LABEL | utf8 | | 0x1C | NOTE | utf8 |
| 0x07 | OPTION | utf8, repeatable, max 6 | | 0x1D | DELETED | u8 bool |
| 0x08 | FINISH | u8: 0 stop, 1 length, 2 error, 3 cancelled | | 0x1E | ERROR_CODE | u16 |
| 0x09 | INTENT | u8: 0 ask, 1 explain, 2 summarise selection, 3 summarise chapter, 4 define, 5 translate | | 0x20 | PROTO_VERSION | u8 |
| 0x0A | BOOK_ID | utf8 (content fingerprint) | | 0x21 | NAME | utf8 |
| 0x0B | BOOK_TITLE | utf8 | | 0x22 | VERSION | utf8 |
| 0x0C | CHAPTER_TITLE | utf8 | | 0x23 | FEATURES | u32: 1 chat, 2 passage, 4 file push, 8 sync |
| 0x0D | SPINE | u16 | | 0x24 | COLUMNS | u16 wrap hint |
| 0x0E | OFFSET | u32 | | 0x25 | ROWS | u16 |
| 0x0F | PERCENT | u16 basis points (0–10000) | | 0x26 | BATTERY | u8 % |
| 0x10 | TRANSFER_ID | u16 | | 0x27 | CHARGING | u8 bool |
| 0x11 | FILE_NAME | utf8, bare name | | 0x28 | SOURCE | u8: 0 keyboard, 1 quick reply, 2 button |
| 0x12 | FILE_SIZE | u32 | | 0x29 | OPTION_INDEX | u8 |
| 0x13 | CRC32 | u32 (CRC-32/ISO-HDLC, as zlib) | | 0x2A | FREE_KB | u32 |
| 0x14 | FORMAT | utf8: epub, fb2, md, txt, html | | 0x2B | MAX_BODY | u32 |
| 0x15 | RESULT | u8: 0 ok, 1 rejected, 2 no space, 3 CRC mismatch, 4 I/O error, 5 aborted, 6 bad name | | 0x2C | RESUME_OFFSET | u32 |
| 0x16 | PATH | utf8 | | 0x60 | BIBLE_REF | utf8, OSIS reference (e.g. "JHN.3.16") |
| | | | | 0x61 | BIBLE_BOOK_NAME | utf8, full book name (e.g. "John") |
| | | | | 0x62 | BIBLE_CHAPTER | u16, 1-based chapter number |
| | | | | 0x63 | BIBLE_VERSE | u16, 1-based verse number |
| | | | | 0x64 | BIBLE_NOTE_TYPE | u8: 0 highlight, 1 note |
| | | | | 0x65 | BIBLE_HIGHLIGHT_COLOR | u8: 0 yellow, 1 green, 2 blue, 3 pink |
| | | | | 0x66 | BIBLE_NOTE_RECORD | nested TLV, repeatable |

Required (R) and optional (O) tags per message:

| Message | Tags |
|---|---|
| HELLO (app) | PROTO_VERSION R, NAME, VERSION, FEATURES |
| HELLO (device) | PROTO_VERSION R, NAME, VERSION, FEATURES, COLUMNS, ROWS, MAX_BODY |
| ERROR | ERROR_CODE R, TEXT O |
| DEVICE_STATUS | BATTERY, CHARGING, FREE_KB |
| CHAT_DELTA | CONV_ID R, TURN_ID R, ROLE O (default assistant), TEXT R |
| CHAT_DONE | CONV_ID R, TURN_ID R, FINISH O (default stop) |
| THINKING_STATUS | CONV_ID O, STATUS R, LABEL O |
| QUICK_REPLIES | CONV_ID R, TURN_ID R, OPTION ×0–6 (extra options ignored) |
| USER_MESSAGE | CONV_ID R, SOURCE R, TEXT R, OPTION_INDEX O |
| CHAT_CANCEL | CONV_ID R, TURN_ID R |
| CHAT_NEW | CONV_ID O (app assigns if absent) |
| PASSAGE_SHARE | INTENT R, BOOK_ID O, BOOK_TITLE O, CHAPTER_TITLE O, SPINE O, OFFSET O, PERCENT O, TEXT R |
| FILE_OFFER | TRANSFER_ID R, FILE_NAME R, FILE_SIZE R, CRC32 O, FORMAT O |
| FILE_ACCEPT | TRANSFER_ID R, RESULT R, RESUME_OFFSET O |
| FILE_COMPLETE | TRANSFER_ID R, CRC32 R |
| FILE_RESULT | TRANSFER_ID R, RESULT R, PATH O |
| FILE_ABORT | TRANSFER_ID R, ERROR_CODE O |
| SYNC_REQUEST | SEQ R (0 = everything) |
| SYNC_RECORDS | RECORD ×n, each: RECORD_ID R, RECORD_KIND R, SEQ R, BOOK_ID R, BOOK_TITLE O, SPINE O, OFFSET O, PERCENT O, TEXT O (highlight excerpt), NOTE O, TIMESTAMP R, DELETED O |
| BIBLE_CONTEXT | BIBLE_REF R, BIBLE_BOOK_NAME O, BIBLE_CHAPTER R, BIBLE_VERSE O, TEXT R (verse text) |
| BIBLE_NOTE_SYNC_REQ | SEQ R (0 = everything) |
| BIBLE_NOTE_RECORDS | BIBLE_NOTE_RECORD ×n, each: BIBLE_REF R, BIBLE_NOTE_TYPE R, BIBLE_HIGHLIGHT_COLOR O, NOTE O (text), TIMESTAMP R, SEQ R, DELETED O |
| BIBLE_NOTE_SYNC_DONE | SEQ R (highest included) |
| BIBLE_NOTE_PUSH | BIBLE_NOTE_RECORD ×n (notes edited on phone) |
| BIBLE_NOTE_ACK | SEQ R (highest acknowledged) |
| SYNC_DONE | SEQ R |
| SYNC_ACK | SEQ R |

### Chat semantics

- The app owns conversations. `CONV_ID` is chosen by the app (CHAT_NEW from the device asks for a new
  one). Device-originated turns use `TURN_ID ≥ 0x80000000` so they never collide with the app's.
- CHAT_DELTA appends `TEXT` to turn `TURN_ID`, creating it on first sight. After CHAT_DONE a turn is
  immutable. The app should coalesce LLM tokens into ~100–200 byte deltas (or every ~300 ms); the
  e-ink panel cannot show faster updates anyway.
- **Pre-wrapped text.** The app cleans text for e-ink before sending: strips Markdown it cannot
  render, turns lists into `• ` lines, collapses blank lines, and hard-breaks tokens longer than the
  device's `COLUMNS` hint. Exact line breaking stays on the device, because the reader uses
  proportional fonts whose widths only it knows.
- QUICK_REPLIES belong to the latest assistant turn. A new turn clears them.
- USER_MESSAGE from the device is echoed in the reader's transcript immediately. The app does not
  send it back as a CHAT_DELTA.

### Passage share

Sent when the user selects text in the Reader and picks Explain / Summarise / Define / Translate /
Ask. `SUMMARISE_CHAPTER` sends the chapter text, truncated by the device to `max_body` minus
overhead. The app answers in the normal chat flow (CHAT_DELTA…CHAT_DONE) in the current conversation.

## 8. File push (app → device)

1. App → FILE_OFFER (name, size, CRC32, format).
2. Device validates: bare name (no `/ \ :`, no `..`, no leading dot, ≤ 96 bytes), allowed extension
   (`.epub .fb2 .md .txt .html .htm`), enough free space. It replies FILE_ACCEPT with RESULT=ok and
   RESUME_OFFSET (non-zero if a matching `.part` file exists), or a rejection code.
3. App writes chunks to **FILE**: `transfer_id:u16 offset:u32 data…` (6-byte header, up to
   `frame_size − 6` data bytes). The device acknowledges with CTRL **FILE_ACK** (`next_offset`,
   `credits` = chunks allowed in flight). Gaps or a timeout → resend from `next_offset`.
4. App → FILE_COMPLETE with the whole-file CRC32. The device verifies it, renames
   `/margin/inbox/<name>.part` → `/margin/inbox/<name>`, and replies FILE_RESULT (with PATH).
5. Either side may FILE_ABORT. The device deletes `.part` files older than 7 days.

The device never writes outside `/margin/inbox/`, and files are never executed or parsed until the
user opens them from the Library.

## 9. Pairing, bonding and security

- **LE Secure Connections only**, with **bonding** and **MITM protection**. The device's IO capability
  is DisplayYesNo, so pairing uses **Numeric Comparison**: the reader shows the 6-digit code and the
  user confirms on both screens.
- **Bonded peers only.** Every characteristic requires an encrypted + authenticated link
  (`*_ENC | *_AUTHEN`). New bonds are accepted only while the user has opened **Settings → Pair phone**
  (pairing window, 60 s). Outside that window pairing requests are rejected. If a link finishes
  security without being bonded, encrypted and authenticated, the device disconnects.
- Planned (M2): when the pairing window is closed, advertise with a connect filter accept list of
  bonded identity addresses, and use resolvable private addresses.
- **No debug or settings-write commands over BLE.** The protocol has no message that changes device
  settings, injects input, or reads arbitrary files. (Cautionary example: Explorink's 0.2.0-alpha
  firmware let anyone in Bluetooth range write settings and inject button presses; they removed it in
  0.2.1.)
- Secrets stay on the phone. API keys and LLM traffic never cross BLE; the reader never needs Wi-Fi or
  TLS for chat.
- "Forget phone" on the device deletes all bonds.

## 10. Golden vectors

CHAT_DELTA, `conv_id=1`, `turn_id=2`, `role=assistant`, `text="Hi"`, `msg_id=7`, frame size 20, window 8:

```
body (23 bytes)  01 0400 01000000  02 0400 02000000  04 0100 01  03 0200 4869
frame 0          01 07 0000 | 01 10 0000 17000000 | 0104000100000002
frame 1          06 07 0100 | 040002000000040100010302004869
```

Frame 0 has flags FIRST, a message header (v1, type 0x10, body_len 23) and 8 body bytes. Frame 1 has
flags LAST|ACK_REQ and the remaining 15 bytes. CRC-32 check value: `crc32("123456789") = 0xCBF43926`.

Checked by `firmware/test` (`golden_chat_delta_frames`) and `android/app/src/test` (`GoldenVectorsTest`).

## 11. Open items

- Outbound queues on both sides (today: one message in flight, `send()` refuses while busy).
- Sync direction app → device (edit notes on the phone), and conflict rules.
- Whether to use 2M PHY / connection-interval tuning for file push throughput vs battery.
- Reconnect + resume of an interrupted file transfer across disconnects (RESUME_OFFSET is specified,
  not implemented).
