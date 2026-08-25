# Troubleshooting

Use Ubuntu 22.04 and keep the repository outside `/mnt/c`. Reserve at least
80 GB. If the SDK build fails, inspect `out/logs/sdk-build.log` and do not
copy images or build outputs from another tree to bypass the failure.

If VirtualHere cannot discover AirLink, verify that the PC and AirLink are in
the same LAN, TCP port 7575 is reachable and AP/client isolation is disabled.

## GitHub or tool downloads are extremely slow in WSL

When Windows uses a localhost proxy, WSL2 NAT cannot use `127.0.0.1` as the
Windows host. Find the host gateway and export the proxy before cloning:

```bash
ip route | grep default
export http_proxy=http://172.18.48.1:7897
export https_proxy=$http_proxy
git clone https://github.com/LX-DMT/AirLink.git
```

Replace the gateway and port with the values on the current PC. Do not put
proxy passwords or tokens in the repository.
