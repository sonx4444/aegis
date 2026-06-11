/*
 * Agent.c - AegisAgent.exe: reads the event stream from AegisMon and prints it.
 *
 * Pulls a packed buffer of events via IOCTL_AEGIS_GET_EVENTS, walks it by
 * header->Size, and prints one human-readable line per event. The pull loop is
 * the whole user side for now; a real sensor will dispatch these somewhere.
 */
#include <windows.h>
#include <winioctl.h>
#include <stdio.h>
#include <stdlib.h>
#include "AegisDriverProtocol.h"

#define READ_BUFFER (64u * 1024u)   /* unsigned: matches DeviceIoControl's DWORD size arg */
#define IDLE_SLEEP_MS 200           /* nothing queued: back off before polling again */

C_ASSERT(READ_BUFFER >= AEGIS_MAX_EVENT_SIZE);

/* Render the event header's system-time stamp as local HH:MM:SS.mmm. */
static void
FormatTimestamp(LARGE_INTEGER systemTime, char *out, size_t cap)
{
    FILETIME ft, local;
    SYSTEMTIME st;

    ft.dwLowDateTime  = systemTime.LowPart;
    ft.dwHighDateTime = (DWORD)systemTime.HighPart;

    if (FileTimeToLocalFileTime(&ft, &local) && FileTimeToSystemTime(&local, &st)) {
        _snprintf_s(out, cap, _TRUNCATE, "%02u:%02u:%02u.%03u",
                    st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);
    } else {
        _snprintf_s(out, cap, _TRUNCATE, "--:--:--.---");
    }
}

static void
PrintEvent(const AEGIS_EVENT_HEADER *evt)
{
    char ts[16];
    FormatTimestamp(evt->Timestamp, ts, sizeof(ts));

    switch (evt->Type) {
    case AegisEvtProcessCreate: {
        const AEGIS_PROCESS_EVENT *p = (const AEGIS_PROCESS_EVENT *)(evt + 1);
        if (evt->Size < sizeof(*evt) + sizeof(*p) ||
            p->ImagePathLength >= AEGIS_MAX_PATH) {
            fprintf(stderr, "[%s] malformed process-create event (%u bytes)\n",
                    ts, evt->Size);
            break;
        }
        printf("[%s] #%-5lu CREATE  pid=%-6lu ppid=%-6lu creator=%-6lu %.*ls%s\n",
               ts, evt->Sequence, p->ProcessId, p->ParentProcessId,
               p->CreatingProcessId, (int)p->ImagePathLength, p->ImagePath,
               p->ImagePathExact ? "" : " [partial-name]");
        break;
    }
    case AegisEvtProcessExit: {
        const AEGIS_PROCESS_EVENT *p = (const AEGIS_PROCESS_EVENT *)(evt + 1);
        if (evt->Size < sizeof(*evt) + sizeof(*p)) {
            fprintf(stderr, "[%s] malformed process-exit event (%u bytes)\n",
                    ts, evt->Size);
            break;
        }
        printf("[%s] #%-5lu EXIT    pid=%-6lu\n", ts, evt->Sequence, p->ProcessId);
        break;
    }
    default:
        printf("[%s] #%-5lu type=%u (%u bytes)\n", ts, evt->Sequence, evt->Type, evt->Size);
        break;
    }
}

int main(void)
{
    HANDLE h = CreateFileA(AEGIS_USERMODE_PATH, GENERIC_READ, 0, NULL,
                           OPEN_EXISTING, 0, NULL);
    if (h == INVALID_HANDLE_VALUE) {
        fprintf(stderr, "Cannot open %s (error %lu). Is AegisMon loaded, "
                        "and are you running elevated?\n",
                AEGIS_USERMODE_PATH, GetLastError());
        return 1;
    }

    unsigned char *buf = (unsigned char *)malloc(READ_BUFFER);
    if (buf == NULL) {
        CloseHandle(h);
        return 1;
    }

    printf("AegisAgent: streaming process events. Ctrl+C to stop.\n");

    for (;;) {
        DWORD returned = 0;
        BOOL ok = DeviceIoControl(h, IOCTL_AEGIS_GET_EVENTS, NULL, 0,
                                  buf, READ_BUFFER, &returned, NULL);
        if (!ok) {
            fprintf(stderr, "DeviceIoControl failed (error %lu)\n", GetLastError());
            break;
        }

        /* Walk the packed batch. Each event starts with a header whose Size
         * spans header + payload, so Size is also the stride to the next one. */
        DWORD off = 0;
        while (off + sizeof(AEGIS_EVENT_HEADER) <= returned) {
            const AEGIS_EVENT_HEADER *evt = (const AEGIS_EVENT_HEADER *)(buf + off);
            if (evt->Size < sizeof(AEGIS_EVENT_HEADER) || off + evt->Size > returned) {
                break;   /* malformed / truncated - stop walking this batch */
            }
            PrintEvent(evt);
            off += evt->Size;
        }

        if (returned == 0) {
            Sleep(IDLE_SLEEP_MS);   /* idle: nothing queued */
        }
    }

    free(buf);
    CloseHandle(h);
    return 0;
}
