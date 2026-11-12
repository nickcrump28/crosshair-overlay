# Signs CrosshairOverlay-v*.exe so Windows shows "simplysoul" as the publisher
# instead of "Unknown publisher". Run on Windows in PowerShell, next to the exe.
#
# Option A - Azure Artifact Signing (about $10/month, individuals in the US/Canada):
#   1. Install the Windows SDK (for signtool.exe) and the Artifact Signing client tools
#      (winget install -e --id Microsoft.Azure.ArtifactSigningClientTools).
#   2. Fill in metadata.json:
#      { "Endpoint": "https://<region>.codesigning.azure.net",
#        "CodeSigningAccountName": "<account>", "CertificateProfileName": "<profile>" }
#   3. Sign in with "az login", then:  .\sign-windows.ps1 -Artifact -Dlib "<path to Azure.CodeSigning.Dlib.dll>"
#
# Option B - an OV/EV certificate from a certificate authority (on its USB token / in your cert store):
#   .\sign-windows.ps1 -Thumbprint <certificate thumbprint>
param([switch]$Artifact, [string]$Dlib, [string]$Metadata = "metadata.json", [string]$Thumbprint)
$ErrorActionPreference = "Stop"
$exe = Get-ChildItem -Filter "CrosshairOverlay-v*.exe" | Sort-Object LastWriteTime | Select-Object -Last 1
$signtool = Get-ChildItem "${env:ProgramFiles(x86)}\Windows Kits\10\bin\*\x64\signtool.exe" | Sort-Object FullName | Select-Object -Last 1
if (-not $signtool) { throw "signtool.exe not found - install the Windows SDK." }
$ts = "http://timestamp.acs.microsoft.com"
if ($Artifact) {
    & $signtool.FullName sign /v /fd SHA256 /tr $ts /td SHA256 /dlib $Dlib /dmdf $Metadata $exe.FullName
} elseif ($Thumbprint) {
    & $signtool.FullName sign /v /fd SHA256 /tr "http://timestamp.digicert.com" /td SHA256 /sha1 $Thumbprint $exe.FullName
} else { throw "Pass -Artifact (with -Dlib) or -Thumbprint." }
& $signtool.FullName verify /pa /v $exe.FullName
Write-Host "Signed $($exe.Name)"
