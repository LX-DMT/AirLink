# AirLink user guide

## 1. Select wired or wireless mode

Use the physical mode switch connected to GPIOA_29.

- **Wired mode**: the computer directly owns the USB hub. The display reports
  HDMI, Ethernet and the headphone connector as available.
- **Wireless mode**: Linux connects to Wi-Fi and VirtualHere exposes the USB
  devices to a computer on the same LAN.

A mode change returns the display to the matching home page. Network startup
continues in the background; the UI and CH347 controls remain responsive.

## 2. First Wi-Fi provisioning

When no Wi-Fi profile is saved, AirLink automatically creates a provisioning
access point:

```text
SSID:     AirLink-XXXX
Password: 12345678
Portal:   http://192.168.4.1/
```

1. Put AirLink in wireless mode.
2. On the phone, connect to `AirLink-XXXX` with password `12345678`.
3. The captive portal should open automatically. If it does not, open
   `http://192.168.4.1/` in a browser.
4. Select the target Wi-Fi network. The selected row has a blue border.
5. Enter the target network password and tap **Connect**.
6. Keep the phone near AirLink while it tests the connection for up to
   30 seconds.
7. The round display returns to the Wi-Fi page after the profile is saved.

The AP password is fixed by product policy. The target Wi-Fi password is never
sent to the C906L display firmware and is excluded from status and diagnostic
output.

### Reconfigure or forget Wi-Fi

Open the Wi-Fi page on the round display and tap **Reconfigure**. Closing the
hotspot information window only closes that window; it does not stop the AP or
phone portal. Use `airlinkctl wifi forget` on the serial console to remove the
saved profile completely.

## 3. VirtualHere

Install the official VirtualHere Client on the computer.

1. Confirm AirLink and the computer are on the same LAN.
2. Wait for the display to change from **Waiting for computer** to
   **Computer connected**.
3. In VirtualHere Client, expand the AirLink server and select the USB device.

AirLink listens on TCP 7575. A listening server with no computer connection is
normal and is not a service fault. If discovery does not work:

- enter the AirLink IP manually in VirtualHere Client;
- disable wireless/AP/client isolation on the router;
- confirm the PC firewall allows VirtualHere;
- if the router isolates 2.4 GHz clients, connect the computer through its
  5 GHz band while keeping both bands in the same LAN;
- check `airlinkctl status` and `airlinkctl diag export` over serial.

Release images do not expose SSH, Telnet or adbd.

## 4. CH347 modes

CH347 switching is local to C906L and does not wait for Linux or Wi-Fi.

| UI mode | CH347 MODE | Function |
|---|---:|---|
| Dual UART | MODE0 | Two UART interfaces |
| SPI / I2C vendor driver | MODE1 | Vendor-driver SPI and I2C |
| SPI / I2C HID | MODE2 | Driverless HID SPI and I2C |
| JTAG / UART | MODE3 | JTAG plus UART |

Open the CH347 page, tap **Switch mode**, choose a mode and confirm. C906L sets
DTR1/RTS1, holds CH347 reset for 80 ms and updates the UI. The USB device
re-enumerates, so an active VirtualHere transfer can be interrupted.

Tap **Pin reference** to show the 2 x 6 connector map for the actual current
mode. The rounded **Screen direction** marker identifies the connector side
nearest the physical display. The reference is read-only; tapping it returns to
the CH347 page.

## 5. Display, battery and screen saver

- The battery voltage is sampled by C906L before the first LVGL frame and does
  not depend on Linux startup.
- The four battery bars use a lightly filtered ADC value. Short invalid samples
  keep the previous display instead of flashing gray.
- The screen saver starts after 30 seconds without activity.
- Touch, a stable GPIOA_29 mode change and successful phone provisioning reset
  the complete 30-second deadline.
- The first touch exits the screen saver.

## 6. Useful status meanings

| Display text | Meaning |
|---|---|
| Waiting for phone provisioning | No saved Wi-Fi or provisioning is active |
| Connecting to Wi-Fi | STA association or DHCP is in progress |
| Wireless service starting | Wi-Fi is up but VirtualHere is not listening |
| Waiting for computer | TCP 7575 is listening; no stable client connection |
| Computer connected | A VirtualHere client TCP connection is established |
| Wireless service fault | A real service fault persisted past retry limits |

See [Troubleshooting](troubleshooting.md) for serial diagnostics and recovery.
