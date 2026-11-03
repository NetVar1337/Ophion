/*
*   driver.c - windows kernel driver entry point for the hypervisor
*   creates a device object, symbolic link, and initializes vmx
*   provides ioctl interface for usermode loader communication
*/
#include "hv.h"

#define IOCTL_BASE      0x800
//
// status query only reads from the device, so require read access
//
#define IOCTL_HV_STATUS CTL_CODE(FILE_DEVICE_UNKNOWN, IOCTL_BASE + 0, METHOD_BUFFERED, FILE_READ_ACCESS)

static NTSTATUS DriverCreateClose(PDEVICE_OBJECT device_obj, PIRP irp);
static NTSTATUS DriverIoControl(PDEVICE_OBJECT device_obj, PIRP irp);

VOID
DriverUnload(_In_ PDRIVER_OBJECT driver_obj)
{
    UNICODE_STRING symlink;

    HV_LOG("[hv] Unloading hypervisor driver...\n");

    //
    // restore the hidden device object + symlink first, so the
    // IoDeleteSymbolicLink/IoDeleteDevice below work normally
    //
    dkom_restore();

    broadcast_terminate_all();
    vmx_terminate();

    RtlInitUnicodeString(&symlink, SYMLINK_NAME);
    IoDeleteSymbolicLink(&symlink);

    if (driver_obj->DeviceObject)
    {
        IoDeleteDevice(driver_obj->DeviceObject);
    }

    dkom_cleanup();

    HV_LOG("[hv] Driver unloaded.\n");
}

NTSTATUS
DriverEntry(
    _In_ PDRIVER_OBJECT  driver_obj,
    _In_ PUNICODE_STRING registry_path)
{
    NTSTATUS       status;
    PDEVICE_OBJECT device_obj = NULL;
    UNICODE_STRING device_name;
    UNICODE_STRING symlink;
    UNICODE_STRING sddl;

    UNREFERENCED_PARAMETER(registry_path);

    HV_LOG("[hv] Ophion initializing...\n");

    //
    // restrict the device to SYSTEM and Administrators - the ioctl
    // interface is for the loader service only, not arbitrary processes
    //
    RtlInitUnicodeString(&device_name, DEVICE_NAME);
    RtlInitUnicodeString(&sddl, SDDL_DEVOBJ_SYS_ALL_ADM_ALL);
    status = IoCreateDeviceSecure(
        driver_obj,
        0,
        &device_name,
        FILE_DEVICE_UNKNOWN,
        FILE_DEVICE_SECURE_OPEN,
        FALSE,
        &sddl,
        NULL,
        &device_obj);

    if (!NT_SUCCESS(status))
    {
        HV_LOG("[hv] IoCreateDevice failed: 0x%X\n", status);
        return status;
    }

    RtlInitUnicodeString(&symlink, SYMLINK_NAME);
    status = IoCreateSymbolicLink(&symlink, &device_name);

    if (!NT_SUCCESS(status))
    {
        HV_LOG("[hv] IoCreateSymbolicLink failed: 0x%X\n", status);
        IoDeleteDevice(device_obj);
        return status;
    }

    //
    // allocate the relocated globals before vmx_init(): stealth_init and
    // hostcr3_build must already see this allocation
    //
    if (!dkom_alloc_globals())
    {
        HV_LOG("[hv] Failed to allocate relocated globals\n");
        IoDeleteSymbolicLink(&symlink);
        IoDeleteDevice(device_obj);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    driver_obj->DriverUnload                         = DriverUnload;
    driver_obj->MajorFunction[IRP_MJ_CREATE]         = DriverCreateClose;
    driver_obj->MajorFunction[IRP_MJ_CLOSE]          = DriverCreateClose;
    driver_obj->MajorFunction[IRP_MJ_DEVICE_CONTROL] = DriverIoControl;

    if (!vmx_init())
    {
        HV_LOG("[hv] VMX initialization FAILED!\n");
        vmx_terminate();  // clean up any partially-allocated resources
        IoDeleteSymbolicLink(&symlink);
        IoDeleteDevice(device_obj);
        dkom_cleanup();
        return STATUS_HV_OPERATION_FAILED;
    }

    //
    // PG-safe DKOM: hide device object + symlink, wipe on-disk image
    // (runs in guest context, so it must not touch the EPT-hidden globals)
    //
    dkom_hide(driver_obj);

    HV_LOG("[hv] Hypervisor loaded and active on all cores!\n");
    return STATUS_SUCCESS;
}

static NTSTATUS
DriverCreateClose(
    _In_ PDEVICE_OBJECT device_obj,
    _In_ PIRP           irp)
{
    UNREFERENCED_PARAMETER(device_obj);

    irp->IoStatus.Status      = STATUS_SUCCESS;
    irp->IoStatus.Information = 0;
    IoCompleteRequest(irp, IO_NO_INCREMENT);
    return STATUS_SUCCESS;
}

static NTSTATUS
DriverIoControl(
    _In_ PDEVICE_OBJECT device_obj,
    _In_ PIRP           irp)
{
    NTSTATUS           status = STATUS_SUCCESS;
    PIO_STACK_LOCATION io_stack;
    ULONG              ioctl_code;

    UNREFERENCED_PARAMETER(device_obj);

    io_stack       = IoGetCurrentIrpStackLocation(irp);
    ioctl_code = io_stack->Parameters.DeviceIoControl.IoControlCode;

    switch (ioctl_code)
    {
    case IOCTL_HV_STATUS:
    {
        //
        // return basic status: number of virtualized cores
        //
        if (io_stack->Parameters.DeviceIoControl.OutputBufferLength >= sizeof(UINT32) &&
            irp->AssociatedIrp.SystemBuffer != NULL)
        {
            *(UINT32 *)irp->AssociatedIrp.SystemBuffer = g_cpu_count;
            irp->IoStatus.Information = sizeof(UINT32);
        }
        else
        {
            status = STATUS_BUFFER_TOO_SMALL;
        }
        break;
    }

    default:
        //
        // unknown ioctl
        //
        status = STATUS_INVALID_DEVICE_REQUEST;
        break;
    }

    irp->IoStatus.Status = status;
    IoCompleteRequest(irp, IO_NO_INCREMENT);
    return status;
}
