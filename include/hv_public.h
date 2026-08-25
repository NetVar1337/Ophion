/*
 * Stable user/kernel ABI for Ophion status queries.
 */
#pragma once

#ifndef CTL_CODE
#define CTL_CODE(DeviceType, Function, Method, Access) \
    (((DeviceType) << 16) | ((Access) << 14) | ((Function) << 2) | (Method))
#endif
#ifndef FILE_DEVICE_UNKNOWN
#define FILE_DEVICE_UNKNOWN 0x00000022
#endif
#ifndef METHOD_BUFFERED
#define METHOD_BUFFERED 0
#endif
#ifndef FILE_ANY_ACCESS
#define FILE_ANY_ACCESS 0
#endif

typedef unsigned __int8  HV_UINT8;
typedef unsigned __int16 HV_UINT16;
typedef unsigned __int32 HV_UINT32;
typedef unsigned __int64 HV_UINT64;

#define HV_IOCTL_BASE               0x800
#define IOCTL_HV_STATUS             CTL_CODE(FILE_DEVICE_UNKNOWN, HV_IOCTL_BASE, METHOD_BUFFERED, FILE_ANY_ACCESS)

#define HV_STATUS_VERSION_1         1U
#define HV_STATUS_MAX_VCPUS         256U
#define HV_STATUS_EXIT_REASON_COUNT 128U

#define HV_PARENT_PRESENT           0x00000001U
#define HV_PARENT_HYPERV            0x00000002U
#define HV_PARENT_NESTED_RESTRICTED 0x00000004U

#define HV_CAP_EPT                  0x00000001U
#define HV_CAP_EPT_2MB              0x00000002U
#define HV_CAP_EPT_AD               0x00000004U
#define HV_CAP_MTF                  0x00000008U
#define HV_CAP_INVVPID              0x00000010U
#define HV_CAP_WAITPKG              0x00000020U
#define HV_CAP_PMU                  0x00000040U
#define HV_CAP_HPET                 0x00000080U
#define HV_CAP_XAPIC                0x00000100U

#define HV_VCPU_LAUNCHED            0x00000001U
#define HV_VCPU_DETACHED            0x00000002U
#define HV_VCPU_FAILED              0x00000004U
#define HV_VCPU_TERMINAL            0x00000008U

#define HV_FAILURE_NONE                         0U
#define HV_FAILURE_PROCESSOR_CAPACITY           1U
#define HV_FAILURE_VMX_UNAVAILABLE              2U
#define HV_FAILURE_EPT_UNAVAILABLE              3U
#define HV_FAILURE_GPA_COVERAGE                 4U
#define HV_FAILURE_REQUIRED_CONTROLS            5U
#define HV_FAILURE_NESTED_RESTRICTION           6U
#define HV_FAILURE_ALLOCATION                   7U
#define HV_FAILURE_VMCS_WRITE                   8U
#define HV_FAILURE_VMCS_READ                    9U
#define HV_FAILURE_INVEPT                      10U
#define HV_FAILURE_INVVPID                     11U
#define HV_FAILURE_VM_ENTRY                    12U
#define HV_FAILURE_TRIPLE_FAULT                13U
#define HV_FAILURE_UNKNOWN_EXIT                14U
#define HV_FAILURE_EPT_MISCONFIGURATION        15U

#pragma pack(push, 8)
typedef struct _HV_STATUS_VCPU_V1 {
    HV_UINT32 Size;
    HV_UINT32 Index;
    HV_UINT16 Group;
    HV_UINT8  Number;
    HV_UINT8  Reserved0;
    HV_UINT32 StateFlags;
    HV_UINT32 LastExitReason;
    HV_UINT32 LastFailure;
    HV_UINT32 LastVmInstructionError;
    HV_UINT64 LastExitQualification;
    HV_UINT64 TotalExits;
    HV_UINT64 CpuidExits;
    HV_UINT64 EptViolationExits;
    HV_UINT64 MonitorTrapExits;
    HV_UINT64 RdtscExits;
    HV_UINT64 RdtscpExits;
    HV_UINT64 VmcallExits;
    HV_UINT64 MsrReadExits;
    HV_UINT64 MsrWriteExits;
} HV_STATUS_VCPU_V1;

typedef struct _HV_STATUS_V1 {
    HV_UINT32 Size;
    HV_UINT32 Version;
    HV_UINT32 HeaderSize;
    HV_UINT32 Flags;
    HV_UINT32 TotalProcessors;
    HV_UINT32 LaunchedProcessors;
    HV_UINT32 DetachedProcessors;
    HV_UINT32 FailedProcessors;
    HV_UINT32 TerminalProcessors;
    HV_UINT32 ParentFlags;
    HV_UINT32 ParentFeatures;
    HV_UINT32 CapabilityFlags;
    HV_UINT32 PreflightFailure;
    HV_UINT32 LastFailure;
    HV_UINT32 LastVmInstructionError;
    HV_UINT32 PhysicalAddressBits;
    HV_UINT64 MaximumGuestPhysicalAddress;
    char      ParentVendor[16];
    HV_UINT64 AggregateExitCounters[HV_STATUS_EXIT_REASON_COUNT];
    HV_STATUS_VCPU_V1 Vcpu[HV_STATUS_MAX_VCPUS];
} HV_STATUS_V1;
#pragma pack(pop)
