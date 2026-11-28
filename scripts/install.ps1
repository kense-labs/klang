<#
.SYNOPSIS
    Cross-platform PowerShell installer for the K programming language (klang).
.EXAMPLE
    irm https://raw.githubusercontent.com/kense-labs/klang/main/scripts/install.ps1 | iex
.EXAMPLE
    .\install.ps1 -Version v0.1.0-preview.1 -InstallDir "$HOME\.klang\current"
#>
[CmdletBinding()]
param(
    [string]$Version = $env:KLANG_VERSION,
    [string]$InstallDir = $(if ($env:KLANG_INSTALL_DIR) { $env:KLANG_INSTALL_DIR } else { Join-Path $HOME ".klang\current" }),
    [switch]$NoModifyPath = ($env:KLANG_NO_MODIFY_PATH -eq "1"),
    [switch]$Prerelease = ($env:KLANG_PRERELEASE -eq "1"),
    [string]$BaseUrl = $env:KLANG_BASE_URL
)

$ErrorActionPreference = "Stop"

# Ensure TLS 1.2 is enabled on Windows PowerShell 5.1.
try {
    [Net.ServicePointManager]::SecurityProtocol = [Net.ServicePointManager]::SecurityProtocol -bor [Net.SecurityProtocolType]::Tls12
} catch {
}

$Repo = "kense-labs/klang"
$ApiBase = "https://api.github.com/repos/$Repo"

function Get-GitHubHeaders {
    $headers = @{
        "Accept"     = "application/vnd.github+json"
        "User-Agent" = "klang-installer"
    }
    if ($env:GITHUB_TOKEN) {
        $headers["Authorization"] = "Bearer $($env:GITHUB_TOKEN)"
    }
    return $headers
}

function Resolve-KlangVersion {
    param([string]$Requested, [bool]$IncludePrerelease)
    if (-not [string]::IsNullOrWhiteSpace($Requested)) {
        return $Requested.Trim()
    }

    $headers = Get-GitHubHeaders
    if (-not $IncludePrerelease) {
        try {
            $latest = Invoke-RestMethod -Uri "$ApiBase/releases/latest" -Headers $headers -UseBasicParsing
            if ($latest -and -not $latest.draft -and $latest.tag_name) {
                return [string]$latest.tag_name
            }
        } catch {
        }
    }

    $releases = Invoke-RestMethod -Uri "$ApiBase/releases?per_page=10" -Headers $headers -UseBasicParsing
    foreach ($rel in @($releases)) {
        if ($rel -and -not $rel.draft -and $rel.tag_name) {
            if (-not $IncludePrerelease) {
                Write-Host "No stable release found; selecting latest preview release $($rel.tag_name)"
            }
            return [string]$rel.tag_name
        }
    }
    throw "Could not determine a release tag (pass -Version <tag> explicitly)."
}

function Test-WindowsArch {
    $arch = $env:PROCESSOR_ARCHITECTURE
    if ($env:PROCESSOR_ARCHITEW6432) {
        $arch = $env:PROCESSOR_ARCHITEW6432
    }
    switch ($arch.ToUpperInvariant()) {
        "AMD64" { return }
        "ARM64" {
            Write-Warning "Windows ARM64 detected; installing the windows-x64 release (runs under x64 emulation)."
            return
        }
        default {
            throw "Unsupported Windows architecture '$arch' (only 64-bit x64/ARM64 is supported)."
        }
    }
}

function Get-ExpectedChecksum {
    param([string]$SumsPath, [string]$AssetName)
    foreach ($line in Get-Content -Path $SumsPath) {
        $trimmed = $line.Trim()
        if (-not $trimmed -or $trimmed.StartsWith("#")) { continue }
        $parts = $trimmed -split "\s+", 2
        if ($parts.Count -lt 2) { continue }
        $hash = $parts[0].Trim().ToLowerInvariant()
        $name = $parts[1].Trim().TrimStart("*")
        if ($name.StartsWith("./") -or $name.StartsWith(".\")) {
            $name = $name.Substring(2)
        }
        if ($name -eq $AssetName) {
            return $hash
        }
    }
    throw "No checksum entry for '$AssetName' found in SHA256SUMS."
}

function Add-ToUserPath {
    param([string]$Dir)
    $normalizedTarget = $Dir.TrimEnd("\")
    $currentUserPath = [Environment]::GetEnvironmentVariable("Path", "User")
    $entries = @()
    if ($currentUserPath) {
        $entries = $currentUserPath -split ";" | Where-Object { $_ -and $_.Trim() }
    }
    $alreadyInUser = $entries | Where-Object { $_.Trim().TrimEnd("\").Equals($normalizedTarget, [StringComparison]::OrdinalIgnoreCase) }
    if (-not $alreadyInUser) {
        $newPath = if ($entries.Count -gt 0) { ($entries + $normalizedTarget) -join ";" } else { $normalizedTarget }
        [Environment]::SetEnvironmentVariable("Path", $newPath, "User")
        Write-Host "Added $normalizedTarget to User PATH."
    }

    $procEntries = ($env:Path -split ";") | Where-Object { $_ -and $_.Trim() }
    $alreadyInProc = $procEntries | Where-Object { $_.Trim().TrimEnd("\").Equals($normalizedTarget, [StringComparison]::OrdinalIgnoreCase) }
    if (-not $alreadyInProc) {
        $env:Path = "$normalizedTarget;$($env:Path)"
    }
}

Test-WindowsArch
$Version = Resolve-KlangVersion -Requested $Version -IncludePrerelease ([bool]$Prerelease)
$Asset = "klang-$Version-windows-x64.zip"
$DlBase = if (-not [string]::IsNullOrWhiteSpace($BaseUrl)) { $BaseUrl.TrimEnd("/") } else { "https://github.com/$Repo/releases/download/$Version" }

$TempDir = Join-Path ([IO.Path]::GetTempPath()) ("klang-install-" + [Guid]::NewGuid().ToString("N"))
New-Item -ItemType Directory -Path $TempDir -Force | Out-Null

try {
    $ArchivePath = Join-Path $TempDir $Asset
    $SumsPath = Join-Path $TempDir "SHA256SUMS"
    $ExtractDir = Join-Path $TempDir "extract"

    Write-Host "Downloading $Asset ($Version)..."
    Invoke-WebRequest -Uri "$DlBase/$Asset" -OutFile $ArchivePath -UseBasicParsing
    Invoke-WebRequest -Uri "$DlBase/SHA256SUMS" -OutFile $SumsPath -UseBasicParsing

    $ExpectedHash = Get-ExpectedChecksum -SumsPath $SumsPath -AssetName $Asset
    $ActualHash = (Get-FileHash -Path $ArchivePath -Algorithm SHA256).Hash.ToLowerInvariant()
    if ($ActualHash -ne $ExpectedHash) {
        throw "SHA-256 verification failed for $Asset (expected $ExpectedHash, got $ActualHash)."
    }
    Write-Host "Verified SHA-256 checksum ($ExpectedHash)"

    Write-Host "Extracting into $InstallDir..."
    Expand-Archive -Path $ArchivePath -DestinationPath $ExtractDir -Force

    $KExe = Get-ChildItem -Path $ExtractDir -Filter "k.exe" -Recurse | Select-Object -First 1
    if (-not $KExe) {
        throw "Extracted archive did not contain 'k.exe'."
    }
    $PayloadDir = $KExe.Directory.FullName

    $InstalledVer = (& $KExe.FullName version) -join "`n"
    if ($LASTEXITCODE -ne 0 -or [string]::IsNullOrWhiteSpace($InstalledVer)) {
        throw "'$($KExe.FullName) version' failed after extraction."
    }

    $ParentDir = Split-Path -Path $InstallDir -Parent
    if ($ParentDir -and -not (Test-Path $ParentDir)) {
        New-Item -ItemType Directory -Path $ParentDir -Force | Out-Null
    }

    $BackupDir = $null
    if (Test-Path $InstallDir) {
        $BackupDir = "$InstallDir.bak." + [Guid]::NewGuid().ToString("N").Substring(0, 8)
        Move-Item -Path $InstallDir -Destination $BackupDir -Force
    }
    try {
        Move-Item -Path $PayloadDir -Destination $InstallDir -Force
    } catch {
        if ($BackupDir -and (Test-Path $BackupDir)) {
            Move-Item -Path $BackupDir -Destination $InstallDir -Force -ErrorAction SilentlyContinue
        }
        throw
    }
    if ($BackupDir -and (Test-Path $BackupDir)) {
        Remove-Item -Path $BackupDir -Recurse -Force -ErrorAction SilentlyContinue
    }

    if (-not $NoModifyPath) {
        Add-ToUserPath -Dir $InstallDir
    }

    Write-Host ""
    Write-Host "Installed K ($($InstalledVer.Trim())) to $InstallDir"
    Write-Host "Restart existing terminals if 'k' is not yet on PATH."
    Write-Host "To uninstall: Remove-Item -Recurse -Force `"$InstallDir`""
} finally {
    if (Test-Path $TempDir) {
        Remove-Item -Path $TempDir -Recurse -Force -ErrorAction SilentlyContinue
    }
}
