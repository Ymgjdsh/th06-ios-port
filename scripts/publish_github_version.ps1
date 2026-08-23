param(
    [Parameter(Mandatory = $true)]
    [string]$Message,

    [string]$Tag = ""
)

$ErrorActionPreference = 'Stop'
$Root = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
Set-Location $Root

if (-not (Test-Path -LiteralPath (Join-Path $Root '.git'))) {
    throw 'This source tree is not initialized as a Git repository.'
}

python ios/check_ios_source.py
if ($LASTEXITCODE -ne 0) {
    throw 'iOS source preflight failed; refusing to publish.'
}

git diff --check
if ($LASTEXITCODE -ne 0) {
    throw 'Git whitespace validation failed; refusing to publish.'
}

$maxGitHubFileBytes = 95MB
$candidatePaths = @(git ls-files --cached --others --exclude-standard)
foreach ($relativePath in $candidatePaths) {
    $absolutePath = Join-Path $Root $relativePath
    if ((Test-Path -LiteralPath $absolutePath -PathType Leaf) -and
        (Get-Item -LiteralPath $absolutePath).Length -gt $maxGitHubFileBytes) {
        throw "File exceeds the safe GitHub limit: $relativePath"
    }
}

git add --all
git diff --cached --quiet
if ($LASTEXITCODE -eq 0) {
    throw 'There are no source changes to publish.'
}

git commit -m $Message
if ($LASTEXITCODE -ne 0) {
    throw 'Git commit failed.'
}

if ($Tag) {
    if ($Tag -notmatch '^v[0-9]+\.[0-9]+\.[0-9]+([.-][0-9A-Za-z.-]+)?$') {
        throw 'Tag must look like v1.2.5 or v1.2.5-netplay-fix1.'
    }
    git tag -a $Tag -m $Message
    if ($LASTEXITCODE -ne 0) {
        throw 'Git tag creation failed.'
    }
}

git push origin HEAD:main
if ($LASTEXITCODE -ne 0) {
    throw 'GitHub push failed.'
}

if ($Tag) {
    git push origin $Tag
    if ($LASTEXITCODE -ne 0) {
        throw 'GitHub tag push failed.'
    }
}

Write-Host "Published commit to origin/main."
if ($Tag) {
    Write-Host "Published tag $Tag."
}
