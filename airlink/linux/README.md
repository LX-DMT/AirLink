# R27.6.6.22 portal blue link icon

This Linux-only debug update is built on the validated R27.6.6.21 50 MHz
image and replaces only `/usr/sbin/airlinkd`.

Changes:

- replaces the enlarged letter A portal mark with an inline blue
  USB/Wi-Fi/link icon derived from the user's structural reference;
- restores the complete `AirLink` brand text next to the icon;
- keeps the compact 32 px light-blue brand container and all other portal
  layout, selection and provisioning behavior unchanged;
- keeps the R27.6.6.19 network-state, power-save and VirtualHere diagnostics
  logic unchanged apart from the Linux program version string.

Compatibility remains C906L R27P, Linux LN27, IPC protocol v1 / ABI4.
The icon uses only local HTML/CSS/SVG resources and does not embed the
reference bitmap or any watermark.