# Ophion
<div align="center"> <img src="logo.png" alt="Demo" width="800"/> </div>

Intel VT-x Type-2 hypervisor research code for virtualizing an already running Windows system. The feature list below describes implemented code paths; detector compatibility and production stability are not implied. Current evidence is tracked explicitly in the evidence matrix.


***

## Blog

> **[Ophion — Building a Stealth Intel VT-x Hypervisor for Windows](https://websec.net/blog/ophion-building-a-stealth-intel-vt-x-hypervisor-for-windows-69b62daa7462693131828c97)**
>
> A detailed technical writeup covering the internals of Ophion — VMX bring-up, EPT construction, stealth mechanisms, and the lessons learned along the way. Thanks to [WebSec](https://websec.net) for the motivation and for hosting it.

***

## Features

- **Per-core VMX** — Virtualizes all logical processors via DPC broadcast. Clean VMXOFF on unload with guest CR3 restoration.
- **EPT with 2MB large pages** — Identity-mapped, MTRR-aware memory typing (fixed + variable ranges). Per-processor page tables with dynamic 2MB-to-4KB splitting for hooks.
- **VPID** — Capability-aware INVVPID (prefers type 3 retaining-globals, falls back gracefully).
- **System host CR3** — Uses a build-independent system-process CR3 by default. The stale private page-table snapshot path is retained but disabled.
- **Private host IDT** — Isolated IDT for VMX-root mode prevents NMI hijacking (guest corrupts OS IDT, triggers NMI while in host mode). NMIs are flagged and injected to guest on next VM-exit.
- **Private host GDT** — Per-core isolated GDT for VMX-root mode. Each core has its own TSS, so the private GDT is per-VCPU. On VMXOFF, clears TSS busy bit and reloads original GDTR/TR.
- **CPUID coherence** — Caches bare-metal CPUID at init, hides ECX[31] and invalid hypervisor leaves only on bare metal, and preserves an existing parent-hypervisor persona when running nested.
- **CR4.VMXE coherence** — VMXE is hidden initially, but guest writes update the read shadow exactly as bare metal does. A guest VMXON triggers a clean per-vCPU handoff so VMware can acquire VMX without a #UD/crash.
- **TSC compensation** — One-shot CPUID/RDTSC compensation preserves native CPUID cost without persistent per-core TSC offsets; the same pending bias is mirrored into HPET/LAPIC reads.
- **PMU virtualization** — Loads `IA32_PERF_GLOBAL_CTRL=0` in VMX root, restores/saves the guest mask across transitions, validates RDPMC privilege/type/index/width, and compensates APERF/MPERF root deltas.
- **Wall-clock virtualization** — ACPI-discovers HPET and virtualizes HPET main-counter plus xAPIC/x2APIC current-count reads. Per-vCPU EPT shadows and MTF close the MMIO permission window after one instruction.
- **MSR emulation** — Intercepts TSC, APERF/MPERF, PERF_GLOBAL_CTRL, x2APIC current count, feature-control, and VMX capability surfaces. Only CPUID-advertised Hyper-V synthetic MSRs are forwarded to a parent; unsupported probes get guest #GP.
- **External interrupt re-injection** — ACK-on-exit with deferred delivery via interrupt-window exiting when guest is not interruptible. TPR priority masking via shadowed CR8.
- **IDT vectoring** — Re-injects interrupted IDT events with priority. NMI deferral via NMI-window exiting on collision. Exception combining per SDM Table 6-5 (#DF generation, triple fault on #DF+exception).
- **Debug register passthrough** — Full DR0-DR7, CR8 save/restore on vmexit/vmentry. DR4/DR5 aliasing. Hardware BP matching merged into pending debug exceptions on RIP advance.
- **XSETBV validation** — SDM-compliant XCR0 validation using hardware capability mask from CPUID.0Dh.
- **MOV CR handling** — CR3 writes strip PCID bit 63 and flush through capability-gated INVVPID; CR4 writes preserve a coherent guest shadow while enforcing VMX fixed bits.
- **VMCALL gate** — Signature-verified (R10/R11/R12), CPL-checked (ring 0 only). User-mode VMCALL gets #UD.

***

## VMCS Configuration

| Field | Value |
|-------|-------|
| **Pin-based** | External-interrupt exiting, NMI exiting, virtual NMIs |
| **Primary proc** | TSC offsetting, RDPMC exiting, MSR bitmaps, I/O bitmaps, activate secondary. CR3/HLT/MOV-DR/RDTSC/INVLPG exiting may be forced by must-be-1 bits. MTF is enabled only for an active timer-MMIO retry. |
| **Secondary proc** | Required EPT; capability-gated VPID, RDTSCP, INVPCID, XSAVES/XRSTORS, and WAITPKG |
| **Exit** | 64-bit host, save debug controls, ACK interrupt on exit; capability-gated save/load of `IA32_PERF_GLOBAL_CTRL` |
| **Entry** | IA-32e mode guest, load debug controls; capability-gated guest `IA32_PERF_GLOBAL_CTRL` load |
| **PFEC mask/match** | Both 0 — all #PFs go to guest |
| **CR0 mask** | 0 (full pass-through) |
| **CR4 mask** | Bit 13 (VMXE) when stealth enabled; initial shadow is 0 and later guest writes persist |
| **MSR bitmap** | TSC, APERF/MPERF, PERF_GLOBAL_CTRL, x2APIC current count, IA32_FEATURE_CONTROL, and VMX capability MSRs |
| **I/O bitmaps** | All zeros |
| **EPT pointer** | WB cache, 4-level walk; per-vCPU 4KB HPET/xAPIC timer traps when available |
| **VPID** | Tag 1 |
| **HOST_CR3** | System CR3 by default; optional private snapshot remains disabled until its physical-page walker/coherency model is redesigned |
| **HOST_IDTR** | Private IDT with controlled handlers (or system IDT if disabled) |
| **HOST_GDTR** | Per-core private GDT copy (or system GDT if disabled) |

***

## Architecture

```
include/
    hv.h                Master header, function prototypes
    hv_types.h          Per-VCPU state, EPT structures, VMCALL numbers
    ia32.h              Intel architecture defines (MSRs, VMCS fields, EPT, control bits)
    stealth.h           Anti-detection feature toggles and types
    asm_prototypes.h    C prototypes for MASM routines

src/
    driver.c            DriverEntry, IOCTL dispatch, device setup
    vmx.c               VMX lifecycle (VMXON/VMCS alloc, VMCS programming, launch)
    vmexit.c            VM-exit handler (CPUID, CR, MSR, EPT, interrupts, etc.)
    ept.c               EPT init, MTRR map, identity mapping, tracked splits, HPET/xAPIC shadow hooks
    events.c            Exception/interrupt injection (#GP, #UD, #DF, #BP, #PF, external)
    broadcast.c         Multi-processor DPC broadcast for virtualize/terminate
    hostcr3.c           Private host page table deep-copy
    hostidt.c           Private host IDT for VMX-root mode
    hostgdt.c           Per-core private host GDT for VMX-root mode
    stealth.c           CPUID cache init, bare-metal cost calibration, XCR0 validation
    globals.c           Global variable definitions
    util.c              VA/PA translation, GDT/segment helpers

asm/
    AsmVmexitHandler.asm    VM-exit entry point (save/restore GPRs + XMM + MXCSR)
    AsmVmxContext.asm       Guest state save/restore for VMLAUNCH
    AsmVmxOperation.asm     CR4.VMXE enable, VMCALL with signature
    AsmVmxIntrinsics.asm    INVEPT/INVVPID wrappers
    AsmSegmentRegs.asm      Segment register getters/setters, GDT/IDT
    AsmCommon.asm           RFLAGS, GDTR/IDTR/TR reload, CR2 write
    AsmHostIdt.asm          Private host IDT handlers (NMI, #DF, #GP)
```

***

## Building

The portable build requires Visual Studio 2022 C++ tools and an installed Windows Driver Kit containing x64 kernel headers and libraries. It discovers `vswhere`, the newest x64 MSVC toolset, and the newest suitable WDK; it does not require the WDK Visual Studio extension.

```powershell
# Unsigned Release build, clean first, with compiler and MASM warnings fatal
pwsh -NoProfile -File .\build.ps1 -Configuration Release -Clean -WarningsAsErrors

# Debug plus MSVC code analysis
pwsh -NoProfile -File .\build.ps1 -Configuration Debug -CodeAnalysis

# Select an installed WDK explicitly
pwsh -NoProfile -File .\build.ps1 -WdkVersion $env:OPHION_WDK_VERSION
```

### Production stealth profile

`-Production` compiles `OPHION_PRODUCTION=1`: the status device, symbolic link, IOCTL dispatch, and every diagnostic log string are compiled out, the pool tag is replaced with a non-identifying one, and the embedded PDB reference is neutralized to `driver.pdb`. Per-exit state sampling (debug registers, CR8, APERF/MPERF, LAPIC counters) is reason-conditional in all profiles. The binary contains no device path, log text, build-machine path, or project-name string:

```powershell
pwsh -NoProfile -File .\build.ps1 -Configuration Release -Clean -WarningsAsErrors -Production
# -> build\bin\Release\Ophion-production.sys
```

The output is `build\bin\<Configuration>\Ophion.sys`. The script verifies that it is an x64 Native PE. Default output is unsigned. Signing is opt-in and the SHA1 certificate thumbprint is injected at invocation time:

```powershell
pwsh -NoProfile -File .\build.ps1 -Configuration Release `
  -CertificateThumbprint $env:OPHION_CERT_SHA1 `
  -TimestampUrl $env:OPHION_TIMESTAMP_URL
```

The signing path checks the resulting signer thumbprint and runs kernel-policy signature verification. For a Visual Studio WDK-integrated build, the project remains usable directly:

```powershell
msbuild .\Ophion.sln /m /p:Configuration=Release /p:Platform=x64

# Optional project-level WDK selection and signing
msbuild .\Ophion.sln /m /p:Configuration=Release /p:Platform=x64 `
  /p:OphionWdkVersion=$env:OPHION_WDK_VERSION `
  /p:OphionCertificateThumbprint=$env:OPHION_CERT_SHA1
```

Run the dependency-free contract assertions separately:

```powershell
pwsh -NoProfile -File .\tests\contracts.ps1
```

GitHub-hosted Windows CI runs the contract assertions and builds the user-mode probe. It does not compile the driver because the hosted image does not guarantee WDK kernel headers and libraries.

***

## Boot-time mode

[`boot/OphionBootPkg`](boot/README.md) is the EDK2 boot-time build: a resident DXE driver starts the VMX layer before the Windows loader and establishes a coherent `Microsoft Hv` persona from the first Windows instruction. This is the only architecture that removes the post-boot hypervisor-transition discontinuity; it is compile-verified as a `RELEASE` EFI driver and must first be exercised with OVMF, not a production ESP or firmware flash.

```powershell
pwsh -NoProfile -File .\boot\build.ps1 `
  -Edk2Root $env:OPHION_EDK2 `
  -NasmPrefix $env:NASM_PREFIX `
  -IaslPrefix $env:IASL_PREFIX `
  -Target RELEASE
```
## Loading

Loading is intentionally not automated. An administrator can register a driver produced or signed for the target machine:

```powershell
$driver = (Resolve-Path .\build\bin\Release\Ophion.sys).Path
sc.exe create Ophion type= kernel binPath= $driver
sc.exe start Ophion

sc.exe stop Ophion
sc.exe delete Ophion
```

The default build is unsigned and will not load under normal Windows kernel-signing policy. Test-signing policy and certificate provisioning are operator-managed prerequisites.

***
## Stealth Toggles

Defined in `include/stealth.h`:

| Toggle | Default | Description |
|--------|---------|-------------|
| `STEALTH_ENABLED` | 1 | Master stealth switch |
| `STEALTH_HIDE_CR4_VMXE` | 1 | Hide CR4.VMXE from guest via CR4 shadow |
| `STEALTH_COMPENSATE_TIMING` | 1 | TSC compensation for RDTSC+CPUID+RDTSC timing attacks |
| `STEALTH_CPUID_CACHING` | 1 | Cache native CPUID responses for invalid/hypervisor leaves |
| `USE_PRIVATE_HOST_CR3` | 0 | Disabled: avoids stale/invalid private page-table snapshots on current Windows builds |
| `STEALTH_VIRTUALIZE_PMU` | 1 | Exclude VMX-root PMCs and compensate APERF/MPERF |
| `STEALTH_VIRTUALIZE_TIMERS` | 1 | Virtualize HPET and LAPIC current-count reads |
| `USE_PRIVATE_HOST_IDT` | 1 | Isolated host IDT (prevents NMI hijacking in VMX-root) |
| `USE_PRIVATE_HOST_GDT` | 1 | Per-core isolated host GDT |

***

## Runtime probe

The x64 user-mode probe is read-only: it opens `\\.\Ophion`, requests `HV_STATUS_V1` with a legacy `UINT32` fallback, pins its thread to every active group-aware logical processor, and emits CPUID and timing samples as JSON. It does not load a driver, write an MSR, or change system configuration.

```powershell
pwsh -NoProfile -File .\tools\build-probe.ps1 -Configuration Release -WarningsAsErrors
.\build\bin\Release\OphionProbe.exe --samples 1000 |
  Set-Content .\build\probe.json -Encoding utf8
```

The stable top-level schema identifier is `ophion.probe.v1`. Each processor record contains the processor group/number, CPUID leaf 1, Hyper-V base/interface leaves, two invalid-leaf responses, and paired `RDTSC-CPUID-RDTSC`/QPC deltas.

## Pinned detector sources

`tools\detectors.json` pins public detector repositories and records their expected build command and result artifact. The management script never executes detector binaries. Without `-Fetch` it performs no network activity:

```powershell
# Inspect already-present checkouts only
pwsh -NoProfile -File .\tools\detectors.ps1

# Explicitly clone/fetch and detach each checkout at its pinned revision
pwsh -NoProfile -File .\tools\detectors.ps1 -Fetch
```

The manifest covers VMAware, hvdetecc, checkhv_um, void-stack Hypervisor-Detection, and momo5502 EPT hook detection.

## Evidence matrix

Evidence status as of **2026-08-25**:

| Scope or claim | Compile-verified | Runtime-verified | Evidence / limit |
|---|---:|---:|---|
| Portable unsigned driver build | Yes | No | Release and Debug x64 built with WDK 10.0.26100.0 and `/WX`; Native x64 PE headers verified by `build.ps1`. |
| Optional test-signed driver build | Yes | No | Release signing and Authenticode verification passed with an injected local certificate thumbprint; kernel trust/loading was not exercised. |
| Contract assertion script | Yes | N/A | `tests\contracts.ps1` passed 145 assertions. |
| Production stealth profile | Yes | No | Release production build passed `/WX`; binary scan confirmed no device path, log string, machine path, or project name in ASCII or UTF-16, PDB reference neutralized. |
| `OphionBoot.efi` boot-time DXE driver | Yes | No | EDK2/VS2022 `RELEASE` build passed; SHA-256 `79b9d6fc8a9370a79153d3cc1a4a4b305c970f02bf19c038b387a14226f5d3ed`. OVMF/hardware boot is pending. |
| VMX launch, unload, and all-core status | N/A | No | Requires a compatible Intel host, a loadable signed/test-signed driver, and a captured status artifact. |
| Detector compatibility | N/A | No | `vmaware.png` is retained as a historical image, but it lacks pinned revision/configuration/raw-result provenance and is not current verification. |
| EAC, BattlEye, or antivirus compatibility | N/A | No | No reproducible artifact is tracked; no compatibility claim is made. |

***

## Disclaimer

This project has not been thoroughly tested for long-term usage or stability. It is intended primarily as a learning resource and a foundation for further development. Use at your own risk.

***

## Credits

- [HyperDbg](https://github.com/HyperDbg/HyperDbg) — Referenced for VMX architecture and VM-exit handling patterns
- [humzak711](https://github.com/humzak711) — Stealth feedback: MSR feature hiding (IA32_FEATURE_CONTROL, VMX MSRs), CPUID SMX masking
- [VMAware](https://github.com/kernelwernel/VMAware) — Hypervisor detection testing
- [hvdetecc](https://github.com/can1357/hvdetecc) — Hypervisor detection testing
- [Claude](https://claude.ai) — Debugging assistance and IA-32 architecture research

## License

MIT
