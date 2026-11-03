/*
*   globals.c - global variable definitions
*/
#include "hv.h"

VIRTUAL_MACHINE_STATE *g_vcpu               = NULL;
EPT_STATE *g_ept                            = NULL;
UINT32 g_cpu_count                          = 0;
UINT64 g_system_cr3                         = 0;
UINT64 *g_msr_bitmap_invalid                = NULL;
volatile LONG g_host_nmi_pending[MAX_PROCESSORS] = {0};
volatile LONG g_core_launched[MAX_PROCESSORS]    = {0};

DKOM_GLOBALS *g_dkom_globals                = NULL;
DKOM_STATE    g_dkom_state                  = {0};
