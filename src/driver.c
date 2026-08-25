/*
*   driver.c - windows kernel driver entry point for the hypervisor
*   creates a device object, symbolic link, and initializes vmx
*   provides ioctl interface for usermode loader communication
*
*   production builds create no device object, no symbolic link, and no
*   dispatch table: nothing for module/device enumeration to fingerprint.
*/
#include "hv.h"

#if !OPHION_PRODUCTION

#define DEVICE_NAME     L"\\Device\\Ophion"
#define SYMLINK_NAME    L"\\DosDevices\\Ophion"


static NTSTATUS DriverCreateClose(PDEVICE_OBJECT device_obj, PIRP irp);
static NTSTATUS DriverIoControl(PDEVICE_OBJECT device_obj, PIRP irp);

VOID
DriverUnload(_In_ PDRIVER_OBJECT driver_obj)
{
    UNICODE_STRING symlink;

    HV_LOG(0, 0, "[hv] Unloading hypervisor driver...\n");

    broadcast_terminate_all();
    vmx_terminate();

    RtlInitUnicodeString(&symlink, SYMLINK_NAME);
    IoDeleteSymbolicLink(&symlink);

    if (driver_obj->DeviceObject)
    {
        IoDeleteDevice(driver_obj->DeviceObject);
    }

    HV_LOG(0, 0, "[hv] Driver unloaded.\n");
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

    UNREFERENCED_PARAMETER(registry_path);

    HV_LOG(0, 0, "[hv] Ophion initializing...\n");

    RtlInitUnicodeString(&device_name, DEVICE_NAME);
    status = IoCreateDevice(
        driver_obj,
        0,
        &device_name,
        FILE_DEVICE_UNKNOWN,
        FILE_DEVICE_SECURE_OPEN,
        FALSE,
        &device_obj);

    if (!NT_SUCCESS(status))
    {
        HV_LOG(0, 0, "[hv] IoCreateDevice failed: 0x%X\n", status);
        return status;
    }

    RtlInitUnicodeString(&symlink, SYMLINK_NAME);
    status = IoCreateSymbolicLink(&symlink, &device_name);

    if (!NT_SUCCESS(status))
    {
        HV_LOG(0, 0, "[hv] IoCreateSymbolicLink failed: 0x%X\n", status);
        IoDeleteDevice(device_obj);
        return status;
    }

    driver_obj->DriverUnload                         = DriverUnload;
    driver_obj->MajorFunction[IRP_MJ_CREATE]         = DriverCreateClose;
    driver_obj->MajorFunction[IRP_MJ_CLOSE]          = DriverCreateClose;
    driver_obj->MajorFunction[IRP_MJ_DEVICE_CONTROL] = DriverIoControl;

    if (!vmx_init())
    {
        HV_LOG(0, 0, "[hv] VMX initialization FAILED!\n");
        vmx_terminate();  // clean up any partially-allocated resources
        IoDeleteSymbolicLink(&symlink);
        IoDeleteDevice(device_obj);
        return STATUS_HV_OPERATION_FAILED;
    }

    HV_LOG(0, 0, "[hv] Hypervisor loaded and active on all cores!\n");
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

static VOID
DriverFillStatusV1(HV_STATUS_V1 * status)
{
    RtlZeroMemory(status, sizeof(*status));
    status->Size = sizeof(*status);
    status->Version = HV_STATUS_VERSION_1;
    status->HeaderSize = FIELD_OFFSET(HV_STATUS_V1, Vcpu);
    status->TotalProcessors = g_cpu_count;
    status->ParentFlags = g_hv_capabilities.ParentFlags;
    status->ParentFeatures = g_hv_capabilities.ParentFeatures;
    status->CapabilityFlags = g_hv_capabilities.CapabilityFlags;
    status->PreflightFailure = g_hv_capabilities.Failure;
    status->PhysicalAddressBits = g_hv_capabilities.PhysicalAddressBits;
    status->MaximumGuestPhysicalAddress =
        g_hv_capabilities.MaximumGuestPhysicalAddress;
    RtlCopyMemory(status->ParentVendor, g_hv_capabilities.ParentVendor,
                  sizeof(status->ParentVendor));

    for (UINT32 i = 0; i < g_cpu_count && i < HV_STATUS_MAX_VCPUS; i++)
    {
        VIRTUAL_MACHINE_STATE * vcpu = &g_vcpu[i];
        HV_STATUS_VCPU_V1 * out = &status->Vcpu[i];

        out->Size = sizeof(*out);
        out->Index = i;
        out->Group = g_processor_topology[i].Processor.Group;
        out->Number = g_processor_topology[i].Processor.Number;
        out->LastExitReason = vcpu->exit_reason;
        out->LastExitQualification = vcpu->exit_qual;
        out->LastFailure = vcpu->last_failure;
        out->LastVmInstructionError = vcpu->last_vm_instruction_error;
        out->TotalExits = vcpu->total_exits;
        out->CpuidExits = vcpu->exit_counters[VMX_EXIT_REASON_EXECUTE_CPUID];
        out->EptViolationExits = vcpu->exit_counters[VMX_EXIT_REASON_EPT_VIOLATION];
        out->MonitorTrapExits = vcpu->exit_counters[VMX_EXIT_REASON_MONITOR_TRAP_FLAG];
        out->RdtscExits = vcpu->exit_counters[VMX_EXIT_REASON_EXECUTE_RDTSC];
        out->RdtscpExits = vcpu->exit_counters[VMX_EXIT_REASON_EXECUTE_RDTSCP];
        out->VmcallExits = vcpu->exit_counters[VMX_EXIT_REASON_EXECUTE_VMCALL];
        out->MsrReadExits = vcpu->exit_counters[VMX_EXIT_REASON_EXECUTE_RDMSR];
        out->MsrWriteExits = vcpu->exit_counters[VMX_EXIT_REASON_EXECUTE_WRMSR];

        if (vcpu->launched) {
            out->StateFlags |= HV_VCPU_LAUNCHED;
            status->LaunchedProcessors++;
        }
        if (vcpu->detached) {
            out->StateFlags |= HV_VCPU_DETACHED;
            status->DetachedProcessors++;
        }
        if (vcpu->failed) {
            out->StateFlags |= HV_VCPU_FAILED;
            status->FailedProcessors++;
        }
        if (vcpu->terminal) {
            out->StateFlags |= HV_VCPU_TERMINAL;
            status->TerminalProcessors++;
        }
        if (vcpu->last_failure)
            status->LastFailure = vcpu->last_failure;
        if (vcpu->last_vm_instruction_error)
            status->LastVmInstructionError = vcpu->last_vm_instruction_error;

        for (UINT32 reason = 0;
             reason < HV_STATUS_EXIT_REASON_COUNT;
             reason++)
            status->AggregateExitCounters[reason] +=
                vcpu->exit_counters[reason];
    }
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
        ULONG output_length =
            io_stack->Parameters.DeviceIoControl.OutputBufferLength;

        if (output_length >= sizeof(HV_STATUS_V1))
        {
            DriverFillStatusV1(
                (HV_STATUS_V1 *)irp->AssociatedIrp.SystemBuffer);
            irp->IoStatus.Information = sizeof(HV_STATUS_V1);
        }
        else if (output_length >= sizeof(UINT32))
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
        // add more shi here later
        //
        status = STATUS_INVALID_DEVICE_REQUEST;
        break;
    }

    irp->IoStatus.Status = status;
    IoCompleteRequest(irp, IO_NO_INCREMENT);
    return status;
}

#else // OPHION_PRODUCTION

//
// production entry: virtualize and leave. no device object, no symlink, no
// dispatch table — module/device/service enumerators see nothing to query.
// teardown for a mapped image is driven by VMCALL_VMXOFF per core.
//
NTSTATUS
DriverEntry(
    _In_ PDRIVER_OBJECT  driver_obj,
    _In_ PUNICODE_STRING registry_path)
{
    UNREFERENCED_PARAMETER(driver_obj);
    UNREFERENCED_PARAMETER(registry_path);

    if (!vmx_init())
    {
        vmx_terminate();
        return STATUS_HV_OPERATION_FAILED;
    }

    return STATUS_SUCCESS;
}

VOID
DriverUnload(_In_ PDRIVER_OBJECT driver_obj)
{
    UNREFERENCED_PARAMETER(driver_obj);

    broadcast_terminate_all();
    vmx_terminate();
}

#endif // OPHION_PRODUCTION
