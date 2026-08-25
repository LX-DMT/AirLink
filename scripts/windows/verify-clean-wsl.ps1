#Requires -Version 5.1
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [ValidateScript({ Test-Path -LiteralPath $_ -PathType Leaf })]
    [string]$RootfsTar,

    [Parameter(Mandatory = $true)]
    [ValidatePattern('^https://github\.com/LX-DMT/AirLink(?:\.git)?/?$')]
    [string]$RepositoryUrl,

    [string]$Branch = 'main',
    [string]$DistroName = 'AirLink-Build-Verify',
    [string]$EvidenceDir = (Join-Path $PWD 'airlink-build-validation'),
    [string]$InstallRoot = (Join-Path $env:LOCALAPPDATA 'AirLink\WSL'),
    [switch]$KeepOnSuccess
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

function Invoke-WslChecked {
    param(
        [Parameter(Mandatory = $true)][string]$User,
        [Parameter(Mandatory = $true)][string]$Command
    )
    & wsl.exe -d $DistroName -u $User -- bash -lc $Command
    if ($LASTEXITCODE -ne 0) {
        throw "WSL command failed with exit code $($LASTEXITCODE): $Command"
    }
}

$sourceRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
$versionData = @{}
foreach ($line in Get-Content -LiteralPath (Join-Path $sourceRoot 'versions.lock')) {
    if ($line -match '^([A-Z][A-Z0-9_]*)=(.+)$') {
        $versionData[$Matches[1]] = $Matches[2]
    }
}
foreach ($required in @('AIRLINK_VERSION', 'AIRLINK_BUILD', 'AIRLINK_IMAGE')) {
    if (-not $versionData.ContainsKey($required)) {
        throw "versions.lock is missing $required"
    }
}
$imageName = $versionData['AIRLINK_IMAGE']

$existing = @(& wsl.exe --list --quiet) | ForEach-Object { $_.Trim([char]0).Trim() }
if ($existing -contains $DistroName) {
    throw "WSL distribution '$DistroName' already exists. Unregister it before validation."
}

$RootfsTar = (Resolve-Path -LiteralPath $RootfsTar).Path
$rootfsHash = (Get-FileHash -LiteralPath $RootfsTar -Algorithm SHA256).Hash.ToLowerInvariant()
$InstallRoot = [System.IO.Path]::GetFullPath($InstallRoot)
$installDir = [System.IO.Path]::GetFullPath((Join-Path $InstallRoot $DistroName))
$installRootPrefix = $InstallRoot.TrimEnd('\') + '\'
if (-not $installDir.StartsWith($installRootPrefix, [System.StringComparison]::OrdinalIgnoreCase) -or
    [System.IO.Path]::GetFileName($installDir) -ne $DistroName) {
    throw "Unsafe WSL install directory: $installDir"
}
$success = $false
$transcript = Join-Path $EvidenceDir 'clean-wsl-console.log'

try {
    New-Item -ItemType Directory -Force -Path $installDir | Out-Null
    New-Item -ItemType Directory -Force -Path $EvidenceDir | Out-Null
    Start-Transcript -LiteralPath $transcript -Force | Out-Null

    & wsl.exe --import $DistroName $installDir $RootfsTar --version 2
    if ($LASTEXITCODE -ne 0) {
        throw "wsl --import failed with exit code $LASTEXITCODE"
    }

    Invoke-WslChecked -User root -Command @'
set -euo pipefail
export DEBIAN_FRONTEND=noninteractive
apt-get update
apt-get install -y ca-certificates git make python3 sudo
id builder >/dev/null 2>&1 || useradd --create-home --shell /bin/bash builder
printf 'builder ALL=(ALL) NOPASSWD: ALL\n' >/etc/sudoers.d/90-airlink-builder
chmod 0440 /etc/sudoers.d/90-airlink-builder
chown -R builder:builder /home/builder
'@

    $quotedUrl = $RepositoryUrl.Replace("'", "'\''")
    $quotedBranch = $Branch.Replace("'", "'\''")
    if ($Branch -eq 'main') {
        $cloneCommand = "git clone '$quotedUrl' AirLink"
    }
    else {
        $cloneCommand = "git clone --branch '$quotedBranch' --single-branch '$quotedUrl' AirLink"
    }
    Invoke-WslChecked -User builder -Command @"
set -euo pipefail
cd /home/builder
$cloneCommand
cd AirLink
make doctor
make bootstrap
make release
make verify
"@

    $uncRelease = "\\wsl.localhost\$DistroName\home\builder\AirLink\out\release"
    if (-not (Test-Path -LiteralPath $uncRelease -PathType Container)) {
        throw "Release evidence directory is missing: $uncRelease"
    }

    $files = @(
        $imageName,
        'build-validation-report.md',
        'build-environment.txt',
        'build-resource.txt',
        'build.log.zst',
        'SHA256SUMS',
        'manifest.json',
        'SBOM.spdx.json',
        'build-info.txt',
        'rootfs-size.txt',
        'startup-services.txt',
        'fip-components.json',
        'fip-components.txt',
        'elf-dependencies.txt'
    )
    foreach ($name in $files) {
        $source = Join-Path $uncRelease $name
        if (-not (Test-Path -LiteralPath $source -PathType Leaf)) {
            throw "Required validation evidence is missing: $name"
        }
        Copy-Item -LiteralPath $source -Destination $EvidenceDir -Force
    }

    $sourceCommitLine = Get-Content -LiteralPath (Join-Path $EvidenceDir 'build-environment.txt') |
        Where-Object { $_ -like 'source_commit=*' } |
        Select-Object -First 1
    if (-not $sourceCommitLine) {
        throw 'build-environment.txt does not contain source_commit'
    }

    @(
        "validation_utc=$([DateTime]::UtcNow.ToString('yyyy-MM-ddTHH:mm:ssZ'))"
        "distro=$DistroName"
        "repository=$RepositoryUrl"
        "branch=$Branch"
        "version=$($versionData['AIRLINK_VERSION'])"
        "build=$($versionData['AIRLINK_BUILD'])"
        "image=$imageName"
        "rootfs=$RootfsTar"
        "rootfs_sha256=$rootfsHash"
        "install_root=$InstallRoot"
        $sourceCommitLine
    ) | Set-Content -LiteralPath (Join-Path $EvidenceDir 'clean-wsl-run.txt') -Encoding UTF8

    $success = $true
    Write-Host "AirLink clean WSL validation: PASS"
    Write-Host "Evidence: $EvidenceDir"
}
finally {
    try {
        Stop-Transcript | Out-Null
    }
    catch {
        # A failure before Start-Transcript is expected to reach this path.
    }
    if (-not $success -or -not $KeepOnSuccess) {
        & wsl.exe --terminate $DistroName 2>$null
        & wsl.exe --unregister $DistroName 2>$null
        if (Test-Path -LiteralPath $installDir) {
            $resolvedInstallDir = [System.IO.Path]::GetFullPath($installDir)
            if (-not $resolvedInstallDir.StartsWith($installRootPrefix, [System.StringComparison]::OrdinalIgnoreCase) -or
                [System.IO.Path]::GetFileName($resolvedInstallDir) -ne $DistroName) {
                throw "Refusing to remove unsafe WSL directory: $resolvedInstallDir"
            }
            Remove-Item -LiteralPath $installDir -Recurse -Force
        }
    }
}
