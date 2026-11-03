/*
*   dkom.h - PG-safe driver hiding (dkom subset)
*   unlinks the device object, removes the dosdevices symlink, wipes the
*   on-disk image after load, and relocates .data globals into non-paged
*   pool memory (hidden from guest scans by the EPT decoy, see ept.c).
*
*   deliberately does NOT touch PsLoadedModuleList: it is PatchGuard (KPP)
*   protected and unlinking it bugchecks 0x109 on modern Windows. test
*   signing does not disable KPP. full module-list DKOM is gated behind
*   STEALTH_DKOM_MODULE (default 0) for PG-off systems only.
*/
#pragma once

#define DEVICE_NAME     L"\\Device\\Ophion"
#define SYMLINK_NAME    L"\\DosDevices\\Ophion"

//
// saved linkage so DriverUnload can restore everything before teardown
//
typedef struct _DKOM_STATE {
    PDRIVER_OBJECT driver_obj;
    PDEVICE_OBJECT device_obj;
    UNICODE_STRING device_name;
    UNICODE_STRING symlink_name;
    BOOLEAN        device_unlinked;
    BOOLEAN        symlink_deleted;
} DKOM_STATE, *PDKOM_STATE;

//
// relocated .data globals - lives in non-paged pool, hidden in the EPT
//
typedef struct _DKOM_GLOBALS {
    BOOLEAN             stealth_enabled;
    STEALTH_CPUID_CACHE cpuid_cache;
    HOST_IDT_STATE      host_idt;
} DKOM_GLOBALS, *PDKOM_GLOBALS;

extern DKOM_GLOBALS *g_dkom_globals;
extern DKOM_STATE    g_dkom_state;

BOOLEAN dkom_alloc_globals(VOID);
VOID    dkom_hide(PDRIVER_OBJECT driver_obj);
VOID    dkom_restore(VOID);
VOID    dkom_cleanup(VOID);
