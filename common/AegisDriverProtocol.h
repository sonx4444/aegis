/*
 * AegisDriverProtocol.h - the contract between AegisMon (driver) and AegisAgent (app).
 *
 * Events are a tagged, variable-length stream: a fixed AEGIS_EVENT_HEADER
 * followed by a type-specific payload. The driver packs whole events back to
 * back; the agent walks them using header->Size and dispatches on header->Type.
 * Adding a new event source = a new Type + payload struct here; the transport
 * (queue + IOCTL) never changes.
 *
 * Safe to include from kernel mode (after <ntddk.h>) and user mode
 * (after <windows.h> + <winioctl.h>): both supply CTL_CODE, LARGE_INTEGER,
 * and the integer/wchar_t types used below.
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

/* Pull queued events (a packed stream) from the driver.
 * Device type FILE_DEVICE_UNKNOWN (not a literal 0x8000, which would shift into
 * the sign bit and make the control code a negative int). 0x800-0xFFF is the
 * vendor-defined function range. */
#define IOCTL_AEGIS_GET_EVENTS \
    CTL_CODE(FILE_DEVICE_UNKNOWN, 0x800, METHOD_BUFFERED, FILE_READ_DATA)

/* Max characters (WCHARs) of an image/file path we record, including NUL room. */
#define AEGIS_MAX_PATH 512

typedef enum _AEGIS_EVENT_TYPE {
    AegisEvtProcessCreate = 1,
    AegisEvtProcessExit   = 2,
    AegisEvtThreadCreate  = 3,   /* reserved - future module */
    AegisEvtImageLoad     = 4,   /* reserved - future module */
    AegisEvtFileOp        = 5,   /* reserved - minifilter     */
    AegisEvtNetConn       = 6,   /* reserved - WFP callout     */
} AEGIS_EVENT_TYPE;

#pragma pack(push, 8)

/* Prefix on every event in the stream. */
typedef struct _AEGIS_EVENT_HEADER {
    unsigned short Size;        /* total bytes: this header + payload      */
    unsigned short Type;        /* one of AEGIS_EVENT_TYPE                  */
    unsigned long  Sequence;    /* enqueue order; gaps can reveal eviction  */
    LARGE_INTEGER  Timestamp;   /* system time, 100ns units since 1601     */
} AEGIS_EVENT_HEADER, *PAEGIS_EVENT_HEADER;

/* Payload for AegisEvtProcessCreate (full) and AegisEvtProcessExit (ProcessId only). */
typedef struct _AEGIS_PROCESS_EVENT {
    unsigned long  ProcessId;
    unsigned long  ParentProcessId;
    unsigned long  CreatingProcessId; /* process owning the creating thread  */
    unsigned short ImagePathLength;   /* WCHAR count in ImagePath, no NUL     */
    unsigned char  ImagePathExact;    /* exact source name and not truncated   */
    unsigned char  Reserved;
    wchar_t        ImagePath[AEGIS_MAX_PATH];
} AEGIS_PROCESS_EVENT, *PAEGIS_PROCESS_EVENT;

#pragma pack(pop)

/* Largest event accepted by the current transport. Callers should provide at
 * least this much output space so the head event can always be consumed. */
#define AEGIS_MAX_EVENT_SIZE \
    (sizeof(AEGIS_EVENT_HEADER) + sizeof(AEGIS_PROCESS_EVENT) + 64u)
