# install.ps1 - trust the test cert and load the Aegis driver.
# Run from an ELEVATED PowerShell. Requires test signing ON:
#   bcdedit /set testsigning on   (then reboot)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $MyInvocation.MyCommand.Path
$sys  = Join-Path $root 'build\AegisMon.sys'
$cer  = Join-Path $root 'build\AegisTest.cer'
$svc  = 'AegisMon'

if (-not (Test-Path $sys)) { throw "Driver not built: $sys (run build.cmd first)" }

# Warn if test signing is not enabled.
$ts = (bcdedit /enum '{current}' | Select-String 'testsigning\s+Yes')
if (-not $ts) {
    Write-Warning "Test signing does not appear to be ON. The driver will fail to load."
    Write-Warning "Run (elevated):  bcdedit /set testsigning on   then reboot."
}

Write-Host "[install] trusting test certificate in LocalMachine Root + TrustedPublisher..."
certutil -addstore -f Root           $cer | Out-Null
certutil -addstore -f TrustedPublisher $cer | Out-Null

# Recreate the service cleanly.
sc.exe query $svc *> $null
if ($LASTEXITCODE -eq 0) {
    Write-Host "[install] stopping/removing existing service..."
    sc.exe stop   $svc *> $null
    sc.exe delete $svc *> $null
    Start-Sleep -Milliseconds 500
}

Write-Host "[install] creating kernel service..."
sc.exe create $svc type= kernel start= demand binPath= "$sys" | Out-Null

Write-Host "[install] starting driver..."
sc.exe start $svc
if ($LASTEXITCODE -ne 0) {
    throw "sc start failed ($LASTEXITCODE). Check test signing and the System event log."
}

Write-Host "[install] AegisMon loaded. Now run:  .\build\AegisAgent.exe"
