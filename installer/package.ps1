#requires -Version 5.1
<#
.SYNOPSIS
    Builds the PadsWay distributables under dist\: portable staging folder,
    portable .zip and the Inno Setup installer.

.DESCRIPTION
    Encodes the "clean release" payload rules in ONE place, so nothing gets
    forgotten or leaked when staging by hand:

      Ships  : PadsWay.exe, factory data\ (pad_layouts.json, state_map.json,
               strings\, virtualpad.json defaults, an EMPTY controllers.json),
               images\, and the docs (README / MACROS / QUICK_START, LICENSE).
      Never  : the developer's real controllers.json, profiles\, macros.json,
               pad_layouts.json.bak, controller templates, bots\, or portable.txt.

    It does NOT compile the C++ app. Build the Release config in Visual Studio
    first; this script only packages the existing x64\Release\PadsWay.exe.

.EXAMPLE
    powershell -ExecutionPolicy Bypass -File installer\package.ps1
    powershell -ExecutionPolicy Bypass -File installer\package.ps1 -Version 0.19
    powershell -ExecutionPolicy Bypass -File installer\package.ps1 -SkipZip
#>
[CmdletBinding()]
param(
    [string]$Version = '0.18',
    [string]$Iscc    = 'C:\Programas\Inno Setup 6\ISCC.exe',
    [switch]$SkipZip,
    [switch]$SkipInstaller
)

$ErrorActionPreference = 'Stop'

$root    = Split-Path -Parent $PSScriptRoot          # repo root (script lives in installer\)
$srcData = Join-Path $root 'PadsWay\data'
$srcImg  = Join-Path $root 'PadsWay\images'
$exe     = Join-Path $root 'x64\Release\PadsWay.exe'
$distDir = Join-Path $root 'dist'
$topName = "PadsWay-v$Version-win64"
$stage   = Join-Path $distDir $topName

# --- 1. The freshly built Release exe must exist (we do not compile here) ------
if (-not (Test-Path $exe)) {
    throw "Release exe not found at $exe. Build the Release config in Visual Studio first."
}
Write-Host "Using exe: $exe  ($((Get-Item $exe).LastWriteTime))" -ForegroundColor Cyan

# --- 2. Clean + recreate the staging folder -----------------------------------
if (Test-Path $stage) { Remove-Item $stage -Recurse -Force }
New-Item -ItemType Directory -Path (Join-Path $stage 'data') -Force | Out-Null

# --- 3. Executable ------------------------------------------------------------
Copy-Item $exe (Join-Path $stage 'PadsWay.exe')

# --- 4. Docs + LICENSE --------------------------------------------------------
foreach ($doc in 'README.md','README.es.md','MACROS.md','MACROS.es.md',
                 'QUICK_START.md','QUICK_START.es.md','LICENSE') {
    Copy-Item (Join-Path $root $doc) (Join-Path $stage $doc)
}

# --- 5. Factory data\ (whitelist: only shippable assets + safe defaults) -------
foreach ($f in 'pad_layouts.json','state_map.json','virtualpad.json') {
    Copy-Item (Join-Path $srcData $f) (Join-Path $stage "data\$f")
}
Copy-Item (Join-Path $srcData 'strings') -Destination (Join-Path $stage 'data') -Recurse
# Empty controllers list. NEVER ship the developer's real controllers.json.
# Written as UTF-8 without BOM so the loader parses it cleanly.
[System.IO.File]::WriteAllText(
    (Join-Path $stage 'data\controllers.json'),
    '{"controllers":[]}',
    (New-Object System.Text.UTF8Encoding $false))

# --- 6. images\ (drop the Krita originals in founts\) -------------------------
Copy-Item $srcImg -Destination $stage -Recurse
$founts = Join-Path $stage 'images\founts'
if (Test-Path $founts) { Remove-Item $founts -Recurse -Force }

# --- 7. Guard: portable.txt must NEVER reach the package ----------------------
Get-ChildItem $stage -Recurse -Filter 'portable.txt' -ErrorAction SilentlyContinue | ForEach-Object {
    Write-Warning "Removed stray portable.txt from staging: $($_.FullName)"
    Remove-Item $_.FullName -Force
}
Write-Host "Staging ready: $stage" -ForegroundColor Green

# --- 8. Portable .zip ---------------------------------------------------------
# The ZIP standard requires '/' separators. PowerShell 5.1 Compress-Archive and
# ZipFile.CreateFromDirectory both write '\', which other extractors mishandle
# (folder structure lost). So we build the archive by hand, normalising names.
if (-not $SkipZip) {
    Add-Type -AssemblyName System.IO.Compression
    Add-Type -AssemblyName System.IO.Compression.FileSystem
    $zip = Join-Path $distDir "$topName.zip"
    if (Test-Path $zip) { Remove-Item $zip -Force }
    $archive = [System.IO.Compression.ZipFile]::Open($zip, [System.IO.Compression.ZipArchiveMode]::Create)
    try {
        Get-ChildItem $stage -Recurse -File | ForEach-Object {
            $rel = $_.FullName.Substring($stage.Length + 1).Replace([char]92, [char]47)  # backslash -> slash
            [System.IO.Compression.ZipFileExtensions]::CreateEntryFromFile($archive, $_.FullName, "$topName/$rel") | Out-Null
        }
    } finally {
        $archive.Dispose()
    }
    # Verify no backslash leaked into entry names.
    $reader = [System.IO.Compression.ZipFile]::OpenRead($zip)
    try { $bad = @($reader.Entries | Where-Object { $_.FullName -match '\\' }) } finally { $reader.Dispose() }
    if ($bad.Count -gt 0) { throw "Zip has $($bad.Count) backslash entries (broken structure)." }
    Write-Host "Portable zip: $zip" -ForegroundColor Green
}

# --- 9. Installer via Inno Setup ----------------------------------------------
if (-not $SkipInstaller) {
    if (-not (Test-Path $Iscc)) {
        throw "ISCC not found at $Iscc. Pass -Iscc PATH or install Inno Setup 6."
    }
    & $Iscc "/DAppVersion=$Version" (Join-Path $root 'installer\PadsWay.iss')
    if ($LASTEXITCODE -ne 0) { throw "ISCC failed with exit code $LASTEXITCODE." }
    Write-Host "Installer: $(Join-Path $distDir ('PadsWay-v' + $Version + '-setup.exe'))" -ForegroundColor Green
}

Write-Host "Done." -ForegroundColor Green
