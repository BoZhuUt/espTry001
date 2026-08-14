# Calibration Profile Hot Update Plan

## 1. Goal

Allow approved calibration and 4–20 mA profiles to be added, replaced, or
removed through USB without rebuilding firmware or running PlatformIO
`uploadfs`.

The intended operator workflow is:

1. Import a mobile APP device JSON in `CalibrationProfileManager.exe`.
2. Review, confirm, and approve the normalized operations.
3. Click **Publish to Device** and select the device COM port.
4. The tool builds a complete approved calibration bundle and uploads it.
5. The device validates and installs the bundle, restarts, and discovers the
   connected probe again.

The first implementation may still require the operator to open **CONFIG
UPLOAD** on the device. Automatic entry into update mode is a later phase.

## 2. Runtime dependency rule

Master display support and calibration support must remain independent after a
probe PN has been discovered:

```text
Probe discovery -> product number
                    |
                    +-- Sensor Profile found -> live parameter polling/display
                    |
                    +-- Calibration Profile found -> CAL & OUTPUT entry
```

Entering calibration requires:

- MASTER mode;
- a successfully discovered slave address and PN;
- an approved Calibration Profile for that PN.

It must not require a matching Sensor Profile. If only calibration support is
available, the MASTER UI should show the discovered PN and a concise “no live
parameter profile” message while keeping CAL & OUTPUT available.

## 3. Publishing model

Publishing is a complete desired-state replacement, not a sequence of remote
add/delete operations.

- Only `release_status: approved` profiles enter the package.
- Draft, blocked, duplicate-PN, unsupported-version, unconfirmed, or invalid
  profiles block or are excluded according to the existing bundle validator.
- The package contains a generated index and every approved profile referenced
  by that index.
- A new package replaces the previous calibration set. A PN removed from the
  approved set is therefore removed from device support after publication.
- Sensor Profile data is not changed by a calibration publication.

This keeps the PC project directory as the source of truth and prevents stale
profiles from accumulating on the device.

## 4. USB protocol direction

Reuse the existing CONFIG UPLOAD transport and its proven block-level USB CDC
handshake, CRC, timeout, and restart behavior. Extend it so the receiver can
identify the payload target:

- Sensor Profile update: retain compatibility with existing `ESPCFG1`.
- Calibration bundle update: use the versioned `ESPCAL1` package type.

The final wire format will be selected after auditing `SensorProfileUpdater`.
Compatibility requirements:

- Existing `SensorProfileUpdater.exe` and `ESPCFG1` uploads keep working.
- Payload type is explicit; calibration data can never overwrite
  `/sensor_profiles.json`.
- Header and payload lengths are bounded before allocation or file creation.
- Each transferred payload is covered by CRC32.
- Unknown package types and versions receive a clear NAK.

Selected `ESPCAL1` framing:

```text
Outer header:
  "ESPCAL1"             7 bytes
  package_length         uint32 little-endian
  package_crc32          uint32 little-endian

Package payload:
  "CALPKG1"             7 bytes
  file_count             uint16 little-endian
  repeated entries:
    kind                 uint8 (1=profile, 2=index)
    path_length          uint8
    payload_length       uint32 little-endian
    payload_crc32        uint32 little-endian
    ASCII path           path_length bytes
    file payload         payload_length bytes
```

Profile entries come first and `/calibration/index.json` is always the final
entry. Profile filenames contain PN and content CRC, for example
`/calibration/p10009-1a2b3c4d.json`. The active index is therefore unchanged
until every versioned profile has arrived and passed validation.

## 5. Safe LittleFS installation

The receiver must never edit the active calibration files in place.

1. Receive into staging files.
2. Verify transfer length and CRC.
3. Validate UTF-8, JSON structure, schema version, generated index, PN/path
   consistency, operation limits, and every referenced profile.
4. Commit only after the entire candidate set is valid.
5. On failure or interruption, discard staging and continue using the previous
   set.
6. After a successful commit, remove obsolete calibration files and restart.

The implementation should prefer a manifest/index commit point or another
recoverable two-phase scheme. The exact rename/backup sequence must be checked
against the current `SensorProfileUpdater` behavior and LittleFS rename
semantics before coding.

## 6. PC-side publish artifact

Extend `tools/build_calibration_bundle.py` with a deterministic publish output
that contains:

- package format/version;
- generation identifier;
- approved profile count;
- generated `index.json`;
- profile paths, lengths, and CRC values;
- the normalized profile payloads.

The same builder is used by the CLI uploader and the GUI so they cannot publish
different content.

Before upload, show a summary:

- added PN;
- updated PN;
- removed PN;
- unchanged PN;
- package size and validation result.

## 7. Implementation phases

### Phase A — protocol and storage audit

- Document the existing `ESPCFG1` state machine, limits, ACK/NAK behavior,
  temporary/backup files, and recovery rules.
- Check LittleFS capacity and practical package limits.
- Choose the calibration package framing and atomic commit sequence.
- Add protocol-format and failure-mode details to this document.

Acceptance: the new format can represent additions, replacements, and deletions
without ambiguity and cannot target unrelated LittleFS paths.

### Phase B — bundle builder

- Generate the complete approved calibration publish artifact.
- Make ordering and CRC output deterministic.
- Reject unsafe filenames, duplicate PN, unsupported schema, and UI limits.
- Add unit tests for zero, one, ten-operation, duplicate, deletion, corruption,
  and oversized cases.

Acceptance: identical approved inputs produce identical package bytes.

### Phase C — firmware receiver

- Extend CONFIG UPLOAD to recognize calibration packages.
- Stream payloads to staging rather than holding the complete bundle in RAM.
- Validate all candidate files using the same limits as
  `CalibrationProfileStore`.
- Commit or roll back atomically and expose useful progress/error text.
- Restart only after successful installation.

Acceptance: valid update succeeds; CRC failure, timeout, invalid JSON, invalid
index, power interruption, and insufficient space leave the previous bundle
usable.

### Phase D — command-line uploader

- Add a calibration upload command using the new protocol.
- Auto-build the approved bundle before transfer.
- Print publish summary, progress, terminal result, and restart status.
- Add repeat/retry/corruption tests based on `test_espcfg1.py`.

Acceptance: additions, modifications, and removals are confirmed on a real
device after rediscovery.

### Phase E — Calibration Profile Manager integration

- Add **Publish to Device**.
- Select/refresh COM port and remember the last valid choice.
- Run the common builder and show blockers before opening the port.
- Show additions/updates/removals and require confirmation before replacing the
  device calibration set.
- Display upload progress and the device’s final result.

Acceptance: the normal workflow requires no command line or PlatformIO.

### Phase F — MASTER/calibration decoupling

- Track Sensor Profile availability and Calibration Profile availability
  separately.
- Allow CAL & OUTPUT for a discovered PN with calibration support even when no
  Sensor Profile exists.
- Preserve current behavior when both profiles exist.
- Rediscover and reload both stores after an update/restart.

Acceptance: all four combinations of the two profile types render and behave as
defined in Section 2.

### Phase G — optional automatic update entry

- Define a safe idle USB command that requests CONFIG UPLOAD without a touch.
- Reject it while calibration, logging transfer, or another exclusive USB
  operation is active.
- Let the GUI request update mode, wait for readiness, and upload in one action.

Acceptance: one PC-side Publish action completes the update, while accidental
serial traffic cannot enter update mode.

## 8. Non-goals for the first release

- Wi-Fi/cloud OTA delivery.
- Editing individual active files remotely.
- Publishing draft or locally unreviewed operations.
- Changing firmware, partition tables, or the Sensor Profile bundle during a
  calibration-only update.
- Supporting the 1.47-inch display calibration UI.

## 9. Verification checklist

- Existing `ESPCFG1` Sensor Profile update regression passes.
- Calibration package installs on a freshly formatted LittleFS device.
- Adding one approved PN makes it available after rediscovery.
- Updating a PN changes its operations and input ranges.
- Removing a PN removes its calibration entry after publication.
- A calibration-only PN can enter CAL & OUTPUT without live parameter display.
- Draft and blocked Profiles never reach the device.
- Duplicate PN publication is rejected.
- Corrupt header, CRC, truncated payload, malformed JSON, invalid index, full
  filesystem, disconnect, and restart during transfer preserve the old set.
- Repeated installation of the same bundle is safe.

## 10. Current implementation status

- [x] Planning and dependency rules documented.
- [x] Existing `ESPCFG1` receiver audited and retained for compatibility.
- [x] Deterministic `CALPKG1` builder implemented with versioned Profile paths.
- [x] `ESPCAL1` CONFIG UPLOAD receiver implemented.
- [x] Candidate index and every referenced Profile are validated before commit.
- [x] Missing active index is restored from backup after an interrupted commit.
- [x] Command-line build/upload tool implemented.
- [x] Calibration-only PN UI no longer requires a Sensor Profile.
- [x] Real-device ESPCAL1 upload and rediscovery test.
- [ ] Corruption, retry, removal, and interrupted-transfer device tests.
- [x] Replace the formal Profile Manager EXE with the Publish button build.
- [x] One-click GUI publish device test.
