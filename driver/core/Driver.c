/*
 * Driver.c - AegisMon.sys entry point.
 *
 * Creates the device + symlink, wires the IRP dispatch table, and answers the
 * IOCTL with a fixed greeting. No telemetry is captured yet; this is just the
 * kernel<->user channel the rest of the driver will be built on.
 */
#include <ntddk.h>
#include <wdmsec.h>
#include "AegisDriverProtocol.h"

static PDEVICE_OBJECT g_DeviceObject = NULL;

static UNICODE_STRING g_DeviceName = RTL_CONSTANT_STRING(AEGIS_DEVICE_NAME);
static UNICODE_STRING g_SymLink    = RTL_CONSTANT_STRING(AEGIS_SYMLINK_NAME);

/* Private setup-class GUID used only to associate the device with its security
 * policy. Do not reuse a system-defined device-class GUID here. */
static const GUID g_DeviceClassGuid = {
    0x4b4cc1a7, 0x6d5d, 0x4e62, { 0x9c, 0x3f, 0x42, 0xc7, 0xc3, 0xa8, 0x25, 0x6e }
};

/* Placeholder payload the IOCTL hands back until real events flow through it.
 * sizeof() includes the trailing NUL, which we deliberately copy across. */
static const char g_Greeting[] = "hello from kernel";

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
     * state, so the correct minimum is to complete the IRP successfully -
     * otherwise the user's CreateFile()/CloseHandle() would fail or hang. */
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

    /* The IO_STACK_LOCATION is our per-driver slot in the IRP; it carries which
     * control code was sent and the caller's buffer sizes. */
    if (stack->Parameters.DeviceIoControl.IoControlCode == IOCTL_AEGIS_GET_EVENTS) {
        ULONG outLen = stack->Parameters.DeviceIoControl.OutputBufferLength;
        /* METHOD_BUFFERED: the I/O manager gave us one SystemBuffer for both
         * directions and copies it back to the caller - up to Information bytes. */
        if (outLen >= sizeof(g_Greeting)) {
            RtlCopyMemory(Irp->AssociatedIrp.SystemBuffer, g_Greeting, sizeof(g_Greeting));
            info = sizeof(g_Greeting);
            status = STATUS_SUCCESS;
        } else {
            status = STATUS_BUFFER_TOO_SMALL;
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

    /* Tear down in reverse order of creation. */
    IoDeleteSymbolicLink(&g_SymLink);
    if (g_DeviceObject != NULL) {
        IoDeleteDevice(g_DeviceObject);
        g_DeviceObject = NULL;
    }
    DbgPrint("[AegisMon] unloaded\n");
}

_Use_decl_annotations_
NTSTATUS
DriverEntry(PDRIVER_OBJECT DriverObject, PUNICODE_STRING RegistryPath)
{
    NTSTATUS status;

    UNREFERENCED_PARAMETER(RegistryPath);

    status = IoCreateDeviceSecure(DriverObject, 0, &g_DeviceName,
                                  FILE_DEVICE_UNKNOWN, FILE_DEVICE_SECURE_OPEN,
                                  FALSE, &SDDL_DEVOBJ_SYS_ALL_ADM_ALL,
                                  &g_DeviceClassGuid,
                                  &g_DeviceObject);
    if (!NT_SUCCESS(status)) {
        DbgPrint("[AegisMon] IoCreateDeviceSecure failed 0x%08X\n", status);
        return status;
    }

    status = IoCreateSymbolicLink(&g_SymLink, &g_DeviceName);
    if (!NT_SUCCESS(status)) {
        /* Unwind what we built so a failed load leaves nothing behind. */
        DbgPrint("[AegisMon] IoCreateSymbolicLink failed 0x%08X\n", status);
        IoDeleteDevice(g_DeviceObject);
        g_DeviceObject = NULL;
        return status;
    }

    /* The dispatch table is just function pointers indexed by IRP major code:
     * when an IRP of a given type arrives, the I/O manager calls our handler. */
    DriverObject->MajorFunction[IRP_MJ_CREATE]         = AegisCreateClose;
    DriverObject->MajorFunction[IRP_MJ_CLOSE]          = AegisCreateClose;
    DriverObject->MajorFunction[IRP_MJ_DEVICE_CONTROL] = AegisDeviceControl;
    DriverObject->DriverUnload                         = AegisUnload;

    DbgPrint("[AegisMon] loaded; channel ready\n");
    return STATUS_SUCCESS;
}
