# Aegis

A Windows kernel-mode EDR (Endpoint Detection & Response), built incrementally.
It starts as the smallest thing that matters: a kernel driver and a user-mode
agent that can **talk to each other**. The driver creates a device, the agent
opens it and pulls data over an IOCTL. There's no telemetry yet — the driver
answers with a fixed greeting — but the channel everything else will ride on is
in place.

> ⚠️ **For test/educational use.** Aegis loads a test-signed kernel
> driver. Run it only on a machine with test signing enabled that you are willing
> to break — kernel bugs bluescreen the box.

## Components

| Name | Kind | Output | Role |
|------|------|--------|------|
| **AegisMon** | kernel driver | `AegisMon.sys` | Creates the device, dispatches IRPs, answers the IOCTL |
| **AegisAgent** | user-mode agent | `AegisAgent.exe` | Opens the device and pulls from it |

## Layout

```
common/   AegisDriverProtocol.h  kernel⇄agent contract (device names + IOCTL)
driver/
  core/   Driver.c               device + symlink, IRP dispatch, the IOCTL handler
agent/    Agent.c                opens the device, sends one IOCTL, prints the reply
build.cmd  install.ps1  uninstall.ps1
```

## Prerequisites

- Visual Studio 2026 (MSVC) + Windows SDK & **WDK 10.0.26100**
- x64 Windows with test signing on, then reboot:
  ```
  bcdedit /set testsigning on
  ```

## Build

From a normal prompt (it sets up the MSVC environment itself):

```
build.cmd
```

Produces `build\AegisMon.sys` (test-signed) and `build\AegisAgent.exe`.

## Run

In an **elevated** PowerShell:

```powershell
.\install.ps1            # trusts the test cert, loads the AegisMon service
.\build\AegisAgent.exe   # opens the device and prints the driver's reply
.\uninstall.ps1          # stop & remove the driver
```

Expected output:

```
driver says: hello from kernel (18 bytes)
```
