# Certabo BLE compatibility test, 2026-10-25

Target: esp32-c3-supermini. Diagnostic beta published through the web installer. Not a confirmed fix.
Firmware application image only; flash at 0x10000 using matching bootloader and partitions.

The old v11 and Spectrum builds use the same normal connection call with an
immediate MTU exchange. The supplied log shows discovery followed by five
failed connect calls, before GATT discovery and LED protocol detection.
No root cause is confirmed by the existing logs.

This build retains the normal path and adds one same-address retry without
initial MTU exchange only after a failed normal connection. It records numeric
and textual BLE errors, elapsed times, actual MTU, and separate discovery and
subscription failures. Log marker: ble-compat-20261025.

SuperMini build passed: flash 731274 bytes, RAM 79444 bytes.
Hardware connection and MTU23 transport behavior remain to be tested.
No changes to LED mapping, calibration, King/Phoenix cable or other board drivers.
Backup: backups/certabo_ble_compat_20261025.

Use the beta installer, reproduce the connection failure and export the gateway log. Both normal and compatibility attempts record their error codes. The successful Spectrum path is retained. Stable v11 and previous beta remain available. No freeze.
