/*
 * Driver.c - AegisMon.sys entry point and orchestration.
 *
 * Owns the device/symlink and IOCTL dispatch, initializes the event queue, and
 * starts/stops the monitor modules. Telemetry capture lives in driver/modules/*.
 */
#include <ntddk.h>
#include <wdmsec.h>
#include "AegisDriverProtocol.h"
#include "EventQueue.h"
#include "Modules.h"

static PDEVICE_OBJECT g_DeviceObject = NULL;

static UNICODE_STRING g_DeviceName = RTL_CONSTANT_STRING(AEGIS_DEVICE_NAME);
static UNICODE_STRING g_SymLink    = RTL_CONSTANT_STRING(AEGIS_SYMLINK_NAME);

/* Private setup-class GUID used only to associate the device with its security
 * policy. Do not reuse a system-defined device-class GUID here. */
static const GUID g_DeviceClassGuid = {
    0x4b4cc1a7, 0x6d5d, 0x4e62, { 0x9c, 0x3f, 0x42, 0xc7, 0xc3, 0xa8, 0x25, 0x6e }
};

DRIVER_INITIALIZE DriverEntry;
static DRIVER_UNLOAD AegisUnload;
static DRIVER_DISPATCH AegisCreateClose;
static DRIVER_DISPATCH AegisDeviceControl;

_Use_decl_annotations_
static NTSTATUS
AegisCreateClose(PDEVICE_OBJECT DeviceObject, PIRP Irp)
{
    UNREFERENCED_PARAMETER(DeviceObject);
    /* One handler for both IRP_MJ_CREATE and IRP_MJ_CLOSE. We keep no per-handle
     * state, so the correct minimum is to complete the IRP successfully. */
    Irp->IoStatus.Status = STATUS_SUCCESS;
    Irp->IoStatus.Information = 0;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
    return STATUS_SUCCESS;
}

_Use_decl_annotations_
static NTSTATUS
AegisDeviceControl(PDEVICE_OBJECT DeviceObject, PIRP Irp)
{
    PIO_STACK_LOCATION stack = IoGetCurrentIrpStackLocation(Irp);
    NTSTATUS status = STATUS_INVALID_DEVICE_REQUEST;
    ULONG_PTR info = 0;

    UNREFERENCED_PARAMETER(DeviceObject);

    if (stack->Parameters.DeviceIoControl.IoControlCode == IOCTL_AEGIS_GET_EVENTS) {
        /* METHOD_BUFFERED: SystemBuffer is the one buffer the I/O manager copies
         * back to the caller, up to Information bytes. Drain whatever fits. */
        ULONG outLen = stack->Parameters.DeviceIoControl.OutputBufferLength;
        if (outLen < AEGIS_MAX_EVENT_SIZE) {
            status = STATUS_BUFFER_TOO_SMALL;
        } else {
            info = AegisQueuePull(Irp->AssociatedIrp.SystemBuffer, outLen);
            status = STATUS_SUCCESS;
        }
    }

    Irp->IoStatus.Status = status;
    Irp->IoStatus.Information = info;   /* byte count copied back to user mode */
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
    return status;
}

_Use_decl_annotations_
static VOID
AegisUnload(PDRIVER_OBJECT DriverObject)
{
    UNREFERENCED_PARAMETER(DriverObject);

    /* Stop producing first, then tear down the channel, then free the queue -
     * reverse order of DriverEntry, so nothing is left publishing into freed state. */
    ImageMonStop();
    ThreadMonStop();
    ProcessMonStop();

    IoDeleteSymbolicLink(&g_SymLink);
    if (g_DeviceObject != NULL) {
        IoDeleteDevice(g_DeviceObject);
        g_DeviceObject = NULL;
    }

    AegisQueueDrain();
    DbgPrint("[AegisMon] unloaded\n");
}

_Use_decl_annotations_
NTSTATUS
DriverEntry(PDRIVER_OBJECT DriverObject, PUNICODE_STRING RegistryPath)
{
    NTSTATUS status;

    UNREFERENCED_PARAMETER(RegistryPath);

    AegisQueueInit();

    status = IoCreateDeviceSecure(DriverObject, 0, &g_DeviceName,
                                  FILE_DEVICE_UNKNOWN, FILE_DEVICE_SECURE_OPEN,
                                  FALSE, &SDDL_DEVOBJ_SYS_ALL_ADM_ALL,
                                  &g_DeviceClassGuid,
                                  &g_DeviceObject);
    if (!NT_SUCCESS(status)) {
        DbgPrint("[AegisMon] IoCreateDeviceSecure failed 0x%08X\n", status);
        AegisQueueDrain();
        return status;
    }

    status = IoCreateSymbolicLink(&g_SymLink, &g_DeviceName);
    if (!NT_SUCCESS(status)) {
        /* Unwind what we built so a failed load leaves nothing behind. */
        DbgPrint("[AegisMon] IoCreateSymbolicLink failed 0x%08X\n", status);
        IoDeleteDevice(g_DeviceObject);
        g_DeviceObject = NULL;
        AegisQueueDrain();
        return status;
    }

    /* The dispatch table is just function pointers indexed by IRP major code. */
    DriverObject->MajorFunction[IRP_MJ_CREATE]         = AegisCreateClose;
    DriverObject->MajorFunction[IRP_MJ_CLOSE]          = AegisCreateClose;
    DriverObject->MajorFunction[IRP_MJ_DEVICE_CONTROL] = AegisDeviceControl;
    DriverObject->DriverUnload                         = AegisUnload;

    /* Start monitor modules. Each new source is one Start call here and its
     * matching Stop in AegisUnload; the queue and IOCTL underneath never change. */
    status = ProcessMonStart();
    if (NT_SUCCESS(status)) { status = ThreadMonStart(); }
    if (NT_SUCCESS(status)) { status = ImageMonStart(); }
    if (!NT_SUCCESS(status)) {
        /* Each Stop is a no-op unless that module registered, so unwinding all
         * three is safe regardless of which one failed. */
        ImageMonStop();
        ThreadMonStop();
        ProcessMonStop();
        IoDeleteSymbolicLink(&g_SymLink);
        IoDeleteDevice(g_DeviceObject);
        g_DeviceObject = NULL;
        AegisQueueDrain();
        return status;
    }

    DbgPrint("[AegisMon] loaded; monitoring process, thread, and image-load activity\n");
    return STATUS_SUCCESS;
}
