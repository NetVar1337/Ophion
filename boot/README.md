# OphionBoot — boot-time Hyper-V persona

`OphionBoot.efi` is an EDK2 x64 resident DXE driver that brings up Ophion before the Windows loader continues. It captures the UEFI caller's complete register frame, virtualizes every logical processor through `EFI_MP_SERVICES_PROTOCOL`, identity-maps firmware-declared RAM/MMIO with EPT, and presents a Microsoft Hyper-V CPUID/MSR/hypercall-page floor from the first Windows instruction.

This changes the detection model: Windows caches a coherent `Microsoft Hv` platform identity at boot, rather than observing a post-boot hidden VMX transition. It does **not** make virtualization physically unmeasurable; it establishes a legitimate Hyper-V-compatible identity from S0.

## Build

Prerequisites: an EDK2 checkout with Windows BaseTools, NASM, iasl, and Visual Studio C++ x64 tools. The package was compiled with EDK2/VS2022 as both `NOOPT` and `RELEASE` during this work.

```powershell
pwsh -NoProfile -File .\boot\build.ps1 `
  -Edk2Root $env:OPHION_EDK2 `
  -NasmPrefix $env:NASM_PREFIX `
  -IaslPrefix $env:IASL_PREFIX `
  -Target RELEASE
```

Expected output:

```text
<Edk2Root>\Build\OphionBoot\RELEASE_VS2022\X64\OphionBoot.efi
```

The validated Release artifact built on this workstation hashed to:

```text
c1dba45c5ab98e215be1f62c2844ffa9824a388b5ff81351c81edefb8b3bb281
```

## Lab boot path

1. Use a disposable OVMF VM first. Add `OphionBootPkg/Application/OphionBoot.inf` to the OVMF platform DSC `[Components]` and rebuild OVMF so this **DXE driver** remains resident through `ExitBootServices`; do not launch the EFI file as a transient `StartImage` application.
2. Keep Secure Boot off for the unsigned lab image, or enroll a test key and sign the OVMF/DXE image.
3. Capture serial/debug output, then confirm Windows reaches the loader with CPUID.1.ECX[31] set and `CPUID(0x40000000)` equal to `Microsoft Hv`.
4. Run the repository's `OphionProbe`, VMAware, hvdetecc, and the Hyper-V stratum of the detector matrix from a disposable account/VM only.

## Current boundary

This is a compiled boot-time VMX/HV-persona foundation, not a firmware flasher or a finished Hyper-V implementation. It intentionally provides only the boot-critical Hyper-V MSR/hypercall floor: guest OS ID, hypercall-page registration, VP index, and no-op success for flush/post-message style hypercalls. Expand it only from trace evidence captured during the OVMF boot path.

Do not modify `bootmgfw.efi`, SPI flash, measured-boot state, TPM state, or a production ESP until the OVMF boot matrix is green.
