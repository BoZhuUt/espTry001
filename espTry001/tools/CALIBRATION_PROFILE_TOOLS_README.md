# Calibration Profile Tools

These tools import the mobile-app device JSON library into the normalized calibration Schema without modifying the source files.

## Project rules

`config/calibration_import_rules.json` records repeatable project decisions:

- exclude `50316.json`;
- exclude PN 56600;
- use `56377_New.json` for PN 56377;
- use PN 57769, 50661, and 53607 as the pilot set.

## Pilot import

Dry-run and write only the normalized pilot profiles:

```powershell
python tools/import_calibration_profiles.py --pilot --write `
  --report-base reports/calibration_import_report
```

Outputs:

- `config/calibration_profiles/*.json`: normalized profiles;
- `reports/calibration_import_report.json`: machine-readable audit;
- `reports/calibration_import_report.md`: human-readable audit.

The importer marks profiles as:

- `approved`: safe for the bundle;
- `draft`: structurally valid but requires result-outcome review;
- `blocked`: contains an unsupported or ambiguous protocol layout.

Importing a structurally valid profile no longer implies approval. Every new
profile records a `review` checklist and remains `draft` until command
addresses, values, and waits have been reviewed. Input ranges, result mappings,
and version conditions require review when the profile uses those features.
Import findings are retained in `audit_issues` so unsupported or ambiguous
source semantics cannot be hidden by changing only `release_status`.

The original files under `devices/devices` are opened read-only and are never rewritten.

## Full-library audit

Run a dry audit without writing normalized profiles:

```powershell
python tools/import_calibration_profiles.py `
  --report-base reports/calibration_full_audit
```

A non-zero exit status is expected while duplicate PN variants remain unresolved. The report lists every conflict.

## Validate and build LittleFS data

Validate normalized profiles without writing output:

```powershell
python tools/build_calibration_bundle.py --validate-only
```

Build the LittleFS bundle:

```powershell
python tools/build_calibration_bundle.py
```

Only `approved` profiles are written to `data/calibration`. Draft and blocked profiles are reported and skipped. The generated `index.json` is a small PN-to-file index; each device profile remains a separate JSON file.

## USB hot update

Firmware with `ESPCAL1` support can replace the complete approved calibration
set without reflashing LittleFS. Put the device on **CONFIG UPLOAD**, then run:

```powershell
& "$env:USERPROFILE\.platformio\penv\Scripts\python.exe" `
  tools\upload_calibration_package.py COM3
```

The command validates `config/calibration_profiles`, builds a deterministic
package, uploads only approved Profiles, and waits for the device's final ACK.
Publishing is a complete replacement: removing approval or deleting a local
Profile removes that PN from the next active device index.

The Profile Manager also exposes **Publish to Device**, which runs the same
builder and uploader after a COM port is selected.

If an older generated profile is no longer approved, the builder stops and lists the stale file instead of deleting it automatically. Review and remove that exact generated file before rebuilding.

## Calibration Profile Manager

Build the WinForms review tool:

```powershell
tools\Build_Calibration_Profile_Manager.bat
```

Run `CalibrationProfileManager.exe` from the `tools` folder. It opens `config/calibration_profiles` automatically. You can also pass a normalized JSON file or folder on the command line.

The manager provides:

- profile list with PN, model, and release status;
- `[SENSOR]` / `[OUTPUT]` procedure classification;
- detailed input type, default, inclusive bounds, decimals, unit, and write scale;
- version conditions at procedure, step, and alternative levels;
- write address, type, fixed/input value, wait, result poll, and timeout details;
- result-register mapping table;
- editable `success`, `failure`, `pending`, and `info` outcomes;
- persistent review checks for ranges, outcomes, conditions, and command sequences;
- explicit approval blockers for duplicate PN, unsupported version conditions,
  unresolved import issues, invalid commands, and the 4+1 UI capacity;
- validation, explicit profile approval, and UTF-8 JSON save;
- unsaved-change confirmation when switching files or closing.

The default `Review` tab is intended for normal use. Select each imported
operation, read the plain-language input/command/result summary, optionally use
`Edit JSON`, and click `Confirm operation`. After every operation is confirmed,
`Approve & Save` completes the review flags, applies the approval gates, and
saves the profile. Editing an operation clears its confirmations and returns the
profile to draft.

`Delete operation` removes the selected imported operation after confirmation.
Deletion returns the profile to draft and clears all operation confirmations,
so the remaining list must be reviewed again before approval.

Review confirmations and structural edits are saved immediately as draft
checkpoints. This prevents a confirmed operation from being lost when the user
switches profiles or closes the application before final approval.

Use `Import APP JSON` to add support from a mobile-app device JSON stored in
any folder. The manager invokes the project importer, converts `Calibrations`
and `Span4_20mA`, writes a normalized draft into
`config/calibration_profiles`, reloads it, and opens it for review. If the same
profile ID already exists, replacement requires explicit confirmation because
the fresh conversion discards previous review edits. Exclusion, preferred-file,
duplicate-PN, and unsupported-command rules still apply during this import.
If the normalized profile already exists, choose `Yes` to open it without
changing prior review work, `No` to replace it with a fresh conversion, or
`Cancel` to stop.

The `Advanced` tab retains the full tree, result grid, manual review checklist,
and structural editing controls for development and exceptional corrections.

### Editing operations

Select a node in the procedure tree, then use the toolbar:

- `+ Operation`: create a sensor-calibration or 4–20mA output operation;
- `+ Input`: add an input to the selected operation (maximum three);
- `+ Step`: add a write-command step to the selected operation;
- `Edit JSON`: edit the complete selected operation, input, step, or alternative;
- `Delete`, `Up`, `Down`: remove or reorder the selected tree object;
- `+ Result`: add a result definition/mapping to the selected Alternative;
- `- Result`: remove the selected result-grid row.

Result `Value`, `Outcome`, and `Message` cells are directly editable. New
steps deliberately use address 1 and fixed value 0 as visible placeholders;
they must be edited and reviewed before approval. Every structural edit changes
the profile to `draft` and clears the affected review checks. Save, validate,
complete the review checklist, and approve again before publishing.

The manager edits normalized calibration profiles, not the original mobile-app JSON library. A profile marked `approved` still has to pass `build_calibration_bundle.py` before it can enter LittleFS data.
The builder repeats the approval checks, so manually changing
`release_status` cannot bypass the review record or unsupported-feature gate.

## Tests

```powershell
python -m unittest tools/test_import_calibration_profiles.py
python tools/test_calibration_executor_native.py
```

The tests cover version conditions, WaitOnly, MappedResult outcome classification, null communication-error mappings, input scaling, open/closed bounds, conditional address alternatives, and publish validation.

The native executor test compiles the production `src/calibration_executor.cpp`
with MinGW `g++` and substitutes only `HardwareSerial` and `millis()`. It checks
the exact FC06/FC16/CDAB/FC03 frame bytes, 1-based address conversion, command
ordering, non-blocking waits, input bounds, CRC and echo failures, Modbus
exceptions, write-response timeout semantics, pending-result polling, terminal
result mappings, unknown values, and total result timeout.

Calibration UI and execution are enabled only for the 2.8-inch `waveshare28`
environment. The 1.47-inch environment does not load calibration profiles or
show a calibration entry point.
