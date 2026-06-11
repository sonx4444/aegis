# Aegis

A Windows kernel-mode EDR (Endpoint Detection & Response), built incrementally.
It starts as the smallest thing that matters: a kernel driver and a user-mode
agent that can **talk to each other**, then grows real eyes. The driver watches
process creation from the kernel, queues each event, and the agent pulls a
packed event stream over an IOCTL and prints it. Process is the first source;
the queue and the wire contract are built so new sources (thread, image, file,
network) slot in without touching the transport.

> ⚠️ **For test/educational use.** Aegis loads a test-signed kernel
> driver. Run it only on a machine with test signing enabled that you are willing
> to break — kernel bugs bluescreen the box.

## Components

| Name | Kind | Output | Role |
|------|------|--------|------|
| **AegisMon** | kernel driver | `AegisMon.sys` | Watches process create/exit, queues events, answers the IOCTL |
| **AegisAgent** | user-mode agent | `AegisAgent.exe` | Pulls the event stream and prints it |

## Layout

```
common/    AegisDriverProtocol.h  kernel⇄agent contract (event header + payloads)
driver/
  core/    Driver.c               device + symlink, IRP dispatch, the IOCTL handler
           EventQueue.c           spinlock-guarded FIFO bridging the IRQL gap
  modules/ ProcessMon.c           process create/exit notify callback
           Modules.h              the Start/Stop registry new modules plug into
agent/     Agent.c                pulls the packed event stream and prints it
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
.\build\AegisAgent.exe   # streams process events until Ctrl+C
.\uninstall.ps1          # stop & remove the driver
```

Expected output (launch a few programs while it runs):

```
AegisAgent: streaming process events. Ctrl+C to stop.
[18:42:07.391] #128   CREATE  pid=9240   ppid=6312   creator=6312   \Device\HarddiskVolume3\Windows\System32\notepad.exe
[18:42:09.004] #129   EXIT    pid=9240
```
