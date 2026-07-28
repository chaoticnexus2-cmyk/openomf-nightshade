<#
    build-win.ps1 - Configure and build OpenOMF Nightshade (with the Kyra
    expansion tools) on Windows using VS2019 Build Tools + vcpkg.

    C: is full on this machine, so TEMP and the vcpkg binary cache are
    redirected to M: to keep the build off the system drive.
#>
[CmdletBinding()]
param(
    [string]$VcpkgRoot = "M:\vcpkg",
    [string]$BuildDir = "M:\omf-build",
    [string]$ScratchRoot = "M:\omf-build-temp"
)

$ErrorActionPreference = "Stop"
$repoRoot = $PSScriptRoot

# --- Keep all scratch/cache off the full C: drive --------------------------
New-Item -ItemType Directory -Force -Path $ScratchRoot | Out-Null
New-Item -ItemType Directory -Force -Path "$VcpkgRoot\bincache" | Out-Null
$env:TEMP = $ScratchRoot
$env:TMP = $ScratchRoot
$env:VCPKG_DEFAULT_BINARY_CACHE = "$VcpkgRoot\bincache"
$env:VCPKG_DOWNLOADS = "$VcpkgRoot\downloads"

# --- Import the VS2019 x64 developer environment ---------------------------
$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
$vsPath = & $vswhere -latest -products * `
    -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 `
    -property installationPath
if (-not $vsPath) { throw "No VS C++ toolset found." }
$vcvars = "$vsPath\VC\Auxiliary\Build\vcvars64.bat"

Write-Host "Importing VS environment from $vcvars ..." -ForegroundColor Cyan
$envDump = & cmd /c "`"$vcvars`" >nul 2>&1 && set"
foreach ($line in $envDump) {
    if ($line -match "^([^=]+)=(.*)$") {
        Set-Item -Path "Env:$($matches[1])" -Value $matches[2]
    }
}
# Re-assert scratch redirection (vcvars may reset TEMP/TMP).
$env:TEMP = $ScratchRoot
$env:TMP = $ScratchRoot

$cmake = "$vsPath\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
$ninja = "$vsPath\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe"
$ninjaDir = Split-Path $ninja
$env:PATH = "$ninjaDir;$env:PATH"

# --- Configure -------------------------------------------------------------
Write-Host "Configuring (this triggers the vcpkg dependency build) ..." -ForegroundColor Cyan
& $cmake -G Ninja `
    -S $repoRoot -B $BuildDir `
    -DCMAKE_BUILD_TYPE=Release `
    -DCMAKE_TOOLCHAIN_FILE="$VcpkgRoot\scripts\buildsystems\vcpkg.cmake" `
    -DBUILD_EXPANSION=ON
if ($LASTEXITCODE -ne 0) { throw "CMake configure failed ($LASTEXITCODE)." }

# --- Build -----------------------------------------------------------------
Write-Host "Building expansion tools + game ..." -ForegroundColor Cyan
& $cmake --build $BuildDir --target expansion_gen mkpilot mkportrait openomf
if ($LASTEXITCODE -ne 0) { throw "Build failed ($LASTEXITCODE)." }

Write-Host "Build complete. Artifacts in $BuildDir" -ForegroundColor Green
