# Release build for Ink & Quill: the FOMOD archive players install.
#
#   1. Refuses a working tree with uncommitted changes (a release is a commit), unless -allowDirty.
#   2. Builds everything with .\Build_Local.ps1 -noDeploy (plugin, Papyrus, SWFs, ESP), unless -skipBuild.
#   3. Stages build\release\stage: 00 Core (DLL, ESP, Scripts, Source\Scripts, Interface\Translations),
#      01 Vanilla and 02 Convenient Reading (one book.swf each), fomod\ (its version from CMakeLists.txt).
#   4. Checks what it staged: both SWFs uncompressed with the writing marker, every translation with
#      English's keys, ModuleConfig.xml valid with every folder it installs present.
#   5. Zips it: build\release\<fomod\info.xml's Name> <version>.zip.
#
# Usage:
#   .\Build_Release.ps1                # build, check, zip
#   .\Build_Release.ps1 -skipBuild     # pack the last build as it is
#   .\Build_Release.ps1 -allowDirty    # a test release from uncommitted work
#
# The outcome is printed and written to %TEMP%\iq-release-result.json. See docs/DEVELOPMENT.md#releases.

#Requires -Version 7

param(
    [string]$config = "Release",
    [switch]$skipBuild,
    [switch]$allowDirty
)
$ErrorActionPreference = "Stop"
Set-Location -LiteralPath $PSScriptRoot
Add-Type -AssemblyName System.IO.Compression.FileSystem

$resultFile = Join-Path $env:TEMP "iq-release-result.json"
function Complete-Release {
    param([string]$Status, [string]$Message, [string]$Archive = "")
    $color = if ($Status -eq 'SUCCESS') { 'Green' } else { 'Red' }
    Write-Host ""
    Write-Host "==================== RELEASE $Status ====================" -ForegroundColor $color
    if ($Archive) { Write-Host "  Archive: $Archive" -ForegroundColor $color }
    if ($Message) { Write-Host "  $Message" -ForegroundColor $color }
    Write-Host "=========================================================" -ForegroundColor $color
    @{ status = $Status; message = $Message; archive = $Archive; finished = (Get-Date -Format o) } |
        ConvertTo-Json | Set-Content -LiteralPath $resultFile -Encoding UTF8
    exit $(if ($Status -eq 'SUCCESS') { 0 } else { 1 })
}
trap { Complete-Release -Status 'FAILURE' -Message $_.Exception.Message }

# --- 1. A commit ---------------------------------------------------------------
$commit = (git rev-parse --short HEAD).Trim()
$dirty = git status --porcelain
if ($dirty) {
    if (-not $allowDirty) { Complete-Release -Status 'FAILURE' -Message "Uncommitted changes: commit first, or -allowDirty for a test release" }
    Write-Host "Uncommitted changes: a test release, not commit $commit's." -ForegroundColor Yellow
    $commit += "-dirty"
}
$version = (Select-String -Path "CMakeLists.txt" -Pattern '^\s*VERSION\s+([\d.]+)').Matches[0].Groups[1].Value
Write-Host "Ink & Quill $version ($commit)" -ForegroundColor Cyan

# --- 2. Build ------------------------------------------------------------------
if (-not $skipBuild) {
    & (Join-Path $PSScriptRoot "Build_Local.ps1") -noDeploy -config $config
    if ($LASTEXITCODE -ne 0) { Complete-Release -Status 'FAILURE' -Message "Build_Local.ps1 failed (see above)" }
}

# --- 3. Stage --------------------------------------------------------------------
$release = Join-Path $PSScriptRoot "build\release"
$stage = Join-Path $release "stage"
if (Test-Path $stage) { Remove-Item $stage -Recurse -Force }
$files = [ordered]@{
    "00 Core\SKSE\Plugins\InkAndQuill.dll"     = "build\$config\InkAndQuill.dll"
    "00 Core\InkAndQuill.esp"                  = "build\esp\InkAndQuill.esp"
    "01 Vanilla\Interface\book.swf"            = "Interface\book.swf"
    "02 Convenient Reading\Interface\book.swf" = "build\variants\convenient-reading\Interface\book.swf"
}
$folders = [ordered]@{
    "00 Core\Scripts"                = "Scripts"
    "00 Core\Source\Scripts"         = "Source\Scripts"
    "00 Core\Interface\Translations" = "Interface\Translations"
    "fomod"                          = "fomod"
}
foreach ($to in $files.Keys) {
    if (-not (Test-Path -LiteralPath $files[$to])) { throw "Missing $($files[$to]): build first" }
    New-Item -ItemType Directory -Force -Path (Split-Path (Join-Path $stage $to)) | Out-Null
    Copy-Item -LiteralPath $files[$to] -Destination (Join-Path $stage $to)
}
foreach ($to in $folders.Keys) {
    if (-not (Test-Path -LiteralPath $folders[$to])) { throw "Missing $($folders[$to])" }
    New-Item -ItemType Directory -Force -Path (Join-Path $stage $to) | Out-Null
    Copy-Item -Path (Join-Path $folders[$to] "*") -Destination (Join-Path $stage $to) -Recurse
}
# The FOMOD's version is CMakeLists.txt's.
$info = Join-Path $stage "fomod\info.xml"
(Get-Content -LiteralPath $info -Raw) -replace '<Version>[^<]*</Version>', "<Version>$version</Version>" |
    Set-Content -LiteralPath $info -Encoding UTF8 -NoNewline

# --- 4. Check --------------------------------------------------------------------
$problems = @()
foreach ($swf in "01 Vanilla\Interface\book.swf", "02 Convenient Reading\Interface\book.swf") {
    $bytes = [IO.File]::ReadAllBytes((Join-Path $stage $swf))
    $text = [Text.Encoding]::ASCII.GetString($bytes)
    if ($text.Substring(0, 3) -ne "FWS") { $problems += "$swf is compressed (writing would be off)" }
    if ($text -notmatch 'BOOKMENU_WRITING_INTERFACE=\d+') { $problems += "$swf has no writing marker" }
    if ($swf -like "01 Vanilla*" -and $text.Contains('Convenient Reading')) { $problems += "$swf contains Convenient Reading's code" }
}
function Get-Keys([string]$path) {
    [IO.File]::ReadAllText($path, [Text.Encoding]::Unicode) -split "`r?`n" |
        Where-Object { $_ -match '^\$' } | ForEach-Object { ($_ -split "`t", 2)[0] }
}
$english = Get-Keys (Join-Path $stage "00 Core\Interface\Translations\InkAndQuill_ENGLISH.txt")
foreach ($file in Get-ChildItem (Join-Path $stage "00 Core\Interface\Translations\*.txt")) {
    $diff = Compare-Object $english (Get-Keys $file.FullName)
    if ($diff) { $problems += "$($file.Name): keys differ from English ($(($diff | ForEach-Object InputObject) -join ', '))" }
}
[xml]$module = Get-Content -LiteralPath (Join-Path $stage "fomod\ModuleConfig.xml") -Raw
foreach ($folder in $module.SelectNodes("//folder")) {
    if (-not (Test-Path -LiteralPath (Join-Path $stage $folder.source))) { $problems += "ModuleConfig.xml installs missing folder '$($folder.source)'" }
}
if ($problems) { Complete-Release -Status 'FAILURE' -Message ("Checks failed:`n  - " + ($problems -join "`n  - ")) }

# --- 5. Zip ----------------------------------------------------------------------
# Named as the FOMOD names the mod (info.xml), which a mod manager may offer as the mod's name.
# MO2 guesses a name from the file name only up to a character it doesn't take ("&" left "Ink"): "and" instead.
$name = ([xml](Get-Content -LiteralPath $info -Raw)).fomod.Name -replace '\s*&\s*', ' and '
$name = ($name.ToCharArray() | ForEach-Object { if ([IO.Path]::GetInvalidFileNameChars() -contains $_) { '_' } else { $_ } }) -join ''
$archive = Join-Path $release "$name $version.zip"
if (Test-Path -LiteralPath $archive) { Remove-Item -LiteralPath $archive -Force }
[IO.Compression.ZipFile]::CreateFromDirectory($stage, $archive, [IO.Compression.CompressionLevel]::Optimal, $false)
$count = (Get-ChildItem $stage -Recurse -File).Count
$size = "{0:N0} KB" -f ((Get-Item -LiteralPath $archive).Length / 1KB)
Complete-Release -Status 'SUCCESS' -Archive $archive -Message "$count files, $size, from $commit"
