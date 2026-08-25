/*
 * Persona.c - Microsoft Hyper-V guest persona for the boot hypervisor.
 *
 * Goal: Windows caches its hypervisor identity from the first CPUID/MSR it
 * issues after the loader hands over. We present a coherent Win11-era
 * "Microsoft Hv" partition-0 identity, exactly like a machine running under
 * real Hyper-V, so detectors that special-case recognized hypervisors relax
 * their timing thresholds and system queries (NtQuerySystemInformation 0xC5)
 * agree with live CPUID.
 *
 * Persona invariants (mirror real Hyper-V root partition):
 *   CPUID.1: ECX[31]=1 (hypervisor present), ECX[5]=0 (VMX hidden)
 *   CPUID.0x40000000: vendor "Microsoft Hv", max hypervisor leaf
 *   CPUID.0x40000001: interface signature "Hv#1" (0x31237648)
 *   MSR 0x40000000..0x40000002: guest OS ID / hypercall page / VP index
 */

#include "OphionBoot.h"

#define HV_MSR_GUEST_OS_ID      0x40000000
#define HV_MSR_HYPERCALL_PAGE   0x40000001
#define HV_MSR_VP_INDEX         0x40000002

#define HV_MAX_LEAF             0x4000000A

/* canonical Hyper-V hypercall page stub: vmcall; ret */
STATIC CONST UINT8 mHypercallStub[16] = {
    0x0F, 0x01, 0xC1, 0xC3, 0xCC, 0xCC, 0xCC, 0xCC,
    0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC
};

/* "Microsoft Hv" little-endian */
#define HV_VENDOR_EBX 0x7263694D
#define HV_VENDOR_ECX 0x666F736F
#define HV_VENDOR_EDX 0x76482074

STATIC
VOID
OpbNativeCpuid (
    UINT32 Leaf,
    UINT32 SubLeaf,
    UINT32 *Eax,
    UINT32 *Ebx,
    UINT32 *Ecx,
    UINT32 *Edx
    )
{
    AsmCpuidEx (Leaf, SubLeaf, Eax, Ebx, Ecx, Edx);
}

VOID
OpbPersonaCpuid (
    UINT32 Leaf,
    UINT32 SubLeaf,
    UINT32 *Eax,
    UINT32 *Ebx,
    UINT32 *Ecx,
    UINT32 *Edx
    )
{
    /* everything the firmware/hardware answers stays native except the
     * hypervisor window and the VMX capability bit Hyper-V hides from a
     * root partition. */
    OpbNativeCpuid (Leaf, SubLeaf, Eax, Ebx, Ecx, Edx);

    switch (Leaf) {
    case 1:
        if (Ecx != NULL) {
            *Ecx |= BIT31;          /* hypervisor present bit            */
            *Ecx &= ~(UINT32)BIT5;  /* VMX hidden, root partition view   */
            *Ecx &= ~(UINT32)BIT6;  /* SMX hidden                         */
        }
        return;

    case 0x40000000:
        if (Eax != NULL) { *Eax = HV_MAX_LEAF; }
        if (Ebx != NULL) { *Ebx = HV_VENDOR_EBX; }
        if (Ecx != NULL) { *Ecx = HV_VENDOR_ECX; }
        if (Edx != NULL) { *Edx = HV_VENDOR_EDX; }
        return;

    case 0x40000001:
        if (Eax != NULL) { *Eax = 0x31237648; } /* "Hv#1" */
        if (Ebx != NULL) { *Ebx = 0; }
        if (Ecx != NULL) { *Ecx = 0; }
        if (Edx != NULL) { *Edx = 0; }
        return;

    /* PartitionPrivilegeFlags: a conservative Win11-era set.
     * bits set: AccessPartitionReferenceCounter(0), AccessSynicRegs(1),
     * AccessHypercallMsrs(2), AccessVpIndex(3), SynthTimer(4),
     * CreatePartitions disabled - root partition only needs timers/synic.
     * Everything else reads zero, which Windows tolerates by not using the
     * enlightenment. */
    case 0x40000003:
        if (Eax != NULL) { *Eax = 0x0000001F; }
        if (Ebx != NULL) { *Ebx = 0; }
        if (Ecx != NULL) { *Ecx = 0; }
        if (Edx != NULL) { *Edx = 0; }
        return;

    /* Implementation limits: recommend 1 VP index, virtual TSC available,
     * number of implemented synthetic timer MSRs following real Hyper-V. */
    case 0x40000004:
        if (Eax != NULL) { *Eax = 0x00000001; } /* number of HB banks   */
        if (Ebx != NULL) { *Ebx = 0x00000004; } /* HB banks recommended */
        if (Ecx != NULL) { *Ecx = 0; }
        if (Edx != NULL) { *Edx = 0; }
        return;

    /* HW features reported by implementation: no synthetic MMIO, no DMA
     * remapping claims; keep zero so Windows takes the unenlightened path. */
    case 0x40000005:
    case 0x40000006:
    case 0x40000007:
    case 0x40000008:
    case 0x40000009:
    case 0x4000000A:
        if (Eax != NULL) { *Eax = 0; }
        if (Ebx != NULL) { *Ebx = 0; }
        if (Ecx != NULL) { *Ecx = 0; }
        if (Edx != NULL) { *Edx = 0; }
        return;

    default:
        if (Leaf >= 0x40000000 && Leaf <= 0x4FFFFFFF) {
            /* unknown hypervisor leaf: report the terminal leaf like real
             * Hyper-V does for unmapped hypervisor-range leaves. */
            OpbNativeCpuid (HV_MAX_LEAF, 0, Eax, Ebx, Ecx, Edx);
        }
        return;
    }
}

EFI_STATUS
OpbPersonaReadMsr (
    OPB_VCPU *Vcpu,
    UINT32 Msr,
    UINT64 *Value
    )
{
    switch (Msr) {
    case HV_MSR_GUEST_OS_ID:
        *Value = Vcpu->hv_guest_os_id;
        return EFI_SUCCESS;
    case HV_MSR_HYPERCALL_PAGE:
        *Value = Vcpu->hv_hypercall_gpa;
        return EFI_SUCCESS;
    case HV_MSR_VP_INDEX:
        *Value = Vcpu->core_index;
        return EFI_SUCCESS;
    default:
        return EFI_UNSUPPORTED;
    }
}

EFI_STATUS
OpbPersonaWriteMsr (
    OPB_VCPU *Vcpu,
    UINT32 Msr,
    UINT64 Value
    )
{
    VOID *HypercallPage;
    UINT64 HypercallMsr;
    UINT64 Gpa;

    switch (Msr) {
    case HV_MSR_GUEST_OS_ID:
        Vcpu->hv_guest_os_id = Value;
        return EFI_SUCCESS;

    case HV_MSR_HYPERCALL_PAGE:
        if (Value == 0 || (Value & 1) == 0) {
            Vcpu->hv_hypercall_gpa = 0;
            return EFI_SUCCESS;
        }
        /*
         * Hv MSR 0x40000001 is GPA[51:12] plus enable bit0. UEFI is
         * identity-mapped at this point, so the GPA is directly writable.
         */
        Gpa = Value & 0x000FFFFFFFFFF000ULL;
        if (Gpa == 0 || (Value & 0xFFE) != 0) {
            return EFI_UNSUPPORTED;
        }
        HypercallPage = (VOID *)(UINTN)Gpa;
        CopyMem (HypercallPage, mHypercallStub, sizeof (mHypercallStub));
        SetMem ((UINT8 *)HypercallPage + sizeof (mHypercallStub),
                0x1000 - sizeof (mHypercallStub), 0xCC);
        HypercallMsr = Gpa | 1;
        Vcpu->hv_hypercall_gpa = HypercallMsr;
        return EFI_SUCCESS;

    case HV_MSR_VP_INDEX:
        return EFI_UNSUPPORTED;      /* read-only */

    default:
        return EFI_UNSUPPORTED;
    }
}

/*
 * Permissive hypercall floor: every hypercall the early Windows boot path
 * issues when it thinks Hyper-V is present must terminate cleanly. Flush,
 * debugger, and post-message style calls report success without action;
 * unknown call codes return the architectural failure so the guest retries
 * or falls back to the unenlightened path.
 */
UINT64
OpbPersonaHypercall (
    OPB_VCPU *Vcpu,
    OPB_GUEST_REGS *Regs
    )
{
    UINT16 CallCode;

    /* Hyper-V x64 hypercall input: RCX bits 15:0 = call code */
    CallCode = (UINT16)(Regs->rcx & 0xFFFF);

    switch (CallCode) {
    case 0x0001:                     /* HvFlushVirtualAddressSpace  */
    case 0x0002:                     /* HvFlushVirtualAddressList   */
    case 0x0003:                     /* HvGetGpaRanges              */
    case 0x0009:                     /* HvSetVpRegisters (probe)    */
    case 0x000B:                     /* HvPostMessage               */
    case 0x000C:                     /* HvSignalEvent               */
    case 0x0043:                     /* HvFlushVirtualAddressSpaceEx*/
    case 0x0044:                     /* HvFlushVirtualAddressListEx */
        Regs->rdx = 0;
        return 0;                     /* HV_STATUS_SUCCESS */

    default:
        Regs->rdx = 0;
        return 0x0002;                /* HV_STATUS_INVALID_HYPERCALL_CODE */
    }
}
