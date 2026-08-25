[CmdletBinding()]
param(
    [string]$ProbePath,
    [string]$OutputPath
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

if (-not $ProbePath) {
    $ProbePath = Join-Path $PSScriptRoot '..\build\bin\Release\OphionProbe.exe'
}

function Invoke-Probe([string]$Path) {
    if (-not (Test-Path $Path)) { return $null }
    $json = & $Path
    if ($LASTEXITCODE -ne 0) { throw "Probe failed with exit code $LASTEXITCODE." }
    return ($json -join "`n") | ConvertFrom-Json
}

$probe = Invoke-Probe $ProbePath
$computer = Get-CimInstance Win32_ComputerSystem
$hypervisorPresent = [bool]$computer.HypervisorPresent
$vendor = $null
$hypervisorCpuid = $false
if ($probe -and $probe.processors.Count -gt 0) {
    $first = $probe.processors[0].cpuid
    $hypervisorCpuid = (([uint32]$first.leaf1.ecx -band 0x80000000) -ne 0)
    if ($hypervisorCpuid) {
        $bytes = [System.Collections.Generic.List[byte]]::new()
        foreach ($word in @($first.hypervisorBase.ebx,
                             $first.hypervisorBase.ecx,
                             $first.hypervisorBase.edx)) {
            $bytes.AddRange([BitConverter]::GetBytes([uint32]$word))
        }
        $vendor = [Text.Encoding]::ASCII.GetString($bytes.ToArray()).Trim([char]0)
    }
}
try {
    $dg = Get-CimInstance -Namespace root\Microsoft\Windows\DeviceGuard -ClassName Win32_DeviceGuard -ErrorAction Stop
    $vbs = [bool]($dg.VirtualizationBasedSecurityStatus -ne 0)
} catch { }

$result = [ordered]@{
    schema = 'ophion.hyperv-preflight.v1'
    timestampUtc = [DateTime]::UtcNow.ToString('o')
    computerHypervisorPresent = $hypervisorPresent
    cpuidVendor = $vendor
    microsoftHyperV = ($vendor -eq 'Microsoft Hv')
    cpuidHypervisorPresent = $hypervisorCpuid
    vbsEnabled = $vbs
    probeSchema = if ($probe) { $probe.schema } else { $null }
    probeStatus = if ($probe) { $probe.status } else { $null }
    readyForAttachmentLab = ($hypervisorPresent -and $vendor -eq 'Microsoft Hv')
    boundary = 'Read-only preflight only. It does not modify boot state, Hyper-V, UEFI, Secure Boot, TPM, or drivers.'
}

$json = $result | ConvertTo-Json -Depth 6
if ($OutputPath) {
    $parent = Split-Path -Parent $OutputPath
    if ($parent) { New-Item -ItemType Directory -Path $parent -Force | Out-Null }
    Set-Content -LiteralPath $OutputPath -Value $json -Encoding utf8
}
$json
