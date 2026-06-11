@echo off
rem ===========================================================================
rem  Aegis build script - compiles the kernel driver (AegisMon.sys) and the
rem  user-mode agent (AegisAgent.exe), then test-signs the driver.
rem  Run from a normal cmd prompt; it sets up the MSVC env itself.
rem ===========================================================================
setlocal enabledelayedexpansion

set "WDKVER=10.0.26100.0"
set "KIT=C:\Program Files (x86)\Windows Kits\10"
set "VS=C:\Program Files\Microsoft Visual Studio\18\Community"
set "ROOT=%~dp0"
set "OUT=%ROOT%build"

call "%VS%\VC\Auxiliary\Build\vcvars64.bat" >nul || (echo [build] vcvars64 failed & goto :err)

set "INCKM=%KIT%\Include\%WDKVER%\km"
set "INCCRT=%KIT%\Include\%WDKVER%\km\crt"
set "INCSHARED=%KIT%\Include\%WDKVER%\shared"
set "LIBKM=%KIT%\Lib\%WDKVER%\km\x64"
set "TOOLS=%KIT%\bin\%WDKVER%\x64"

if not exist "%OUT%" mkdir "%OUT%"

rem ---- driver: compile every .c under driver\core and driver\modules ----------
echo [build] compiling driver...
set "DRVINC=/I"%ROOT%common" /I"%ROOT%driver\core" /I"%ROOT%driver\modules" /I"%INCKM%" /I"%INCCRT%" /I"%INCSHARED%""
set "DRVOBJS="
for %%f in ("%ROOT%driver\core\*.c" "%ROOT%driver\modules\*.c") do (
    rem wdmsec.lib is /GS-protected, so compile the driver with stack cookies too.
    cl /nologo /c /kernel /GS /W4 /Zi /Oi /D_WIN64 /D_AMD64_ /DNDEBUG ^
       %DRVINC% /Fo"%OUT%\%%~nf.obj" /Fd"%OUT%\AegisMon.pdb" "%%f" ^
       || (echo [build] driver compile failed: %%~nxf & goto :err)
    set "DRVOBJS=!DRVOBJS! "%OUT%\%%~nf.obj""
)

echo [build] linking driver...
rem GsDriverEntry initializes the kernel security cookie before our DriverEntry;
rem BufferOverflowFastFailK.lib supplies that wrapper and the /GS runtime.
link /nologo /DRIVER /SUBSYSTEM:NATIVE /ENTRY:GsDriverEntry ^
   /NODEFAULTLIB /INTEGRITYCHECK /DEBUG /MACHINE:X64 ^
   /LIBPATH:"%LIBKM%" /OUT:"%OUT%\AegisMon.sys" ^
   !DRVOBJS! ntoskrnl.lib hal.lib wdmsec.lib BufferOverflowFastFailK.lib ^
   || (echo [build] driver link failed & goto :err)

rem ---- agent: compile every .c under agent ------------------------------------
echo [build] compiling agent...
cl /nologo /W4 /Zi /I"%ROOT%common" /I"%ROOT%agent" ^
   /Fe"%OUT%\AegisAgent.exe" /Fo"%OUT%\\" /Fd"%OUT%\AegisAgent.pdb" ^
   "%ROOT%agent\Agent.c" || (echo [build] agent compile failed & goto :err)

rem ---- test cert + signing -----------------------------------------------------
rem  Ensure exactly one code-signing cert (CN=AegisTestCert) in CurrentUser\My,
rem  export the matching public .cer, and sign by its exact thumbprint so the
rem  signing cert always matches the cert install.ps1 trusts (no /n ambiguity).
echo [build] ensuring test certificate...
for /f "usebackq delims=" %%T in (`powershell -NoProfile -ExecutionPolicy Bypass -Command ^
  "$c = Get-ChildItem Cert:\CurrentUser\My | ? Subject -eq 'CN=AegisTestCert' | Select -First 1;" ^
  "if (-not $c) { $c = New-SelfSignedCertificate -Subject 'CN=AegisTestCert' -Type CodeSigningCert -CertStoreLocation Cert:\CurrentUser\My -KeyExportPolicy Exportable -NotAfter (Get-Date).AddYears(10) };" ^
  "Export-Certificate -Cert $c -FilePath '%OUT%\AegisTest.cer' -Force | Out-Null;" ^
  "$c.Thumbprint"`) do set "THUMB=%%T"
if not defined THUMB (echo [build] certificate step failed & goto :err)

echo [build] test-signing driver (thumbprint %THUMB%)...
"%TOOLS%\signtool.exe" sign /fd sha256 /sha1 %THUMB% ^
    "%OUT%\AegisMon.sys" || (echo [build] signtool failed & goto :err)

echo.
echo [build] SUCCESS
echo        driver : %OUT%\AegisMon.sys
echo        agent  : %OUT%\AegisAgent.exe
echo        cert   : %OUT%\AegisTest.cer
echo.
echo Next: run install.ps1 as Administrator to trust the cert and load the driver.
endlocal & exit /b 0

:err
echo [build] FAILED
endlocal & exit /b 1
