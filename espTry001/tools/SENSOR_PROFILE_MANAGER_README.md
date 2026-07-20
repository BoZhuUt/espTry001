# Sensor Profile Manager

`SensorProfileManager.exe` is the visual editor for `config/sensor_profiles.json`.

## Typical workflow

1. Double-click `SensorProfileManager.exe`.
2. The project `config/sensor_profiles.json` is opened automatically when the EXE remains in the `tools` directory.
3. Select a product on the left.
4. Use **Duplicate** when a new PN shares the same registers with an existing product, then change its PN and name.
5. Use **Add** for a new empty profile, or **Delete** to remove support for a PN.
6. Edit fields in the table and update **Config version**.
7. Click **Validate**, then **Save**.
8. Open `SensorProfileUpdater.exe` and upload the saved JSON to the device.

## Language

Use the **Language / 语言 / Idioma** selector on the toolbar. The editor supports:

- 中文
- English
- Español

The selection updates the interface, validation messages, and the language of the generated PDF report. Protocol identifiers such as PN, FC03, CDAB, `float32`, and `uint16` remain unchanged.

## PDF report

Every successful **Save** or **Save As** writes two files in the same directory with the same base name:

```text
sensor_profiles.json
sensor_profiles.pdf
```

The PDF contains the configuration version, supported PN/name list, field and register totals, and a detailed table for every profile. A `float32` field is shown as a two-register range, while a `uint16` field is shown as one register.

PDF pages are rendered with Windows fonts and embedded as images so Chinese characters and Spanish accents display consistently on other computers.

The editor enforces the current firmware limits: 1-32 profiles, 1-16 fields per profile, FC03, 1-based addresses, and `uint16` or `float32` with CDAB word order.

To rebuild the program after changing its C# source, run `Build_Profile_Manager.bat`.
