# Troubleshooting

Use Ubuntu 22.04 and keep the repository outside `/mnt/c`. Reserve at least
80 GB. If the SDK build fails, inspect `out/logs/sdk-build.log` and do not
copy images or build outputs from another tree to bypass the failure.

If VirtualHere cannot discover AirLink, verify that the PC and AirLink are in
the same LAN, TCP port 7575 is reachable and AP/client isolation is disabled.
