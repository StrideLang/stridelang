<#
.SYNOPSIS
    Builds the Stride Language SDK, runs tests, and generates the installer packages (.exe / .zip).

.DESCRIPTION
    Configures CMake in Release mode, compiles stridec, executes the test suite,
    and runs CPack to produce the Windows NSIS (.exe) installer and portable .zip archive.

.EXAMPLE
    .\scripts\build_installer.ps1
#>

param (
    [string]$BuildDir = "build_installer",
    [string]$Config = "Release",
    [string]$PrebuiltBuildDir = "",
    [string]$StrideSrcDir = "",
    [switch]$SkipTests
)

$ErrorActionPreference = "Stop"

$RootDir = Split-Path -Parent $PSScriptRoot
Set-Location $RootDir

Write-Host "==============================================" -ForegroundColor Cyan
Write-Host " Building Stride Installer Package ($Config)  " -ForegroundColor Cyan
Write-Host "==============================================" -ForegroundColor Cyan

# 1. Configure
Write-Host "`n[1/4] Configuring CMake..." -ForegroundColor Yellow
$CmakeArgs = @("-B", $BuildDir, "-DCMAKE_BUILD_TYPE=$Config")
if ($PrebuiltBuildDir) {
    $CmakeArgs += "-DSTRIDE_PREBUILT_BUILD_DIR=$PrebuiltBuildDir"
}
if ($StrideSrcDir) {
    $CmakeArgs += "-DSTRIDE_SRC_DIR=$StrideSrcDir"
}
cmake @CmakeArgs

# 2. Build
Write-Host "`n[2/4] Compiling Stride Compiler ($Config)..." -ForegroundColor Yellow
cmake --build $BuildDir --config $Config

# 3. Test
if (-not $SkipTests) {
    Write-Host "`n[3/4] Running Verification Test Suite..." -ForegroundColor Yellow
    ctest --test-dir $BuildDir --output-on-failure -C $Config
} else {
    Write-Host "`n[3/4] Skipping tests (-SkipTests specified)..." -ForegroundColor Gray
}

# 4. Package (CPack)
Write-Host "`n[4/4] Generating Installer Packages with CPack..." -ForegroundColor Yellow
Push-Location $BuildDir
cpack -C $Config
Pop-Location

Write-Host "`n==============================================" -ForegroundColor Green
Write-Host " Installer Generation Completed!              " -ForegroundColor Green
Write-Host " Artifacts created in: $RootDir\$BuildDir" -ForegroundColor Green
Write-Host "==============================================" -ForegroundColor Green
Get-ChildItem -Path "$RootDir\$BuildDir" -Include *.exe, *.zip, *.msi, *.tar.gz -Recurse | Select-Object Name, Length, LastWriteTime | Format-Table -AutoSize
