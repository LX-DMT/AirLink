# AirLink software architecture

## 1. System overview

AirLink uses both RISC-V cores in the SG2002:

- **C906L bare metal:** round display, touch, battery ADC, GPIOA_29 and local
  CH347 control.
- **C906 Linux:** Wi-Fi, DHCP, phone provisioning, VirtualHere and diagnostics.

The C906L does not depend on Linux scheduling. The UI, touch, voltage display
and CH347 remain responsive while Linux boots, changes network mode or restarts
services. The cores exchange versioned, CRC-protected commands and state through
reserved shared memory. The target Wi-Fi password is never sent over IPC.

## 2. Boot flow

```text
BootROM
→ FSBL/BL2 initializes DDR and clocks
→ OpenSBI/U-Boot loads the Linux FIT
→ BLCP_2ND starts the C906L firmware
→ Linux Kernel, DTB and ramdisk start
→ udev and AIC8800 modules load
→ S29airlinkd starts and completes the IPC handshake
→ GPIOA_29 selects the wired or wireless state machine
```

The C906L binary is stored as `BLCP_2ND` in FIP and runs at `0x8fe00000`.
The Linux Kernel, DTB and ramdisk are stored in the `boot.sd` FIT. They are
rebuilt independently and assembled into the FAT16 boot partition.

## 3. C906L firmware

The firmware ID is `R27P`. Major modules are:

- `display.c`: GC9A01 setup, address windows and SPI flushing.
- `touch.c`: CST816T reads, taps, swipes and debounce.
- `adc1.c`: trimmed 16-sample mean, median of three batches and four-bar
  battery presentation.
- `ch347.c`: MODE0–3, DTR1/RTS1 outputs and non-blocking 80 ms reset.
- `airlink_ui.c`: wired/wireless pages, provisioning notice, pinout page and
  30-second screen saver.
- `ipc_smoke.c`: shared-memory initialization, CRC, generation, heartbeat and
  command rings.

The display uses one `240×240 RGB565` framebuffer. Page transitions run at a
20 ms cadence for 240 ms, local spinners at 16 ms and the screen saver at
33 ms. A full-screen flush is submitted as one transfer to avoid the seams and
ghosting associated with four 60-line buffers.

## 4. Linux userspace

`airlinkd` is a C11 daemon that exclusively owns:

- `wlan0`, `wpa_supplicant` and BusyBox `udhcpc`;
- `hostapd`, `dnsmasq` and the embedded captive portal;
- the VirtualHere process, TCP 7575 listener and client detection;
- GPIOA_29, shared-memory IPC and `/run/airlinkd.sock`;
- `airlinkctl status/diag/wifi forget`.

Linux identifies as `LN27`. The release profile keeps serial root access and
`airlinkctl`, but does not start SSH, Telnet or adbd. A STA password is only
present in the mode-0600 configuration and temporary child-process input. It is
excluded from IPC, logs, status JSON, diagnostics and the display.

## 5. IPC protocol v1 ABI4

The common definition is `airlink/ipc/airlink_ipc_v4.h`. The `0x380`-byte
layout contains bidirectional message rings, command results, a Linux state
snapshot, a 128-byte provisioning block, firmware IDs, CRC32, generation and
heartbeat fields.

A writer publishes an odd generation, updates payload and CRC, then publishes
an even generation. A reader accepts a snapshot only if two generation reads
match, the value is even and the CRC is valid. Linux requires peer ID `R27P`
and ABI4:

```text
IPC peer=R27P abi=4 PASS
SELFTEST PASS
```

An unknown ABI or firmware mismatch stops hardware control rather than allowing
the two cores to interpret different layouts.

## 6. GPIOA_29 mode machine

GPIOA_29 is accepted after 200 ms of stable debounce. C906L switches to the
matching home page immediately while Linux performs service work in the
background.

```text
Wired:
MODE_SWITCHING
→ terminate hostapd/dnsmasq/wpa_supplicant/udhcpc/VirtualHere in parallel
→ WIRED_READY

Wireless:
MODE_SWITCHING
→ prepare wlan0
→ WIRELESS_WAIT_LINK or WIRELESS_PROVISIONING
→ connect Wi-Fi and start VirtualHere
```

The UI uses inline progress rather than a blocking full-screen dialog. Entering
wired mode force-closes the provisioning overlay and invalidates stale session
state.

## 7. Phone provisioning and captive portal

With no saved network, provisioning starts automatically. It can also be
started from the Wi-Fi page.

```text
SSID: AirLink-XXXX
Password: 12345678
Address: 192.168.4.1/24
Channel: 1, 20 MHz
Security: WPA2-CCMP
```

The state machine is:

```text
IDLE → SCANNING → AP_STARTING → AP_READY
→ SUBMITTED → STA_TESTING → SUCCESS
or FAILED → restore AP
```

The non-blocking HTTP server implements `/api/networks`, `/api/status`,
`/api/provision` and `/api/cancel`, plus common Android, iOS and Windows
captive-portal probes. A submission is acknowledged before the AP is stopped.
The candidate is saved atomically only after STA obtains IPv4; failure preserves
the old configuration and restores the AP.

## 8. Wi-Fi and VirtualHere lifecycle

The AIC8800 driver requests a 50 MHz SDIO clock; SG2002 division produces an
observed 46.875 MHz clock. Every STA start and reconnect applies
`iw dev wlan0 set power_save off`.

VirtualHere is delayed until association, a real IPv4 address, a wlan0 default
route and two continuous seconds of stable network state. Its state is:

- 0: not started;
- 1: TCP 7575 listening, waiting for a PC;
- 2: a stable PC TCP connection exists.

No PC connection is not a service fault. A wireless fault is published only
after three real start failures. Listening for 30 seconds without a client
produces a diagnostic hint to check PC routing or AP isolation.

## 9. Local CH347 switching

CH347 control does not wait for Linux, Wi-Fi or VirtualHere:

```text
select MODE0–3
→ drive DTR1/RTS1
→ assert RST# for 80 ms
→ release reset
→ read back latched state
→ update current mode and UI
```

Generation-based deduplication prevents repeated resets from duplicate touch
events. The pinout view shows only the actual mode and never writes GPIO, sends
IPC or initiates USB re-enumeration.

## 10. Display and bus clocks

- GC9A01 SPI request: 50 MHz; actual: 46.875 MHz.
- AIC8800 SDIO request: 50 MHz; actual: approximately 46.875 MHz.
- Wi-Fi RF channel width and the SDIO host bus clock are independent.
- The provisioning AP uses 2.4 GHz channel 1 with 20 MHz width.
- Full-screen motion targets the physical 48–50 FPS limit and does not claim
  60 FPS.

## 11. RootFS, FIP and image assembly

```text
airlink/c906l source → r26-lvgl.bin → FIP BLCP_2ND
airlink/linux source → airlinkd/airlinkctl → release RootFS
Linux + DTS → Kernel/DTB → boot.sd FIT
ramdisk + Buildroot + osdrv → SDK RootFS and modules
FIP + boot.sd + release RootFS → 1.58 GiB SD image
```

Release assembly removes Python, Qt, OpenCV, FFmpeg, development libraries,
debug tools and legacy services, then checks the ELF dependency closure. It
creates an MBR, a 16 MiB FAT16 boot partition and a 1600 MiB ext4 RootFS
partition. Verification reads FIP, FIT, DTB, critical RootFS binaries and the
AIC8800 module back from the final image and compares them with the build
outputs.

## 12. Fault and security boundaries

- A C906L UI failure cannot overwrite the Linux Wi-Fi configuration.
- Linux does not issue hardware commands after a failed IPC handshake.
- Provisioning failure keeps the old network and restores the hotspot.
- Wired mode terminates provisioning and wireless processes.
- CH347 switching may intentionally interrupt an active USB session but does
  not wait for Linux confirmation.
- Release listeners are limited to DHCP/DNS/HTTP during provisioning and
  VirtualHere during wireless sharing.
- A public release requires both a clean-WSL build and hardware acceptance of
  the image produced by that exact build.
