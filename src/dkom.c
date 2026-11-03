/*
*   dkom.c - PG-safe driver hiding
*   - unlink device object from DriverObject->DeviceObject chain
*   - delete the \DosDevices symlink
*   - zero the on-disk .sys image after load
*   - relocate .data globals into EPT-hidden pool memory
*   everything is restored in DriverUnload (except the file wipe).
*   does NOT touch PsLoadedModuleList (PatchGuard-protected).
*/
#include "hv.h"

static BOOLEAN g_globals_pooled = FALSE;

/*
*   allocate the relocated globals struct. must run at PASSIVE_LEVEL before
*   vmx_init(): stealth_init_cpuid_cache() writes through the g_stealth_*
*   macros, and hostcr3_build() (inside vmx_init) must already see this
*   allocation so the page is present in the private host page tables.
*/
BOOLEAN
dkom_alloc_globals(VOID)
{
#if STEALTH_RELOC_GLOBALS
    g_dkom_globals = (DKOM_GLOBALS *)ExAllocatePool2(
        POOL_FLAG_NON_PAGED, sizeof(DKOM_GLOBALS), HV_POOL_TAG);
    if (!g_dkom_globals)
        return FALSE;
    g_globals_pooled = TRUE;
#else
    static DKOM_GLOBALS static_globals;
    g_dkom_globals = &static_globals;
#endif

    RtlZeroMemory(g_dkom_globals, sizeof(DKOM_GLOBALS));
    g_dkom_globals->stealth_enabled = STEALTH_ENABLED;
    return TRUE;
}

#if STEALTH_WIPE_IMAGE_FILE
/*
*   zero the on-disk driver image so file-based scans find nothing.
*   best effort: any failure just leaves the file intact.
*/
static VOID
dkom_wipe_image_file(PDRIVER_OBJECT driver_obj)
{
    PLDR_DATA_TABLE_ENTRY ldr = (PLDR_DATA_TABLE_ENTRY)driver_obj->DriverSection;
    HANDLE                h_file = NULL;
    IO_STATUS_BLOCK       iosb = {0};
    OBJECT_ATTRIBUTES     oa;
    NTSTATUS              status;

    if (!ldr)
        return;

    //
    // FullDllName holds the NT path of the .sys (e.g. \??\C:\...\Ophion.sys)
    //
    InitializeObjectAttributes(&oa, &ldr->FullDllName,
                               OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE,
                               NULL, NULL);

    status = ZwCreateFile(&h_file, GENERIC_WRITE, &oa, &iosb, NULL,
                          FILE_ATTRIBUTE_NORMAL, 0, FILE_OPEN,
                          FILE_NON_DIRECTORY_FILE | FILE_SYNCHRONOUS_IO_NONALERT,
                          NULL, 0);
    if (!NT_SUCCESS(status))
    {
        HV_LOG("[hv] DKOM: cannot open image file to wipe (0x%X)\n", status);
        return;
    }

    {
        FILE_STANDARD_INFORMATION fsi;
        status = ZwQueryInformationFile(h_file, &iosb, &fsi, sizeof(fsi),
                                        FileStandardInformation);
        if (NT_SUCCESS(status) && fsi.EndOfFile.QuadPart > 0)
        {
            UINT8 zeros[PAGE_SIZE] = {0};
            UINT64 remaining = fsi.EndOfFile.QuadPart;
            UINT64 offset    = 0;

            while (remaining > 0)
            {
                ULONG chunk = (remaining > PAGE_SIZE) ? PAGE_SIZE : (ULONG)remaining;
                LARGE_INTEGER off;
                off.QuadPart = (INT64)offset;

                status = ZwWriteFile(h_file, NULL, NULL, NULL, &iosb,
                                     zeros, chunk, &off, NULL);
                if (!NT_SUCCESS(status))
                    break;

                offset    += chunk;
                remaining -= chunk;
            }
        }
    }

    ZwClose(h_file);
    HV_LOG("[hv] DKOM: on-disk image wiped\n");
}
#endif

VOID
dkom_hide(PDRIVER_OBJECT driver_obj)
{
    g_dkom_state.driver_obj = driver_obj;
    RtlInitUnicodeString(&g_dkom_state.device_name, DEVICE_NAME);
    RtlInitUnicodeString(&g_dkom_state.symlink_name, SYMLINK_NAME);

#if STEALTH_HIDE_DEVICE
    //
    // unlink our device object from the driver's device chain.
    // DriverObject->DeviceObject is not PatchGuard-protected.
    //
    {
        PDEVICE_OBJECT dev = driver_obj->DeviceObject;
        PDEVICE_OBJECT * pp = &driver_obj->DeviceObject;

        while (*pp && *pp != dev)
            pp = &(*pp)->NextDevice;

        if (*pp == dev)
        {
            *pp = dev->NextDevice;
            g_dkom_state.device_obj      = dev;
            g_dkom_state.device_unlinked = TRUE;
        }
    }

    if (NT_SUCCESS(IoDeleteSymbolicLink(&g_dkom_state.symlink_name)))
        g_dkom_state.symlink_deleted = TRUE;
#endif

#if STEALTH_WIPE_IMAGE_FILE
    dkom_wipe_image_file(driver_obj);
#endif

    HV_LOG("[hv] DKOM: device object + symlink hidden\n");
}

VOID
dkom_restore(VOID)
{
#if STEALTH_HIDE_DEVICE
    //
    // re-insert the device object at the head of the chain and recreate the
    // symlink, so DriverUnload's IoDeleteSymbolicLink/IoDeleteDevice work
    //
    if (g_dkom_state.device_unlinked &&
        g_dkom_state.device_obj && g_dkom_state.driver_obj)
    {
        PDEVICE_OBJECT dev = g_dkom_state.device_obj;
        dev->NextDevice = g_dkom_state.driver_obj->DeviceObject;
        g_dkom_state.driver_obj->DeviceObject = dev;
        g_dkom_state.device_unlinked = FALSE;
    }

    if (g_dkom_state.symlink_deleted)
    {
        IoCreateSymbolicLink(&g_dkom_state.symlink_name, &g_dkom_state.device_name);
        g_dkom_state.symlink_deleted = FALSE;
    }
#endif
}

VOID
dkom_cleanup(VOID)
{
    if (g_dkom_globals && g_globals_pooled)
    {
        ExFreePoolWithTag(g_dkom_globals, HV_POOL_TAG);
        g_dkom_globals = NULL;
    }
}
