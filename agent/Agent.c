/*
 * Agent.c - AegisAgent.exe.
 *
 * Opens the AegisMon device, sends one IOCTL, and prints what the driver answers
 * with. That's the whole user side of the channel for now; it will grow into a
 * streaming pull loop once the driver has real events to hand over.
 */
#include <windows.h>
#include <winioctl.h>
#include <stdio.h>
#include "AegisDriverProtocol.h"

int main(void)
{
    /* CreateFile on \\.\AegisMon -> Win32 resolves it through the \??\AegisMon
     * symlink to the \Device\AegisMon object -> the I/O manager sends our driver
     * an IRP_MJ_CREATE, which AegisCreateClose completes. */
    HANDLE h = CreateFileA(AEGIS_USERMODE_PATH, GENERIC_READ, 0, NULL,
                           OPEN_EXISTING, 0, NULL);
    if (h == INVALID_HANDLE_VALUE) {
        fprintf(stderr, "Cannot open %s (error %lu). Is AegisMon loaded, "
                        "and are you running elevated?\n",
                AEGIS_USERMODE_PATH, GetLastError());
        return 1;
    }

    char buf[256];
    DWORD returned = 0;
    BOOL ok = DeviceIoControl(h, IOCTL_AEGIS_GET_EVENTS, NULL, 0,
                              buf, sizeof(buf), &returned, NULL);
    if (!ok) {
        fprintf(stderr, "DeviceIoControl failed (error %lu)\n", GetLastError());
        CloseHandle(h);
        return 1;
    }

    /* 'returned' is the driver's IoStatus.Information - the byte count it copied
     * back. The greeting includes its NUL, so it prints as a plain C string. */
    printf("driver says: %s (%lu bytes)\n", buf, returned);

    CloseHandle(h);
    return 0;
}
