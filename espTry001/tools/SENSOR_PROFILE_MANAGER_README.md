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

The editor enforces the current firmware limits: 1-32 profiles, 1-16 fields per profile, FC03, 1-based addresses, and `uint16` or `float32` with CDAB word order.

To rebuild the program after changing its C# source, run `Build_Profile_Manager.bat`.
