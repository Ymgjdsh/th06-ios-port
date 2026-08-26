[CmdletBinding()]
param(
    [string]$MacHost = $env:TH06_MAC_HOST,
    [string]$MacUser = $env:TH06_MAC_USER,
    [int]$MacPort = 22,
    [string]$KeyPath = "$env:USERPROFILE\.ssh\th07_mac",
    [string]$RemoteFolder = "th06-build",
    [string]$XcodeApp = "/Applications/Xcode.app",
    [string]$IosVersion = "1.3.1",
    [int]$IosBuild = 24,
    [switch]$OverwriteDesktop
)

$ErrorActionPreference = "Stop"
$root = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path

if ([string]::IsNullOrWhiteSpace($MacHost) -or [string]::IsNullOrWhiteSpace($MacUser)) {
    throw "Set MacHost/MacUser or TH06_MAC_HOST/TH06_MAC_USER."
}
if ($RemoteFolder -notmatch '^[A-Za-z0-9._/-]+$' -or
    $RemoteFolder.StartsWith("/") -or $RemoteFolder.Contains("..")) {
    throw "RemoteFolder must be a relative path without '..'."
}
if ($XcodeApp -notmatch '^/[A-Za-z0-9._/ -]+$') {
    throw "XcodeApp contains unsupported characters."
}
if ($IosVersion -notmatch '^[0-9]+\.[0-9]+\.[0-9]+$') {
    throw "IosVersion must look like 1.2.5."
}
if ($IosBuild -lt 1) { throw "IosBuild must be positive." }

foreach ($tool in @("ssh", "scp", "python")) {
    if (-not (Get-Command $tool -ErrorAction SilentlyContinue)) {
        throw "Missing required local tool: $tool"
    }
}
if (-not (Test-Path -LiteralPath $KeyPath -PathType Leaf)) {
    throw "SSH private key not found: $KeyPath"
}

Write-Host "Running local iOS source preflight ..."
& python (Join-Path $root "ios\check_ios_source.py") --root $root
if ($LASTEXITCODE -ne 0) { throw "iOS source preflight failed." }
& git -C $root diff --check
if ($LASTEXITCODE -ne 0) { throw "Git whitespace validation failed." }

$commit = (& git -C $root rev-parse --short HEAD).Trim()
if ($LASTEXITCODE -ne 0 -or [string]::IsNullOrWhiteSpace($commit)) {
    throw "Could not determine the source commit."
}
$runId = "${commit}-$(Get-Date -Format yyyyMMdd-HHmmss)"
$archiveDir = Join-Path $root "dist\remote-upload"
$archivePath = Join-Path $archiveDir "th06-ios-source-$commit.zip"
$logDir = Join-Path $root "dist\mac-build\logs-$runId"
$desktopName = "th06-ios-${IosVersion}-${IosBuild}-$commit.ipa"

New-Item -ItemType Directory -Force -Path $archiveDir | Out-Null
Write-Host "Packaging source snapshot ..."
& python (Join-Path $root "scripts\package_ios_source.py") --root $root --output $archivePath
if ($LASTEXITCODE -ne 0) { throw "Source packaging failed." }

$target = "${MacUser}@${MacHost}"
$sshOptions = @(
    "-i", $KeyPath, "-p", "$MacPort", "-o", "BatchMode=yes",
    "-o", "StrictHostKeyChecking=accept-new", "-o", "ConnectTimeout=8",
    "-o", "ServerAliveInterval=15", "-o", "ServerAliveCountMax=4"
)
$scpOptions = @(
    "-i", $KeyPath, "-P", "$MacPort", "-o", "BatchMode=yes",
    "-o", "StrictHostKeyChecking=accept-new", "-o", "ConnectTimeout=8"
)
$remoteBase = "`$HOME/$RemoteFolder"
$remoteRun = "$remoteBase/runs/$runId"
$remoteSource = "$remoteRun/source"
$remoteIpa = "$remoteRun/output/th06-ios-$IosVersion-$IosBuild.ipa"
$remoteDesktop = "`$HOME/Desktop/$desktopName"

try {
    Write-Host "Checking SSH connection to $target ..."
    & ssh @sshOptions $target "printf SSH_OK"
    if ($LASTEXITCODE -ne 0) { throw "SSH connection failed." }

    & ssh @sshOptions $target "mkdir -p `"$remoteBase/incoming`" `"$remoteBase/runs`""
    if ($LASTEXITCODE -ne 0) { throw "Could not create the remote build directory." }

    Write-Host "Uploading source snapshot ..."
    & scp @scpOptions $archivePath "${target}:${RemoteFolder}/incoming/source-$runId.zip"
    if ($LASTEXITCODE -ne 0) { throw "Source upload failed." }

    $prepare = @"
set -eu
BASE=$remoteBase
RUN=$remoteRun
rm -rf "`$RUN"
mkdir -p "`$RUN"
python3 -c 'import sys, zipfile; zipfile.ZipFile(sys.argv[1]).extractall(sys.argv[2])' "`$BASE/incoming/source-$runId.zip" "`$RUN"
mv "`$RUN/th06-ios14-netplay-v1.2.5-source" "`$RUN/source"
rm -f "`$BASE/incoming/source-$runId.zip"
mkdir -p "`$RUN/output"
chmod +x "`$RUN/source"/ios/*.sh "`$RUN/source"/ios/*.py "`$RUN/source"/scripts/*.py
"@ -replace "`r`n", "; " -replace "`n", "; "
    & ssh @sshOptions $target $prepare
    if ($LASTEXITCODE -ne 0) { throw "Remote source preparation failed." }

    $build = @"
set -eu
export PATH='/Applications/CMake.app/Contents/bin:/usr/local/bin:/opt/homebrew/bin:/usr/bin:/bin':`$PATH
export DEVELOPER_DIR='$XcodeApp/Contents/Developer'
cd "$remoteSource"
IOS_VERSION='$IosVersion' IOS_BUILD='$IosBuild' BUILD_DIR="$remoteRun/build-ios" OUTPUT_IPA="$remoteIpa" CLEAN_BUILD=1 ./ios/build_ios.sh
test -s "$remoteIpa"
unzip -tq "$remoteIpa"
CHECK_DIR="`$(mktemp -d "`$TMPDIR/th06-ipa-check.XXXXXX")"
trap 'rm -rf "`$CHECK_DIR"' EXIT
unzip -q "$remoteIpa" -d "`$CHECK_DIR"
APP="`$CHECK_DIR/Payload/th06.app"
test -x "`$APP/th06"
test "`$(/usr/libexec/PlistBuddy -c 'Print :CFBundleShortVersionString' "`$APP/Info.plist")" = '$IosVersion'
test "`$(/usr/libexec/PlistBuddy -c 'Print :CFBundleVersion' "`$APP/Info.plist")" = '$IosBuild'
shasum -a 256 "$remoteIpa"
"@ -replace "`r`n", "; " -replace "`n", "; "
    Write-Host "Building on macOS with Xcode ..."
    & ssh @sshOptions $target $build
    $buildExit = $LASTEXITCODE
    if ($buildExit -ne 0) {
        New-Item -ItemType Directory -Force -Path $logDir | Out-Null
        & scp @scpOptions "${target}:${RemoteFolder}/runs/$runId/build-ios/logs/build-ios.log" $logDir 2>$null
        & scp @scpOptions "${target}:${RemoteFolder}/runs/$runId/build-ios/logs/last-120-lines.txt" $logDir 2>$null
        throw "Remote build failed with exit code $buildExit. Logs: $logDir"
    }

    if (-not $OverwriteDesktop) {
        & ssh @sshOptions $target "test ! -e `"$remoteDesktop`""
        if ($LASTEXITCODE -ne 0) { throw "Desktop output already exists: $desktopName. Use -OverwriteDesktop to replace it." }
    }
    $install = @"
set -eu
IPA="$remoteIpa"
DESKTOP="$remoteDesktop"
mkdir -p "`$HOME/Desktop"
TMP="`$DESKTOP.tmp"
rm -f "`$TMP"
install -m 0644 "`$IPA" "`$TMP"
mv -f "`$TMP" "`$DESKTOP"
test -s "`$DESKTOP"
shasum -a 256 "`$DESKTOP"
"@ -replace "`r`n", "; " -replace "`n", "; "
    Write-Host "Installing verified IPA on the Mac desktop ..."
    & ssh @sshOptions $target $install
    if ($LASTEXITCODE -ne 0) { throw "Could not place the IPA on the Mac desktop." }

    Write-Host "Cleaning temporary source and build files on the Mac ..."
    & ssh @sshOptions $target "rm -rf `"$remoteRun`" `"$remoteBase/incoming/source-$runId.zip`""
    if ($LASTEXITCODE -ne 0) { throw "IPA is on the desktop, but remote cleanup failed: $remoteRun" }
    Write-Host "SUCCESS: /Users/$MacUser/Desktop/$desktopName"
}
finally {
    if ($target -and $remoteRun) {
        & ssh @sshOptions $target "rm -rf `"$remoteRun`" `"$remoteBase/incoming/source-$runId.zip`"" 2>$null
    }
    Remove-Item -LiteralPath $archivePath -Force -ErrorAction SilentlyContinue
    Remove-Item -LiteralPath ($archivePath + ".sha256.txt") -Force -ErrorAction SilentlyContinue
}
