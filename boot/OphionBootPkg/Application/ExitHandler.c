/*
 * ExitHandler.c - C dispatcher for boot-hypervisor VM exits.
 *
 * v1 policy:
 *   CPUID   -> Hyper-V persona
 *   RDMSR/  -> persona floor for 0x40000000..2, native passthrough otherwise
 *   WRMSR
 *   VMCALL  -> permissive Hv hypercall floor (or shutdown via magic key)
 *   INVD/WBINVD -> execute natively on behalf of the guest
 *   everything else -> advance guest RIP (the firmware and Windows are
 *                      trusted to only trigger exits we explicitly armed)
 */

#include "OphionBoot.h"

/* exit reason numbers */
#define EXIT_CPUID          10
#define EXIT_INVD           13
#define EXIT_RDMSR          31
#define EXIT_WRMSR          32
#define EXIT_VMCALL         18
#define EXIT_WBINVD         54

/* VMCS fields used here */
#define VMCS_GUEST_RIP      0x0000681E
#define VMCS_GUEST_RSP      0x0000681C
#define VMCS_EXIT_REASON    0x00004402
#define VMCS_INSTR_LENGTH   0x0000440C
#define VMCS_VMEXIT_ERROR   0x00004400

#define VMCALL_SHUTDOWN_KEY 0x4E4F485950455256ULL /* 'NOHYPERV' in R12 */

extern UINT64 AsmVmread64 (UINT64 Field);
extern UINT64 AsmVmwrite64 (UINT64 Field, UINT64 Value);

#define VMEXIT_MSR_MASK 0xFFFFFFFFU

typedef struct {
    UINT32 Reason;
} OPB_EXIT_INFO;

STATIC OPB_GUEST_REGS *m_Regs;
STATIC OPB_VCPU *m_Vcpu;

STATIC
UINT64
VmRead64 (
    UINT64 Field
    )
{
    return AsmVmread64 (Field);
}

STATIC
VOID
VmWrite64 (
    UINT64 Field,
    UINT64 Value
    )
{
    AsmVmwrite64 (Field, Value);
}

STATIC
VOID
AdvanceRip (
    VOID
    )
{
    UINT64 Length = VmRead64 (VMCS_INSTR_LENGTH);
    UINT64 NewRip = VmRead64 (VMCS_GUEST_RIP) + Length;
    VmWrite64 (VMCS_GUEST_RIP, NewRip);
}

BOOLEAN
EFIAPI
OpbVmExitHandler (
    OPB_GUEST_REGS *Regs,
    OPB_VCPU *Vcpu
    )
{
    UINT64 Reason;
    UINT32 Leaf, SubLeaf, Eax, Ebx, Ecx, Edx;
    UINT32 Msr;
    UINT64 Value;
    EFI_STATUS Status;

    m_Regs = Regs;
    m_Vcpu = Vcpu;

    Reason = VmRead64 (VMCS_EXIT_REASON) & 0xFFFF;

    switch (Reason) {
    case EXIT_CPUID:
        Leaf = (UINT32)Regs->rax;
        SubLeaf = (UINT32)Regs->rcx;
        OpbPersonaCpuid (Leaf, SubLeaf, &Eax, &Ebx, &Ecx, &Edx);
        Regs->rax = Eax;
        Regs->rbx = Ebx;
        Regs->rcx = Ecx;
        Regs->rdx = Edx;
        AdvanceRip ();
        break;

    case EXIT_RDMSR:
        Msr = (UINT32)Regs->rcx;
        Status = OpbPersonaReadMsr (Vcpu, Msr, &Value);
        if (Status != EFI_SUCCESS) {
            Value = AsmReadMsr64 (Msr);
        }
        Regs->rax = Value & 0xFFFFFFFF;
        Regs->rdx = (Value >> 32) & 0xFFFFFFFF;
        AdvanceRip ();
        break;

    case EXIT_WRMSR:
        Msr = (UINT32)Regs->rcx;
        Value = (UINT64)Regs->rax | ((UINT64)Regs->rdx << 32);
        Status = OpbPersonaWriteMsr (Vcpu, Msr, Value);
        if (Status != EFI_SUCCESS) {
            AsmWriteMsr64 (Msr, Value);
        }
        AdvanceRip ();
        break;

    case EXIT_VMCALL:
        if (Regs->r12 == VMCALL_SHUTDOWN_KEY) {
            /* shutdown path: mark not-launched; the stub VMXOFFs and returns
             * to the post-VMLAUNCH continuation with rax = success. */
            Vcpu->launched = FALSE;
            Regs->rax = 0;
            return TRUE;
        }
        Regs->rax = OpbPersonaHypercall (Vcpu, Regs);
        AdvanceRip ();
        break;

    case EXIT_INVD:
        AsmInvd ();
        AdvanceRip ();
        break;

    case EXIT_WBINVD:
        AsmWbinvd ();
        AdvanceRip ();
        break;

    default:
        /* unexpected exit at boot: record and halt this core rather than
         * corrupting the boot flow. */
        Vcpu->last_exit_reason = (UINT32)Reason;
        Vcpu->terminal = TRUE;
        CpuDeadLoop ();
        break;
    }

    return FALSE;
}
