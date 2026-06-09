# uninstall.ps1 - stop and remove the AegisMon driver. Run ELEVATED.
foreach ($svc in @('AegisMon')) {
    sc.exe query $svc *> $null
    if ($LASTEXITCODE -eq 0) {
        Write-Host "[uninstall] stopping/removing $svc..."
        sc.exe stop   $svc *> $null
        sc.exe delete $svc *> $null
    }
}
Write-Host "[uninstall] done. (Test cert left in the store; remove via certmgr if desired.)"
