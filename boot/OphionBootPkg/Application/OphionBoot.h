/*
 * OphionBoot.h - shared definitions for the boot-time hypervisor.
 *
 * UEFI firmware identity-maps installed RAM, so virtual == physical for
 * every allocation made here. All per-core state lives in one structure
 * addressed from the fixed HOST_RSP slot, exactly like the runtime core.
 */
#ifndef OPHION_BOOT_H_
#define OPHION_BOOT_H_

#include <Uefi.h>
#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/DebugLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Register/Intel/Cpuid.h>
#include <Register/Intel/Msr.h>

#define OPB_MAX_PROCESSORS  64
#define OPB_VMM_STACK_SIZE  0x8000
#define OPB_POOL_ALIGNMENT  0x1000

typedef struct {
    UINT64 rax, rcx, rdx, rbx, rsp, rbp, rsi, rdi;
    UINT64 r8, r9, r10, r11, r12, r13, r14, r15;
} OPB_GUEST_REGS;

typedef struct {
    /* must stay first: assembly locates it relative to HOST_RSP */
    volatile INT32 nmi_pending;

    UINT64 vmxon_pa;
    UINT64 vmcs_pa;
    UINT64 vmm_stack;             /* top of private host stack */
    UINT64 msr_bitmap;            /* 4KB, physical == virtual */

    UINT32 core_index;
    BOOLEAN launched;
    BOOLEAN terminal;
    UINT32 last_exit_reason;

    /* persona floor state */
    UINT64 hv_guest_os_id;        /* MSR 0x40000000 shadow */
    UINT64 hv_hypercall_gpa;      /* GPA written to MSR 0x40000001 */

    OPB_GUEST_REGS regs;
} OPB_VCPU;

#pragma pack(push, 1)
typedef struct {
    UINT16 Limit;
    UINT64 Base;
} OPB_DTR;
#pragma pack(pop)
extern OPB_VCPU g_opb_vcpu[OPB_MAX_PROCESSORS];
extern UINT32 g_opb_cpu_count;

/* Assembly.nasm */
extern EFI_STATUS
EFIAPI
AsmEnableVmxAndVmxon (
    UINT64 VmxonPhysicalAddress
    );

extern UINT64
EFIAPI
AsmReadCr0 (VOID);
extern UINT64
EFIAPI
AsmReadCr3 (VOID);
extern UINT64
EFIAPI
AsmReadCr4 (VOID);
/* AsmWriteCr0/AsmWriteCr4/AsmReadMsr64/AsmWriteMsr64 come from BaseLib */

extern EFI_STATUS
EFIAPI
OpbVmclear (
    UINT64 *VmcsPhysicalAddress
    );

extern EFI_STATUS
EFIAPI
OpbVmptrld (
    UINT64 *VmcsPhysicalAddress
    );

extern VOID
EFIAPI
AsmVmExitStub (VOID);

extern EFI_STATUS
EFIAPI
AsmInveptSingleContext (
    UINT64 EptPointer
    );

extern VOID OpbAsmSgdt (OPB_DTR *Gdtr);
extern VOID OpbAsmSidt (OPB_DTR *Idtr);
extern UINT64 OpbAsmReadRflags (VOID);
extern UINT64 OpbAsmReadRsp (VOID);
extern UINT64 OpbGetLaunchRip (VOID);
extern UINT64 OpbGetLaunchRsp (VOID);
extern VOID OpbSetHostStackTop (VOID *StackTop);
extern VOID OpbGdtSetTr (UINT16 Selector);
extern VOID OpbGdtReadTrBase (UINT64 *TrBase);
extern VOID OpbAsmLaunchResume (VOID);

/* VmxCore.c */
EFI_STATUS
OpbSetupCurrentCore (
    UINT32 CoreIndex,
    VOID *GuestStack
    );

EFI_STATUS
EFIAPI
OpbAsmSaveAndVirtualize (
    UINT32 CoreIndex
    );

/* ExitHandler.c */
BOOLEAN
EFIAPI
OpbVmExitHandler (
    OPB_GUEST_REGS *Regs,
    OPB_VCPU *Vcpu
    );

/* Persona.c */
VOID
OpbPersonaCpuid (
    UINT32 Leaf,
    UINT32 SubLeaf,
    UINT32 *Eax,
    UINT32 *Ebx,
    UINT32 *Ecx,
    UINT32 *Edx
    );

EFI_STATUS
OpbPersonaReadMsr (
    OPB_VCPU *Vcpu,
    UINT32 Msr,
    UINT64 *Value
    );

EFI_STATUS
OpbPersonaWriteMsr (
    OPB_VCPU *Vcpu,
    UINT32 Msr,
    UINT64 Value
    );

UINT64
OpbPersonaHypercall (
    OPB_VCPU *Vcpu,
    OPB_GUEST_REGS *Regs
    );

#endif /* OPHION_BOOT_H_ */
