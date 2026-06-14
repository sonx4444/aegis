/*
 * ThreadMon.c - thread create/exit monitor.
 *
 * Turns PsSetCreateThreadNotifyRoutine callbacks into AEGIS_THREAD_EVENT
 * payloads. The signal worth flagging is a thread created by a different
 * process than the one it runs in - the shape of CreateRemoteThread injection.
 */
#include <ntddk.h>
#include "AegisDriverProtocol.h"
#include "EventQueue.h"
#include "Modules.h"

static BOOLEAN g_Registered = FALSE;

static void
ThreadNotify(_In_ HANDLE ProcessId, _In_ HANDLE ThreadId, _In_ BOOLEAN Create)
{
    AEGIS_THREAD_EVENT ev;

    RtlZeroMemory(&ev, sizeof(ev));
    ev.ProcessId = (ULONG)(ULONG_PTR)ProcessId;
    ev.ThreadId  = (ULONG)(ULONG_PTR)ThreadId;

    if (!Create) {
        /* Thread teardown: only the identity is meaningful. */
        AegisPublish(AegisEvtThreadExit, &ev, sizeof(ev));
        return;
    }

    /* On creation this routine runs in the context of the thread doing the
     * creating, so the current process is the creator. If that isn't the
     * process the new thread will run in, something reached across a process
     * boundary to start it - the CreateRemoteThread shape. It also happens
     * benignly: a parent starts its child's first thread the same way. So the
     * flag is a lead to correlate, not a verdict. */
    ev.CreatingProcessId = (ULONG)(ULONG_PTR)PsGetCurrentProcessId();
    ev.Remote = (ev.CreatingProcessId != ev.ProcessId) ? 1 : 0;

    AegisPublish(AegisEvtThreadCreate, &ev, sizeof(ev));
}

NTSTATUS
ThreadMonStart(void)
{
    NTSTATUS status = PsSetCreateThreadNotifyRoutine(ThreadNotify);
    if (NT_SUCCESS(status)) {
        g_Registered = TRUE;
    } else {
        DbgPrint("[AegisMon] ThreadMon register failed 0x%08X\n", status);
    }
    return status;
}

void
ThreadMonStop(void)
{
    if (g_Registered) {
        PsRemoveCreateThreadNotifyRoutine(ThreadNotify);
        g_Registered = FALSE;
    }
}
