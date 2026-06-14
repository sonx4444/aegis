/*
 * ImageMon.c - image (DLL / driver) load monitor.
 *
 * Turns PsSetLoadImageNotifyRoutine callbacks into AEGIS_IMAGE_EVENT payloads:
 * which image mapped into which process, where in memory, and whether it loaded
 * into the kernel. This is the raw material for later "what loaded where"
 * detection - unsigned DLLs, loads from odd paths, unexpected drivers.
 */
#include <ntddk.h>
#include "AegisDriverProtocol.h"
#include "EventQueue.h"
#include "Modules.h"

static BOOLEAN g_Registered = FALSE;

static void
ImageNotify(_In_opt_ PUNICODE_STRING FullImageName, _In_ HANDLE ProcessId,
            _In_ PIMAGE_INFO ImageInfo)
{
    AEGIS_IMAGE_EVENT ev;

    RtlZeroMemory(&ev, sizeof(ev));
    /* ProcessId is zero when the image is a kernel-mode driver rather than a
     * user-mode module mapped into a process. */
    ev.ProcessId       = (ULONG)(ULONG_PTR)ProcessId;
    ev.SystemModeImage = ImageInfo->SystemModeImage ? 1 : 0;
    ev.ImageBase       = (unsigned __int64)(ULONG_PTR)ImageInfo->ImageBase;
    ev.ImageSize       = (unsigned __int64)ImageInfo->ImageSize;

    /* FullImageName is optional and not guaranteed NUL-terminated; copy by
     * Length and terminate ourselves, truncating overlong paths. */
    if (FullImageName != NULL && FullImageName->Buffer != NULL) {
        USHORT chars = FullImageName->Length / sizeof(WCHAR);
        ev.ImagePathExact = 1;
        if (chars > AEGIS_MAX_PATH - 1) {
            chars = AEGIS_MAX_PATH - 1;
            ev.ImagePathExact = 0;
        }
        RtlCopyMemory(ev.ImagePath, FullImageName->Buffer, chars * sizeof(WCHAR));
        ev.ImagePath[chars] = L'\0';
        ev.ImagePathLength = chars;
    }

    AegisPublish(AegisEvtImageLoad, &ev, sizeof(ev));
}

NTSTATUS
ImageMonStart(void)
{
    NTSTATUS status = PsSetLoadImageNotifyRoutine(ImageNotify);
    if (NT_SUCCESS(status)) {
        g_Registered = TRUE;
    } else {
        DbgPrint("[AegisMon] ImageMon register failed 0x%08X\n", status);
    }
    return status;
}

void
ImageMonStop(void)
{
    if (g_Registered) {
        PsRemoveLoadImageNotifyRoutine(ImageNotify);
        g_Registered = FALSE;
    }
}
