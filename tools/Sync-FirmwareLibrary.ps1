[CmdletBinding()]
param([Parameter(Mandatory)][string]$Destination)
$ErrorActionPreference = 'Stop'
$sourceRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$targetRoot = [IO.Path]::GetFullPath($Destination).TrimEnd('\')
if ($targetRoot -eq $sourceRoot.TrimEnd('\')) { throw 'Source and destination must differ.' }
$files = @()
foreach ($folder in @('firmware', 'tools', 'scripts', '.github', 'include', 'src')) {
  $files += Get-ChildItem -LiteralPath (Join-Path $sourceRoot $folder) -Recurse -File | Where-Object {
    $_.Name -ne 'test.txt' -and -not ($_.Name -eq 'config.h' -and $_.DirectoryName -eq (Join-Path $sourceRoot 'include'))
  }
}
$files += Get-ChildItem -LiteralPath $sourceRoot -File | Where-Object { $_.Name -in @('.gitignore', 'README.md', 'LICENSE', 'manifest.schema.json', 'platformio.ini', 'SYNC_STATUS.md') }
$stamp = Get-Date -Format 'yyyyMMdd-HHmmss'
$copied = 0; $same = 0; $backedUp = 0
foreach ($file in $files) {
  $relative = $file.FullName.Substring($sourceRoot.Length + 1)
  $target = Join-Path $targetRoot $relative
  $hash = (Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash
  if (Test-Path -LiteralPath $target) {
    if ((Get-FileHash -LiteralPath $target -Algorithm SHA256).Hash -eq $hash) { $same++; continue }
    $backup = Join-Path $targetRoot ('.sync-backups/' + $stamp + '/' + $relative)
    New-Item -ItemType Directory -Path (Split-Path $backup) -Force | Out-Null
    Copy-Item -LiteralPath $target -Destination $backup
    $backedUp++
  }
  New-Item -ItemType Directory -Path (Split-Path $target) -Force | Out-Null
  Copy-Item -LiteralPath $file.FullName -Destination $target -Force
  if ((Get-FileHash -LiteralPath $target -Algorithm SHA256).Hash -ne $hash) { throw "Hash mismatch: $relative" }
  $copied++
}
[pscustomobject]@{ Destination = $targetRoot; VerifiedFiles = $files.Count; Copied = $copied; Unchanged = $same; BackedUp = $backedUp }
