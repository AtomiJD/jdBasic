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
#          powershell ... -File .\sign_release.ps1 -Only vibe             (one bundle)
#          powershell ... -File .\sign_release.ps1 -Only vibe -WhatIf     (show, do nothing)
#
# -Only takes one or more bundle names, matched as a substring against the pack
# list below, and restricts both the signing and the repackaging to them. Reach
# for it when a single pack was rebuilt: signing a pack again gives it a fresh
# timestamp, which changes its zip bytes and hash, so an already-published
# archive would no longer match what the release notes claim.
param(
    [string]$Thumbprint,
    [string]$TimestampUrl = "http://time.certum.pl",
    [string[]]$Only,
    [switch]$WhatIf
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

# 3. Bundle -> OUR binaries inside it. Third-party DLLs (SDL3*, LLVM-C,
#    libssl/libcrypto) are already vendor-signed and must not be re-signed.
#    One map rather than a target list plus a bundle list, so -Only cannot
#    filter the two out of step and sign a pack it then fails to rezip.
$packs = [ordered]@{
    "jdbasic-core-windows-x64"           = @("jdBasic.exe")
    "jdbasic-mcp-native-windows-x64"     = @("jdBasic.exe", "jdbrt.dll")
    "jdbasic-vibe-game-pack-windows-x64" = @("jdBasic.exe")
    "jdbasic-vb6-windows-x64"            = @("jdBasic.exe")
}

$bundles = @($packs.Keys)
if ($Only) {
    # `powershell -File script.ps1 -Only vibe,vb6` hands the whole thing over as
    # one string - the -File argument parser does not split on commas the way the
    # -Command parser does - so split here and accept both invocation styles.
    $wanted = @()
    foreach ($o in $Only) {
        foreach ($part in ($o -split ',')) {
            $part = $part.Trim()
            if ($part) { $wanted += $part }
        }
    }
    $picked = @()
    foreach ($o in $wanted) {
        $hits = @($packs.Keys | Where-Object { $_ -eq $o -or $_ -like "*$o*" })
        if ($hits.Count -eq 0) {
            throw "-Only '$o' matches no bundle. Known: $($packs.Keys -join ', ')"
        }
        if ($hits.Count -gt 1) {
            throw "-Only '$o' is ambiguous, it matches: $($hits -join ', ')"
        }
        $picked += $hits[0]
    }
    $bundles = @($picked | Select-Object -Unique)
}

$targets = @()
foreach ($b in $bundles) {
    foreach ($bin in $packs[$b]) { $targets += "release\$b\$bin" }
}

Write-Host "Bundles:  $($bundles -join ', ')"
if ($WhatIf) {
    Write-Host "`n-- WhatIf: would sign --"
    foreach ($rel in $targets) {
        $mark = "missing"
        if (Test-Path (Join-Path $repo $rel)) { $mark = "present" }
        Write-Host "  $rel  [$mark]"
    }
    Write-Host "`n-- WhatIf: would rezip --"
    foreach ($b in $bundles) { Write-Host "  release\$b.zip  + .sha256" }
    Write-Host "`nNothing signed, nothing written."
    exit 0
}

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
Write-Host "`nDone. Re-upload these $($bundles.Count) zip(s) + .sha256 to BOTH releases:"
Write-Host "  jdbasic-mcp (pre-release) and v1.0.0 (the Latest users see),"
Write-Host "  then update each release body so its sha256 and build number match."
