# Changelog

## Airlink-V2.0.0

Internal build: R27.6.6.23.

- Activated the C906L core for an independent LVGL display, touch, ADC and CH347 control plane.
- Rebuilt the round-screen UI, screen saver, battery display and CH347 pin reference.
- Added IPC protocol v1 ABI4 with firmware identity, CRC, generation and heartbeat checks.
- Added phone hotspot provisioning, captive portal and atomic Wi-Fi profile validation.
- Reworked VirtualHere lifecycle, connection-state reporting and network diagnostics.
- Increased AIC8800 SDIO request to 50 MHz with approximately 46.875 MHz actual clock.
- Added safe 46.875 MHz GC9A01 refresh pacing and a full-screen RGB565 buffer.
- Added a slim release RootFS without SSH, Telnet or adbd.
- Integrated the complete SG2002 SDK and reproducible WSL2 build workflow in one repository.

## Airlink-V1.0.0

- Original public AirLink source and image release.
- Retained as the historical rollback version.
