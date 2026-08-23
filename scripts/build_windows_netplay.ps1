param(
    [ValidateSet('Win32', 'x64')]
    [string]$Architecture = 'Win32',
    [switch]$Clean
)

$ErrorActionPreference = 'Stop'
$Root = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$BuildDir = Join-Path $Root ("build_windows_netplay_{0}" -f $Architecture.ToLowerInvariant())
$StageDir = Join-Path $Root ("dist\th06-windows-netplay-{0}" -f $Architecture.ToLowerInvariant())
$ZipPath = Join-Path $Root ("dist\th06-windows-netplay-{0}.zip" -f $Architecture.ToLowerInvariant())

if ($Clean -and (Test-Path -LiteralPath $BuildDir)) {
    Remove-Item -LiteralPath $BuildDir -Recurse -Force
}

$cmakeCandidates = @(
    (Get-Command cmake.exe -ErrorAction SilentlyContinue | Select-Object -ExpandProperty Source -First 1),
    'C:\Program Files\Microsoft Visual Studio\2022\Professional\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe',
    'C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe',
    'C:\Program Files\CMake\bin\cmake.exe'
)
$CMake = $cmakeCandidates | Where-Object { $_ -and (Test-Path -LiteralPath $_) } | Select-Object -First 1
if (-not $CMake) {
    throw 'CMake was not found. Install CMake or Visual Studio C++ desktop tools.'
}

& $CMake -S $Root -B $BuildDir -A $Architecture -DTH06_USE_SDL2=ON -DTH06_ENABLE_PREDICTION_ROLLBACK=OFF
if ($LASTEXITCODE -ne 0) { throw 'CMake configure failed.' }
& $CMake --build $BuildDir --config Release --target th06 --parallel 4
if ($LASTEXITCODE -ne 0) { throw 'Windows build failed.' }

$OutputDir = Join-Path $BuildDir 'Release'
if (-not (Test-Path -LiteralPath (Join-Path $OutputDir 'th06.exe'))) {
    throw 'Build completed without th06.exe.'
}

if (Test-Path -LiteralPath $StageDir) { Remove-Item -LiteralPath $StageDir -Recurse -Force }
New-Item -ItemType Directory -Path $StageDir -Force | Out-Null
Copy-Item -LiteralPath (Join-Path $OutputDir 'th06.exe') -Destination $StageDir
foreach ($dll in @('SDL2.dll', 'SDL2_image.dll', 'SDL2_mixer.dll')) {
    $source = Join-Path $OutputDir $dll
    if (-not (Test-Path -LiteralPath $source)) {
        throw "Missing runtime DLL: $dll"
    }
    Copy-Item -LiteralPath $source -Destination $StageDir
}

$fontDir = Join-Path $StageDir 'font'
if (Test-Path -LiteralPath (Join-Path $OutputDir 'font')) {
    Copy-Item -LiteralPath (Join-Path $OutputDir 'font') -Destination $StageDir -Recurse
}

foreach ($asset in Get-ChildItem -LiteralPath (Join-Path $Root 'ios\assets') -File -Include '*.DAT', '*.dat') {
    Copy-Item -LiteralPath $asset.FullName -Destination $StageDir
}
$bgmDir = Join-Path $StageDir 'bgm'
New-Item -ItemType Directory -Path $bgmDir -Force | Out-Null
foreach ($track in Get-ChildItem -LiteralPath (Join-Path $Root 'ios\bgm') -File -Include '*.ogg', '*.OGG') {
    Copy-Item -LiteralPath $track.FullName -Destination $bgmDir
}

$readme = @'
Touhou 06 Windows netplay build

Run th06.exe from this folder. Windows and iOS devices can use:
  Nearby LAN       same Wi-Fi; both devices use the same room port
  Direct address   enter the host IPv4 address and the same room port
  Relay room       enter the same relay endpoint and room code

For example, set the room port to 3037 on both Windows and iOS. The launcher
uses one port for discovery, the host socket, and the guest socket.

The Bluetooth nearby row is kept for menu parity with iOS but is disabled on
Windows. iOS Bluetooth nearby uses Apple's MultipeerConnectivity and cannot be
joined by a Windows desktop transport. Use Nearby LAN, Direct address, or Relay.

If Windows Defender Firewall asks, allow th06.exe on Private networks.
'@
Set-Content -LiteralPath (Join-Path $StageDir 'README-netplay.txt') -Value $readme -Encoding ASCII

# Start in a 640x480 window. Fullscreen is still available from the in-game
# configuration, but windowed startup avoids old Win10 display-driver/FBO
# crashes before the title screen is fully initialized.
$defaultConfig = New-Object byte[] 56
$defaultConfig[2] = 1
for ($i = 6; $i -le 17; $i++) { $defaultConfig[$i] = 255 }
$defaultConfig[20] = 2
$defaultConfig[21] = 1
$defaultConfig[24] = 2
$defaultConfig[25] = 3
$defaultConfig[27] = 1
$defaultConfig[28] = 1
$defaultConfig[29] = 1
$defaultConfig[30] = 1 # windowed startup
$defaultConfig[32] = 0x58
$defaultConfig[33] = 0x02
$defaultConfig[34] = 0x58
$defaultConfig[35] = 0x02
$defaultConfig[37] = 1
$defaultConfig[52] = 1
[IO.File]::WriteAllBytes((Join-Path $StageDir 'th06.cfg'), $defaultConfig)

if (Test-Path -LiteralPath $ZipPath) {
    Remove-Item -LiteralPath $ZipPath -Force
}
Compress-Archive -Path (Join-Path $StageDir '*') -DestinationPath $ZipPath -CompressionLevel Optimal
$zipHash = (Get-FileHash -LiteralPath $ZipPath -Algorithm SHA256).Hash.ToLowerInvariant()
Set-Content -LiteralPath ($ZipPath + '.sha256.txt') -Value ("{0}  {1}" -f $zipHash, (Split-Path $ZipPath -Leaf)) -Encoding ASCII

Write-Host "Created playable folder: $StageDir"
Write-Host "Created ZIP package: $ZipPath"
Write-Host "SHA-256: $zipHash"
