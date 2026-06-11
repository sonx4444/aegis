/*
 * ProcessMon.c - process create/exit monitor.
 *
 * Translates PsSetCreateProcessNotifyRoutineEx callbacks into AEGIS_PROCESS_EVENT
 * payloads and publishes them to the core event queue.
 */
#include <ntddk.h>
#include "AegisDriverProtocol.h"
#include "EventQueue.h"
#include "Modules.h"

static BOOLEAN g_Registered = FALSE;

static void
ProcessNotify(_Inout_ PEPROCESS Process, _In_ HANDLE ProcessId,
              _Inout_opt_ PPS_CREATE_NOTIFY_INFO CreateInfo)
{
    AEGIS_PROCESS_EVENT ev;

    UNREFERENCED_PARAMETER(Process);
    RtlZeroMemory(&ev, sizeof(ev));
    ev.ProcessId = (ULONG)(ULONG_PTR)ProcessId;

    if (CreateInfo == NULL) {
        /* Process exit: only the PID is meaningful. */
        AegisPublish(AegisEvtProcessExit, &ev, sizeof(ev));
        return;
    }

    ev.ParentProcessId   = (ULONG)(ULONG_PTR)CreateInfo->ParentProcessId;
    /* An explicit parent-process attribute can change ParentProcessId, but not
     * the process that owns the thread performing this creation operation. */
    ev.CreatingProcessId = (ULONG)(ULONG_PTR)CreateInfo->CreatingThreadId.UniqueProcess;

    if (CreateInfo->ImageFileName != NULL && CreateInfo->ImageFileName->Buffer != NULL) {
        USHORT chars = CreateInfo->ImageFileName->Length / sizeof(WCHAR);
        ev.ImagePathExact = CreateInfo->FileOpenNameAvailable ? 1 : 0;
        if (chars > AEGIS_MAX_PATH - 1) {
            chars = AEGIS_MAX_PATH - 1;
            ev.ImagePathExact = 0;
        }
        RtlCopyMemory(ev.ImagePath, CreateInfo->ImageFileName->Buffer, chars * sizeof(WCHAR));
        ev.ImagePath[chars] = L'\0';
        ev.ImagePathLength = chars;
    }

    DbgPrint("[AegisMon] create pid=%lu ppid=%lu img=%ws\n",
             ev.ProcessId, ev.ParentProcessId, ev.ImagePath);

    AegisPublish(AegisEvtProcessCreate, &ev, sizeof(ev));
}

NTSTATUS
ProcessMonStart(void)
{
    NTSTATUS status = PsSetCreateProcessNotifyRoutineEx(ProcessNotify, FALSE);
    if (NT_SUCCESS(status)) {
        g_Registered = TRUE;
    } else {
        /* Usually STATUS_ACCESS_DENIED if the image wasn't linked /INTEGRITYCHECK. */
        DbgPrint("[AegisMon] ProcessMon register failed 0x%08X\n", status);
    }
    return status;
}

void
ProcessMonStop(void)
{
    if (g_Registered) {
        PsSetCreateProcessNotifyRoutineEx(ProcessNotify, TRUE);
        g_Registered = FALSE;
    }
}
