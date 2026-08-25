# Building AirLink in Windows WSL2

This is the supported, verified Ubuntu 22.04 build path. Do not mix this tree
with another SDK, an old image, an old RootFS or a development-machine
toolchain. The repository contains the complete SG2002 and AirLink source
snapshot and uses no Git submodules or external patch application.

## Requirements

- Windows 10 22H2 or Windows 11.
- WSL2 with Ubuntu 22.04.
- At least 8 GB available RAM; 16 GB is recommended.
- At least 80 GB free in the WSL Linux filesystem.
- A stable network for apt, the locked host-tools archive and hashed Buildroot
  sources.
- The repository under `/home/<user>`, never under `/mnt/c`.

## Install WSL2

Run in an Administrator PowerShell:

```powershell
wsl --install -d Ubuntu-22.04
wsl --set-version Ubuntu-22.04 2
wsl --list --verbose
```

The VERSION column for Ubuntu-22.04 must be 2. Start Ubuntu and create a normal
user. Do not build as root.

## Clone

```bash
sudo apt-get update
sudo apt-get install -y ca-certificates git make python3

cd ~
git clone https://github.com/LX-DMT/AirLink.git
cd AirLink
```

Do not run `git submodule update` and do not copy `host-tools`, `install`,
Kernel, FIP or RootFS files from another checkout.

## 1. Check the source and host

```bash
make doctor
```

Expected:

```text
AirLink source tree verification: PASS
AirLink doctor: PASS
  Ubuntu: 22.04
```

Doctor checks Ubuntu/WSL, memory, disk, source completeness, forbidden
submodules and patch history, developer paths, private keys, the GitHub file
size limit, the locked VirtualHere hash and C906L device ownership in DTS.

## 2. Install dependencies and toolchains

```bash
make bootstrap
```

Bootstrap runs apt, downloads the locked official SG2002 host-tools archive,
verifies SHA256, extracts it inside this repository and checks both the musl and
bare-metal RISC-V compilers. It supports download resume and retry.

Expected final lines:

```text
AirLink doctor: PASS
AirLink bootstrap: PASS
```

## 3. Build the complete release

```bash
make release
```

The fixed release path:

1. reruns strict doctor;
2. builds and tests C906L;
3. builds and tests airlinkd and airlinkctl;
4. clean-builds U-Boot, OpenSBI, Linux, DTB, osdrv and AIC8800;
5. builds middleware, ramdisk and the Buildroot RootFS;
6. creates the source-built FIP and boot.sd;
7. prunes and verifies the release RootFS;
8. replaces FIP BLCP_2ND with the new C906L binary;
9. creates MBR, FAT16 and ext4;
10. creates and compresses the 1.58 GiB image;
11. reads critical components back from the image;
12. records the full log, elapsed resources, hashes, manifest and SBOM.

The first build downloads pinned Buildroot sources and can take one to several
hours. Do not run two release builds in the same checkout.

Expected final messages:

```text
AirLink components: PASS
AirLink SG2002 SDK build: PASS
AirLink release verification: PASS
AirLink validation evidence: PASS
AirLink complete release build: PASS
```

Logs:

```text
out/logs/release-build.log
out/logs/sdk-build.log
```

## 4. Verify again

```bash
make verify
```

Verification checks the image size, MBR, FAT16 files, FIP, FIT, Kernel, ramdisk,
DTB ownership, ext4 consistency, program read-back hashes, the VirtualHere
lock, the AIC8800 high-speed transition, ELF dependency closure and absence of
SSH, Telnet and adbd from the release profile.

Expected:

```text
AirLink release verification: PASS
AirLink validation evidence: PASS
```

## Output

```text
out/release/Airlink-V2.0.0.img
out/release/SHA256SUMS
out/release/build-info.txt
out/release/manifest.json
out/release/SBOM.spdx.json
out/release/build-validation-report.md
out/release/build-environment.txt
out/release/build-resource.txt
out/release/build.log.zst
out/release/deleted-files.txt
out/release/elf-dependencies.txt
out/release/rootfs-size.txt
out/release/startup-services.txt
out/release/fip-components.json
```

Verify the release payload:

```bash
cd out/release
sha256sum -c SHA256SUMS
```

## Build AirLink components only

```bash
make components
```

This produces C906L, airlinkd and airlinkctl under `out/components`. It is
useful for development but is not a release build because it does not rebuild
Kernel, DTB, ramdisk, RootFS, drivers and FIP.

## Accepted vendor warnings

The unmodified SG2002 SDK emits warnings that remain visible in
`build.log.zst`. They are accepted only when the command exits with status 0
and every AirLink/image verification passes:

- duplicate `FLASH_SIZE_SHRINK` assignment in the vendor board defconfig;
- OpenSSL 3.0 deprecation messages from old U-Boot host tools;
- unused-function, format and C90 declaration warnings in unused vendor
  multimedia/camera drivers;
- recursive-make jobserver notices;
- legacy DTS prerequisite warnings for unused vendor camera/multimedia nodes;
- the ignored e2fsprogs documentation target error when manuals are disabled;
- the FAT lowercase-volume-label compatibility warning.

Do not ignore a non-zero exit, an undefined reference, a missing file/library,
a DTS error, a missing PASS line or any `make verify` failure.

## Troubleshooting

### Interrupted host-tools download

Run `make bootstrap` again. The archive resumes from `.cache/` and is
verified before extraction.

### Buildroot source download failure

Restore network/DNS and rerun `make release`. Do not insert an unverified
archive. Include the failed URL and the final 200 log lines in an issue.

### Disk space

```bash
df -h ~
du -sh host-tools buildroot/output linux_5.10/build ramdisk install out
```

### Ownership

```bash
sudo chown -R "$USER":"$USER" ~/AirLink
```

Never run `sudo make release`.

### Line endings

```bash
git config --global core.autocrlf input
git reset --hard
```

### Locate a failure

```bash
tail -200 out/logs/release-build.log
grep -nE 'Error|ERROR|FAILED|No such file|undefined reference' \
  out/logs/release-build.log | tail -100
```

Do not bypass a missing input by copying another checkout's `install/` or an
old image.

## Clean

```bash
make clean
```

This removes AirLink-generated `out/`. A subsequent `make release` still
runs the vendor SDK clean path and rebuilds the required components.

The image should be flashed for acceptance only when release, verify and
SHA256 checks pass and the report identifies the clean-WSL source commit.

For the two mandatory disposable builds, follow [Clean WSL2 validation](clean-wsl-validation.md).
