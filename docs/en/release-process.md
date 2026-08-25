# Release process

The public version is V2.0.0, the internal build is R27.6.6.23, and the
release tag is Airlink-V2.0.0. A build is publishable only after two
disposable GitHub-clone builds and hardware acceptance of the exact second
image.

## 1. Freeze and back up

1. Confirm all intended source files are tracked and the worktree is clean.
2. Create a local Git bundle before connecting the V2 tree to the historical
   repository.
3. Preserve Airlink-V1.0.0 and the existing Git history.
4. Push the candidate first to release/v2.0.0-validation; do not create a V2
   tag or Release.

## 2. Disposable WSL validation round 1

Create a fresh Ubuntu 22.04 WSL2 distribution named AirLink-Build-Verify,
clone the validation branch from GitHub as the ordinary builder user, and
execute:

    make doctor
    make bootstrap
    make release
    make verify

Do not copy host-tools, package caches, old images, RootFS files or current
build outputs into the distribution. Any failure requires unregistering and
recreating the distribution.

## 3. Disposable WSL validation round 2

After round 1 passes, fast-forward main to the validated commit. Destroy and
recreate the WSL distribution, then follow the README literally:

    git clone https://github.com/LX-DMT/AirLink.git
    cd AirLink
    make doctor
    make bootstrap
    make release
    make verify

The report must identify the final Git commit and show zero source worktree
changes.

## 4. Release assets

Only these files from the second clean build are public Release assets:

    Airlink-V2.0.0.img
    SHA256SUMS
    build-info.txt
    manifest.json
    SBOM.spdx.json
    build-validation-report.md

The raw image remains outside Git history. Keep the environment, resource,
compressed build log, ELF dependency, RootFS size, startup-service and FIP
reports as local build evidence.

## 5. Hardware acceptance

Flash the exact second-round image and complete 10 cold boots, phone
provisioning and reprovisioning, 20 wired/wireless switches, a 1 GiB
VirtualHere transfer, ten switches of each CH347 mode, display/touch/ADC/screen
saver checks, and SDIO clock verification. Release must expose no SSH, Telnet
or adbd listener and must show no SDIO CRC/timeout or DWC2 reset.

Any source or binary change invalidates this acceptance result.

## 6. Publish

After hardware acceptance:

1. create the annotated tag Airlink-V2.0.0;
2. create the GitHub Release with the same name;
3. upload the six required assets;
4. download the assets again and run sha256sum -c SHA256SUMS;
5. publish only after the downloaded image passes a final smoke test.

The Release notes must identify VirtualHere and AIC8800 firmware as third-party
components with separate redistribution terms.
