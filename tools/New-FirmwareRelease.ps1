[CmdletBinding()]
param(
  [ValidatePattern('^[A-Za-z0-9_-]+$')]
  [string]$DeviceType = 'audio_sensor',
  [string]$Version,
  [switch]$NextPatch
)

$ErrorActionPreference = 'Stop'
$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$deviceRoot = Join-Path $repoRoot "firmware\$DeviceType"

if ($NextPatch -and $Version) {
  throw 'Use either -Version or -NextPatch, not both.'
}
if (-not $NextPatch -and [string]::IsNullOrWhiteSpace($Version)) {
  throw 'Specify -Version 0.1.6 or use -NextPatch.'
}
if (-not (Test-Path -LiteralPath $deviceRoot)) {
  throw "Device type folder not found: $deviceRoot"
}

$releases = @(
  Get-ChildItem -LiteralPath $deviceRoot -Directory |
    Where-Object { $_.Name -match '^v(?<version>\d+\.\d+\.\d+)$' } |
    ForEach-Object {
      [pscustomobject]@{ Path = $_.FullName; Version = [version]$Matches.version }
    } |
    Sort-Object Version
)

if ($NextPatch) {
  if ($releases.Count -eq 0) {
    throw "No existing releases found under $deviceRoot. Specify -Version for the first release."
  }
  $latest = $releases[-1].Version
  $Version = "{0}.{1}.{2}" -f $latest.Major, $latest.Minor, ($latest.Build + 1)
}

if ($Version -notmatch '^\d+\.\d+\.\d+$') {
  throw "Version must use major.minor.patch format, for example 0.1.6. Received: $Version"
}
if ($releases.Count -eq 0) {
  throw 'A previous release is required as the template for a new release.'
}

$target = Join-Path $deviceRoot "v$Version"
if (Test-Path -LiteralPath $target) {
  throw "Release already exists: $target"
}

$template = $releases[-1].Path
Copy-Item -LiteralPath $template -Destination $target -Recurse

# Never inherit a compiled image or generated build cache.
Get-ChildItem -LiteralPath $target -Recurse -File -Filter '*.bin' | Remove-Item -Force
Get-ChildItem -LiteralPath $target -Recurse -Directory |
  Where-Object { $_.Name -eq 'build' } |
  Sort-Object FullName -Descending |
  Remove-Item -Recurse -Force

$sketchFiles = @(Get-ChildItem -LiteralPath $target -File -Filter '*.ino')
if ($sketchFiles.Count -ne 1) {
  throw "Expected exactly one sketch directly in $target; found $($sketchFiles.Count)."
}

$sketch = $sketchFiles[0]
$artifactStem = "${DeviceType}_$($Version -replace '\.', '_')"
$newSketchPath = Join-Path $target "$artifactStem.ino"
if ($sketch.FullName -ne $newSketchPath) {
  Move-Item -LiteralPath $sketch.FullName -Destination $newSketchPath
  $sketch = Get-Item -LiteralPath $newSketchPath
}

$source = Get-Content -LiteralPath $sketch.FullName -Raw
$updated = [regex]::Replace(
  $source,
  '(#define\s+FIRMWARE_VERSION\s+")([^"]+)(")',
  { param($match) $match.Groups[1].Value + $Version + $match.Groups[3].Value }
)
if ($updated -eq $source) {
  throw "FIRMWARE_VERSION definition was not found in $($sketch.FullName)."
}
Set-Content -LiteralPath $sketch.FullName -Value $updated -Encoding utf8

$buildInfo = @"
# $DeviceType v$Version

- Status: Not built
- Sketch: ``$($sketch.Name)``
- Binary: ``$artifactStem.bin``
- Binary SHA-256: pending clean compile

Run ``tools\Publish-Firmware.ps1 -DeviceType $DeviceType -Version $Version -Push`` to compile, hash, commit, tag, and push this release.
"@
Set-Content -LiteralPath (Join-Path $target 'BUILD_INFO.md') -Value $buildInfo -Encoding utf8

Write-Host "Created $target from $template"
Write-Host "Next: tools\Publish-Firmware.ps1 -DeviceType $DeviceType -Version $Version -Push"
