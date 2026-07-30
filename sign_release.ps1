# Signs jdBasic's own binaries with the Certum SimplySign code-signing cert,
# then rebuilds the release zips + sha256 so the published archives carry the
# signed binaries.
#
# Prereq: SimplySign Desktop must be running and LOGGED IN (SimplySign mobile
# token) so the cloud cert is mounted into Cert:\CurrentUser\My. A PIN dialog
# from SimplySign Desktop appears during signing.
#
# Usage:   powershell -ExecutionPolicy Bypass -File .\sign_release.ps1
#          powershell ... -File .\sign_release.ps1 -Thumbprint <40-hex>   (override)
param(
    [string]$Thumbprint,
    [string]$TimestampUrl = "http://time.certum.pl"
)
$ErrorActionPreference = "Stop"
$repo = Split-Path -Parent $MyInvocation.MyCommand.Path

# 1. Locate signtool (latest installed Windows SDK).
$signtool = Get-ChildItem "C:\Program Files (x86)\Windows Kits\10\bin\*\x64\signtool.exe" -ErrorAction SilentlyContinue |
    Sort-Object FullName | Select-Object -Last 1 -ExpandProperty FullName
if (-not $signtool) { throw "signtool.exe not found - install the Windows 10/11 SDK." }
Write-Host "signtool: $signtool"

# 2. Resolve the code-signing cert (SimplySign virtual card must be mounted).
if (-not $Thumbprint) {
    $cert = Get-ChildItem Cert:\CurrentUser\My -CodeSigningCert -ErrorAction SilentlyContinue | Select-Object -First 1
    if (-not $cert) {
        throw "No code-signing cert in CurrentUser\My. Start SimplySign Desktop and log in first."
    }
    $Thumbprint = $cert.Thumbprint
    Write-Host "Cert:     $($cert.Subject)  [$Thumbprint]  expires $($cert.NotAfter)"
}

# 3. OUR binaries only - third-party DLLs (SDL3*, LLVM-C, libssl/libcrypto)
#    are already vendor-signed and must not be re-signed.
$targets = @(
    "release\jdbasic-core-windows-x64\jdBasic.exe",
    "release\jdbasic-mcp-native-windows-x64\jdBasic.exe",
    "release\jdbasic-mcp-native-windows-x64\jdbrt.dll",
    "release\jdbasic-vibe-game-pack-windows-x64\jdBasic.exe",
    "release\jdbasic-vb6-windows-x64\jdBasic.exe"
)

# 4. Sign.
foreach ($rel in $targets) {
    $f = Join-Path $repo $rel
    if (-not (Test-Path $f)) { Write-Warning "skip (missing): $rel"; continue }
    Write-Host "`nSigning $rel ..."
    & $signtool sign /sha1 $Thumbprint /fd SHA256 /tr $TimestampUrl /td SHA256 $f
    if ($LASTEXITCODE -ne 0) { throw "sign FAILED: $rel" }
}

# 5. Verify against the full chain.
Write-Host "`n-- verify --"
foreach ($rel in $targets) {
    $f = Join-Path $repo $rel
    if (-not (Test-Path $f)) { continue }
    & $signtool verify /pa /v $f | Out-Null
    if ($LASTEXITCODE -ne 0) { throw "verify FAILED: $rel" }
    Write-Host "OK  $rel"
}

# 6. Rebuild zips + sha256 (signed bytes differ from the published archives).
$bundles = @(
    "jdbasic-core-windows-x64",
    "jdbasic-mcp-native-windows-x64",
    "jdbasic-vibe-game-pack-windows-x64",
    "jdbasic-vb6-windows-x64"
)
Write-Host "`n-- repackage --"
foreach ($b in $bundles) {
    $dir = Join-Path $repo "release\$b"
    $zip = Join-Path $repo "release\$b.zip"
    if (Test-Path $zip) { Remove-Item $zip -Force }
    Compress-Archive -Path "$dir\*" -DestinationPath $zip -Force
    $hash = (Get-FileHash -Algorithm SHA256 $zip).Hash
    Set-Content -Path "$zip.sha256" -Value $hash -Encoding ascii
    Write-Host "OK  $b.zip  sha256=$hash"
}
Write-Host "`nDone. Re-upload the four zips + .sha256 to the GitHub release."
