# Clean WSL2 validation

A public release is valid only after two independent builds in a disposable
Ubuntu 22.04 WSL2 distribution.

## Rules

- The distribution name is `AirLink-Build-Verify`.
- The build user is the non-root user `builder`.
- Source is cloned from `https://github.com/LX-DMT/AirLink.git`.
- No current checkout, host-tools, Buildroot download cache, `install/`,
  `out/`, old image or old RootFS is copied into the distribution.
- The commands are exactly `make doctor`, `make bootstrap`,
  `make release` and `make verify`.
- A failed attempt is unregistered and recreated before another claim.
- Evidence is copied out only after the build; build inputs are never copied in.

## Automated Windows runner

Use the repository script from an Administrator PowerShell after downloading an
official Ubuntu 22.04 WSL rootfs tarball:

```powershell
Set-ExecutionPolicy -Scope Process Bypass
.\scripts\windows\verify-clean-wsl.ps1 `
  -RootfsTar C:\path\ubuntu-jammy-wsl-amd64.rootfs.tar.gz `
  -RepositoryUrl https://github.com/LX-DMT/AirLink.git `
  -InstallRoot D:\AirLink-WSL
```

The runner verifies that the URL is GitHub HTTPS, imports a new WSL2
distribution, creates `builder`, executes the README path, copies the release
evidence and the exact release `.img` to `airlink-build-validation\`, and
unregisters the distribution. `-InstallRoot` is optional; use a drive with at
least 80 GB free instead of filling the Windows system drive. The runner also
records the full PowerShell/WSL console transcript in `clean-wsl-console.log`.
Use `-KeepOnSuccess` only for investigation; never use a retained distribution
as the second clean validation.

Record the rootfs SHA256 beside the evidence:

```powershell
Get-FileHash C:\path\ubuntu-jammy-wsl-amd64.rootfs.tar.gz -Algorithm SHA256
```

## Two rounds

1. Push the tracked source snapshot and run the disposable build. Fix every
   missing file, absolute path or undocumented warning, then destroy the distro.
2. Push the corrected commit, recreate the distro from the original rootfs
   tarball, clone GitHub again and repeat the four README commands.

The exact second-round image must then pass the hardware acceptance checklist
in [release-process.md](release-process.md).
