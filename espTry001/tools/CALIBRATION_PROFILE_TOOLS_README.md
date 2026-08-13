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

If an older generated profile is no longer approved, the builder stops and lists the stale file instead of deleting it automatically. Review and remove that exact generated file before rebuilding.

## Calibration Profile Manager

Build the WinForms review tool:

```powershell
tools\Build_Calibration_Profile_Manager.bat
```

Run `CalibrationProfileManager.exe` from the `tools` folder. It opens `config/calibration_profiles` automatically. You can also pass a normalized JSON file or folder on the command line.

The first version provides:

- profile list with PN, model, and release status;
- procedure/input/step tree;
- result-register mapping table;
- editable `success`, `failure`, `pending`, and `info` outcomes;
- validation, explicit profile approval, and UTF-8 JSON save;
- unsaved-change confirmation when switching files or closing.

The manager edits normalized calibration profiles, not the original mobile-app JSON library. A profile marked `approved` still has to pass `build_calibration_bundle.py` before it can enter LittleFS data.

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
