/*
 * AegisDriverProtocol.h - the contract between AegisMon (driver) and AegisAgent (app).
 *
 * One device with three names, and a single IOCTL the agent uses to pull data
 * from the driver. Right now that pull just returns a fixed greeting - the event
 * stream that will eventually flow through it doesn't exist yet, but the channel
 * does.
 *
 * Safe to include from kernel mode (after <ntddk.h>) and user mode
 * (after <windows.h> + <winioctl.h>): both supply CTL_CODE.
 */
#pragma once

/* One device, three names:
 *   AEGIS_DEVICE_NAME   the kernel-internal object the driver creates
 *   AEGIS_SYMLINK_NAME  an object-manager symlink pointing at it
 *   AEGIS_USERMODE_PATH what user mode's CreateFile() opens, which Win32
 *                       resolves through the \??\ symlink above. */
#define AEGIS_DEVICE_NAME    L"\\Device\\AegisMon"
#define AEGIS_SYMLINK_NAME   L"\\??\\AegisMon"
#define AEGIS_USERMODE_PATH  "\\\\.\\AegisMon"

/* Pull whatever the driver has for us (a fixed greeting, for now).
 * Device type FILE_DEVICE_UNKNOWN (not a literal 0x8000, which would shift into
 * the sign bit and make the control code a negative int). 0x800-0xFFF is the
 * vendor-defined function range. */
#define IOCTL_AEGIS_GET_EVENTS \
    CTL_CODE(FILE_DEVICE_UNKNOWN, 0x800, METHOD_BUFFERED, FILE_READ_DATA)
