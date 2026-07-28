<#
    package-expansion-win.ps1 - Windows port of build-expansion.sh.

    Assembles the OMF "Kyra" (Nightshade) expansion from the tools built by
    build-win.ps1:
      1. Stage the original OMF:2097 assets as templates into the build resources.
      2. Inject the new Vance portrait into a copy of WORLD.PIC -> NIGHTSHD.PIC
         (falls back to a plain copy if Pillow-baked art is missing).
      3. Generate the two story tournaments (KYRA.TRN, RECKON.TRN).
      4. Create a ready-to-play test pilot (CHAMPION.CHR) in the save dir.

    Differs from the bash script only where the platform demands: uses the built
    .exe tools, Windows paths, and %APPDATA%\OpenOMF\save instead of the macOS
    "~/Library/Application Support" path.
#>
[CmdletBinding()]
param(
    [string]$BuildDir = "M:\omf-build",
    [string]$AssetsDir = "M:\omf-assets\OMF2097",
    [string]$SaveDir = "$env:APPDATA\OpenOMF\save"
)

$ErrorActionPreference = "Stop"
$repoRoot = $PSScriptRoot
$art = Join-Path $repoRoot "expansion-art"
$resources = Join-Path $BuildDir "resources"

New-Item -ItemType Directory -Force -Path $resources | Out-Null

# 1. Stage the original assets the generator uses as binary templates.
foreach ($file in "WORLD.PIC", "PLAYERS.PIC", "NORTH_AM.TRN", "WAR.TRN") {
    $src = Join-Path $AssetsDir $file
    if (-not (Test-Path $src)) { throw "Missing required asset: $src" }
    Copy-Item $src (Join-Path $resources $file) -Force
}
Write-Host "Staged original assets into $resources" -ForegroundColor Cyan

# 2. Inject the original Nightshade Concord cast into NIGHTSHD.PIC (fall back to
#    a plain WORLD.PIC copy if the portrait art is missing).
$portraitsDir = Join-Path $art "portraits"
$worldPic = Join-Path $resources "WORLD.PIC"
$nightPic = Join-Path $resources "NIGHTSHD.PIC"
$injected = $false
if (Test-Path $portraitsDir) {
    & (Join-Path $BuildDir "mkportrait.exe") $worldPic $portraitsDir $nightPic
    if ($LASTEXITCODE -eq 0) {
        Write-Host "Injected Nightshade Concord cast -> NIGHTSHD.PIC" -ForegroundColor Green
        $injected = $true
    } else {
        Write-Host "(portrait injection failed -- falling back to WORLD.PIC faces)" -ForegroundColor Yellow
    }
}
if (-not $injected) {
    Copy-Item $worldPic $nightPic -Force
    Write-Host "(using WORLD.PIC copy as NIGHTSHD.PIC)" -ForegroundColor Yellow
}

# 3. Generate the tournaments (templates in, KYRA.TRN / RECKON.TRN out).
& (Join-Path $BuildDir "expansion_gen.exe") $resources $resources $art
if ($LASTEXITCODE -ne 0) { throw "expansion_gen failed ($LASTEXITCODE)." }
Write-Host "Generated tournaments into $resources" -ForegroundColor Green

# 4. Create the ready-to-play test pilot.
New-Item -ItemType Directory -Force -Path $SaveDir | Out-Null
$playersPic = Join-Path $resources "PLAYERS.PIC"
$championChr = Join-Path $SaveDir "CHAMPION.CHR"
& (Join-Path $BuildDir "mkpilot.exe") $playersPic $championChr "CHAMPION" 0
if ($LASTEXITCODE -ne 0) { throw "mkpilot failed ($LASTEXITCODE)." }
Write-Host "Created test pilot -> $championChr" -ForegroundColor Green

Write-Host ""
Write-Host "Expansion packaged. Artifacts:" -ForegroundColor Green
Get-ChildItem $resources -Include KYRA.TRN, RECKON.TRN, NIGHTSHD.PIC -Recurse |
    Select-Object Name, @{n = 'KB'; e = { [math]::Round($_.Length / 1KB, 1) } } |
    Format-Table -AutoSize
