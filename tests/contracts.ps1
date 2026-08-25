Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$script:assertions = 0

function Assert([bool]$Condition, [string]$Message) {
    $script:assertions++
    if (-not $Condition) { throw "contract assertion failed: $Message" }
}
function Assert-Equal($Actual, $Expected, [string]$Message) {
    Assert ($Actual -eq $Expected) "$Message (expected=$Expected actual=$Actual)"
}
function Align([int]$Offset, [int]$Alignment) {
    return [int]([Math]::Floor(($Offset + $Alignment - 1) / [double]$Alignment) * $Alignment)
}

$repo = Split-Path -Parent $PSScriptRoot
$publicHeader = Get-Content -LiteralPath (Join-Path $repo 'include\hv_public.h') -Raw
$vmexit = Get-Content -LiteralPath (Join-Path $repo 'src\vmexit.c') -Raw
$ept = Get-Content -LiteralPath (Join-Path $repo 'src\ept.c') -Raw

# Public ABI constants and pack-8 layout.
Assert ($publicHeader -match '#pragma\s+pack\s*\(\s*push\s*,\s*8\s*\)') 'public status structures must use pack(8)'
Assert ($publicHeader -match '#define\s+HV_IOCTL_BASE\s+0x800\b') 'HV IOCTL base changed'
Assert ($publicHeader -match '#define\s+HV_STATUS_VERSION_1\s+1U?\b') 'status version changed'
Assert ($publicHeader -match '#define\s+HV_STATUS_MAX_VCPUS\s+256U?\b') 'maximum VCPU count changed'
Assert ($publicHeader -match '#define\s+HV_STATUS_EXIT_REASON_COUNT\s+128U?\b') 'exit counter count changed'
Assert ($publicHeader -match '#define\s+IOCTL_HV_STATUS\s+CTL_CODE\s*\(\s*FILE_DEVICE_UNKNOWN\s*,\s*HV_IOCTL_BASE\s*,\s*METHOD_BUFFERED\s*,\s*FILE_ANY_ACCESS\s*\)') 'status IOCTL definition changed'
$ioctl = (0x22 -shl 16) -bor (0x800 -shl 2)
Assert-Equal $ioctl 0x222000 'IOCTL_HV_STATUS numeric value changed'

function Get-Fields([string]$TypeName) {
    $match = [regex]::Match($publicHeader, "(?s)typedef\s+struct\s+_$TypeName\s*\{(?<body>.*?)\}\s*$TypeName\s*;")
    Assert $match.Success "$TypeName declaration missing"
    $fields = foreach ($field in [regex]::Matches($match.Groups['body'].Value, '(?m)^\s*(?<type>HV_UINT(?:8|16|32|64)|char|HV_STATUS_VCPU_V1)\s+(?<name>\w+)(?:\[(?<count>\w+)\])?\s*;')) {
        [pscustomobject]@{ Type = $field.Groups['type'].Value; Name = $field.Groups['name'].Value; Count = $field.Groups['count'].Value }
    }
    return @($fields)
}
function Assert-FieldSequence($Fields, [string[]]$Expected, [string]$TypeName) {
    $actual = @($Fields | ForEach-Object { if ($_.Count) { "$($_.Type) $($_.Name)[$($_.Count)]" } else { "$($_.Type) $($_.Name)" } })
    Assert-Equal ($actual -join '|') ($Expected -join '|') "$TypeName field sequence changed"
}
function Get-Layout($Fields, [hashtable]$KnownSizes) {
    $sizes = @{ HV_UINT8 = 1; HV_UINT16 = 2; HV_UINT32 = 4; HV_UINT64 = 8; char = 1 }
    foreach ($key in $KnownSizes.Keys) { $sizes[$key] = $KnownSizes[$key] }
    $counts = @{ HV_STATUS_EXIT_REASON_COUNT = 128; HV_STATUS_MAX_VCPUS = 256 }
    $offset = 0
    $maxAlign = 1
    $offsets = @{}
    foreach ($field in $Fields) {
        $size = [int]$sizes[$field.Type]
        $alignment = [Math]::Min($size, 8)
        $maxAlign = [Math]::Max($maxAlign, $alignment)
        $offset = Align $offset $alignment
        $offsets[$field.Name] = $offset
        $count = if (-not $field.Count) { 1 } elseif ($counts.ContainsKey($field.Count)) { $counts[$field.Count] } else { [int]$field.Count }
        $offset += $size * $count
    }
    return [pscustomobject]@{ Size = (Align $offset $maxAlign); Offsets = $offsets }
}

$vcpuFields = Get-Fields 'HV_STATUS_VCPU_V1'
Assert-FieldSequence $vcpuFields @(
    'HV_UINT32 Size', 'HV_UINT32 Index', 'HV_UINT16 Group', 'HV_UINT8 Number', 'HV_UINT8 Reserved0',
    'HV_UINT32 StateFlags', 'HV_UINT32 LastExitReason', 'HV_UINT32 LastFailure',
    'HV_UINT32 LastVmInstructionError', 'HV_UINT64 LastExitQualification', 'HV_UINT64 TotalExits',
    'HV_UINT64 CpuidExits', 'HV_UINT64 EptViolationExits', 'HV_UINT64 MonitorTrapExits',
    'HV_UINT64 RdtscExits', 'HV_UINT64 RdtscpExits', 'HV_UINT64 VmcallExits',
    'HV_UINT64 MsrReadExits', 'HV_UINT64 MsrWriteExits'
) 'HV_STATUS_VCPU_V1'
$vcpuLayout = Get-Layout $vcpuFields @{}
Assert-Equal $vcpuLayout.Size 112 'HV_STATUS_VCPU_V1 size changed'
Assert-Equal $vcpuLayout.Offsets.Group 8 'HV_STATUS_VCPU_V1 Group offset changed'
Assert-Equal $vcpuLayout.Offsets.LastExitQualification 32 'HV_STATUS_VCPU_V1 qualification offset changed'
Assert-Equal $vcpuLayout.Offsets.TotalExits 40 'HV_STATUS_VCPU_V1 TotalExits offset changed'

$statusFields = Get-Fields 'HV_STATUS_V1'
Assert-FieldSequence $statusFields @(
    'HV_UINT32 Size', 'HV_UINT32 Version', 'HV_UINT32 HeaderSize', 'HV_UINT32 Flags',
    'HV_UINT32 TotalProcessors', 'HV_UINT32 LaunchedProcessors', 'HV_UINT32 DetachedProcessors',
    'HV_UINT32 FailedProcessors', 'HV_UINT32 TerminalProcessors', 'HV_UINT32 ParentFlags',
    'HV_UINT32 ParentFeatures', 'HV_UINT32 CapabilityFlags', 'HV_UINT32 PreflightFailure',
    'HV_UINT32 LastFailure', 'HV_UINT32 LastVmInstructionError', 'HV_UINT32 PhysicalAddressBits',
    'HV_UINT64 MaximumGuestPhysicalAddress', 'char ParentVendor[16]',
    'HV_UINT64 AggregateExitCounters[HV_STATUS_EXIT_REASON_COUNT]',
    'HV_STATUS_VCPU_V1 Vcpu[HV_STATUS_MAX_VCPUS]'
) 'HV_STATUS_V1'
$statusLayout = Get-Layout $statusFields @{ HV_STATUS_VCPU_V1 = $vcpuLayout.Size }
Assert-Equal $statusLayout.Offsets.MaximumGuestPhysicalAddress 64 'maximum GPA offset changed'
Assert-Equal $statusLayout.Offsets.ParentVendor 72 'parent vendor offset changed'
Assert-Equal $statusLayout.Offsets.AggregateExitCounters 88 'aggregate exit counter offset changed'
Assert-Equal $statusLayout.Offsets.Vcpu 1112 'VCPU array/header size changed'
Assert-Equal $statusLayout.Size 29784 'HV_STATUS_V1 size changed'

# PMU widths are defined over an unsigned 64-bit mask.
function Pmu-WidthMask([int]$Width) {
    if ($Width -ge 64) { return [uint64]::MaxValue }
    if ($Width -le 0) { return [uint64]0 }
    [uint64]$mask = 0
    for ($i = 0; $i -lt $Width; $i++) { $mask = [uint64](($mask -shl 1) -bor 1) }
    return $mask
}
Assert-Equal (Pmu-WidthMask 0) ([uint64]0) 'zero-width PMU mask'
Assert-Equal (Pmu-WidthMask 1) ([uint64]1) 'one-bit PMU mask'
Assert-Equal (Pmu-WidthMask 48) ([uint64]0x0000FFFFFFFFFFFF) '48-bit PMU mask'
Assert-Equal (Pmu-WidthMask 63) ([uint64]0x7FFFFFFFFFFFFFFF) '63-bit PMU mask'
Assert-Equal (Pmu-WidthMask 64) ([uint64]::MaxValue) '64-bit PMU mask'
Assert-Equal (Pmu-WidthMask 65) ([uint64]::MaxValue) 'oversized PMU mask'
Assert ($vmexit -match '(?s)if\s*\(width\s*>=\s*64\).*?MAXULONG64.*?if\s*\(!width\).*?return\s+0.*?\(1ULL\s*<<\s*width\)\s*-\s*1') 'source PMU mask rules drifted'

# MTRR descriptors are half-open [base,end); check every edge and adjacency.
function In-Mtrr([uint64]$Address, [uint64]$Base, [uint64]$End) { return $Address -ge $Base -and $Address -lt $End }
Assert (-not (In-Mtrr 0x0FFF 0x1000 0x2000)) 'MTRR address below base matched'
Assert (In-Mtrr 0x1000 0x1000 0x2000) 'MTRR base must match'
Assert (In-Mtrr 0x1FFF 0x1000 0x2000) 'MTRR final byte must match'
Assert (-not (In-Mtrr 0x2000 0x1000 0x2000)) 'MTRR exclusive end matched'
Assert (In-Mtrr 0x2000 0x2000 0x3000) 'adjacent MTRR base must match exactly once'
Assert (-not (In-Mtrr 0x1000 0x1000 0x1000)) 'empty MTRR interval matched'
Assert ($ept -match 'page_addr\s*>=\s*range->phys_base\s*&&\s*page_addr\s*<\s*range->phys_end') 'source MTRR lookup is not half-open'

# Processor-group flattening: prefix sum + group-relative number is bijective.
$groupCounts = @(64, 3, 1)
$flattened = @()
$prefix = 0
for ($group = 0; $group -lt $groupCounts.Count; $group++) {
    for ($number = 0; $number -lt $groupCounts[$group]; $number++) {
        $flattened += [pscustomobject]@{ Group = $group; Number = $number; Index = $prefix + $number }
    }
    $prefix += $groupCounts[$group]
}
Assert-Equal $flattened.Count 68 'flattened processor count'
Assert-Equal (@($flattened.Index | Sort-Object -Unique).Count) $flattened.Count 'flattened indices must be unique'
Assert-Equal $flattened[63].Index 63 'last processor in group zero'
Assert-Equal $flattened[64].Index 64 'first processor in group one'
Assert-Equal $flattened[-1].Index 67 'last flattened processor'
foreach ($cpu in $flattened) { Assert ($cpu.Number -lt $groupCounts[$cpu.Group]) 'flattened processor inverse is invalid' }

# Hyper-V synthetic MSRs have an explicit feature and direction whitelist.
function HyperV-MsrAllowed([uint32]$Msr, [bool]$Write, [uint32]$Features = [uint32]::MaxValue, [bool]$ParentHyperV = $true) {
    if (-not $ParentHyperV) { return $false }
    switch ($Msr) {
        0x40000000 { return ($Features -band (1 -shl 5)) -ne 0 }
        0x40000001 { return ($Features -band (1 -shl 5)) -ne 0 }
        0x40000002 { return (-not $Write) -and (($Features -band (1 -shl 6)) -ne 0) }
        0x40000003 { return $Write -and (($Features -band (1 -shl 7)) -ne 0) }
        0x40000010 { return (-not $Write) -and (($Features -band 1) -ne 0) }
        0x40000020 { return (-not $Write) -and (($Features -band (1 -shl 1)) -ne 0) }
        0x40000021 { return ($Features -band (1 -shl 9)) -ne 0 }
        0x40000022 { return (-not $Write) -and (($Features -band (1 -shl 11)) -ne 0) }
        0x40000023 { return (-not $Write) -and (($Features -band (1 -shl 11)) -ne 0) }
        0x40000070 { return $Write -and (($Features -band (1 -shl 4)) -ne 0) }
        0x40000071 { return ($Features -band (1 -shl 4)) -ne 0 }
        0x40000072 { return ($Features -band (1 -shl 4)) -ne 0 }
        0x40000073 { return ($Features -band (1 -shl 4)) -ne 0 }
        0x40000080 { return ($Features -band (1 -shl 2)) -ne 0 }
        0x40000081 { return (-not $Write) -and (($Features -band (1 -shl 2)) -ne 0) }
        0x40000082 { return ($Features -band (1 -shl 2)) -ne 0 }
        0x40000083 { return ($Features -band (1 -shl 2)) -ne 0 }
        0x40000084 { return $Write -and (($Features -band (1 -shl 2)) -ne 0) }
        0x400000F0 { return $Write -and (($Features -band (1 -shl 10)) -ne 0) }
        default {
            if ($Msr -ge 0x40000090 -and $Msr -le 0x4000009F) { return ($Features -band (1 -shl 2)) -ne 0 }
            if ($Msr -ge 0x400000B0 -and $Msr -le 0x400000B7) { return ($Features -band (1 -shl 3)) -ne 0 }
            return $false
        }
    }
}
Assert (HyperV-MsrAllowed 0x40000000 $false) 'guest OS ID MSR read must be allowed'
Assert (HyperV-MsrAllowed 0x40000000 $true) 'guest OS ID MSR write must be allowed'
Assert (HyperV-MsrAllowed 0x40000002 $false) 'VP index read must be allowed'
Assert (-not (HyperV-MsrAllowed 0x40000002 $true)) 'VP index write must be rejected'
Assert (-not (HyperV-MsrAllowed 0x40000003 $false)) 'reset read must be rejected'
Assert (HyperV-MsrAllowed 0x40000003 $true) 'reset write must be allowed'
Assert (HyperV-MsrAllowed 0x40000081 $false) 'SINT read-only member must be readable'
Assert (-not (HyperV-MsrAllowed 0x40000081 $true)) 'SINT read-only member must reject writes'
Assert (-not (HyperV-MsrAllowed 0x40000084 $false)) 'EOI write-only member must reject reads'
Assert (HyperV-MsrAllowed 0x40000084 $true) 'EOI write-only member must accept writes'
Assert (-not (HyperV-MsrAllowed 0x400000FF $false)) 'unknown synthetic MSR must be rejected'
Assert (-not (HyperV-MsrAllowed 0x40000000 $false ([uint32]::MaxValue) $false)) 'synthetic MSR must be rejected without Hyper-V parent'
Assert ($vmexit -match 'parent_hyperv_msr_supported' -and $vmexit -match '(?s)case\s+0x40000002:.*?return\s+!write' -and $vmexit -match '(?s)case\s+0x40000003:.*?return\s+write') 'source Hyper-V direction whitelist drifted'

# Integer timer conversion must be saturating, monotonic, and division-safe.
function MulDiv-U64([uint64]$Value, [uint64]$Multiplier, [uint64]$Divisor) {
    if ($Divisor -eq 0 -or $Multiplier -eq 0) { return [uint64]0 }
    $wide = [System.Numerics.BigInteger]$Value * [System.Numerics.BigInteger]$Multiplier / [System.Numerics.BigInteger]$Divisor
    if ($wide -gt [System.Numerics.BigInteger][uint64]::MaxValue) { return [uint64]::MaxValue }
    return [uint64]$wide
}
Assert-Equal (MulDiv-U64 123 456 0) ([uint64]0) 'zero-divisor timer conversion'
Assert-Equal (MulDiv-U64 123 0 456) ([uint64]0) 'zero-multiplier timer conversion'
Assert-Equal (MulDiv-U64 3000000000 1000000 3000000000) ([uint64]1000000) 'exact timer conversion'
Assert-Equal (MulDiv-U64 1 3 2) ([uint64]1) 'timer conversion truncates toward zero'
Assert-Equal (MulDiv-U64 ([uint64]::MaxValue) 2 1) ([uint64]::MaxValue) 'timer conversion saturation'
$previous = [uint64]0
foreach ($value in 0, 1, 2, 3, 10, 1000, 1000000) {
    $converted = MulDiv-U64 $value 1000000000 3000000000
    Assert ($converted -ge $previous) 'timer conversion must be monotonic'
    $previous = $converted
}
Assert ($ept -match 'ept_mul_div_u64' -and $ept -match 'if\s*\(!divisor\s*\|\|\s*!multiplier\)' -and $ept -match 'quotient\s*>\s*MAXULONG64\s*/\s*multiplier') 'source timer conversion safeguards drifted'

# Unknown VM exits are terminal/error paths and must not skip an arbitrary guest instruction.
Assert ($vmexit -match '(?s)vmexit_enter_terminal\s*\([^)]*\)\s*\{.*?vcpu->advance_rip\s*=\s*FALSE;') 'terminal helper must suppress RIP advancement'
Assert ($vmexit -match '(?s)default:\s*vmexit_enter_terminal\s*\(\s*vcpu\s*,\s*HV_FAILURE_UNKNOWN_EXIT\s*\);\s*break;') 'unknown VM exits must enter the terminal path'

Write-Host "Contract assertions passed: $script:assertions"
