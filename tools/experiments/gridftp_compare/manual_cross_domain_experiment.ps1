[CmdletBinding()]
param(
    [ValidateSet('plan','pilot','full')]
    [string]$Mode = 'plan',
    [ValidateSet('both','shenzhen-to-shanghai','shanghai-to-shenzhen')]
    [string]$Direction = 'both',
    [ValidateSet(0,1)]
    [int]$LookaheadDepth = 0,
    [ValidateRange(1,50)]
    [int]$Repeats = 3,
    [ValidateSet('single','dense','mixed','all')]
    [string]$Dataset = 'all',
    [string]$Seed = '20260924',
    [string]$OutputRoot = '',
    [switch]$Execute
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

# This entry point deliberately defaults to a local plan. The execution branch is
# fail-closed and requires an explicit dynamic-QA release marker.
$RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..\..')).Path
$SshExe = 'C:\Windows\System32\OpenSSH\ssh.exe'
$RunStamp = Get-Date -Format 'yyyyMMdd-HHmmss'
if ([string]::IsNullOrWhiteSpace($OutputRoot)) {
    $OutputRoot = Join-Path $RepoRoot "tools\experiments\gridftp_compare\manual-plans\$RunStamp"
}

function Get-Sha256([string]$Path) {
    if (Test-Path -LiteralPath $Path -PathType Leaf) {
        return (Get-FileHash -Algorithm SHA256 -LiteralPath $Path).Hash.ToLowerInvariant()
    }
    return $null
}

function Add-Case([System.Collections.Generic.List[object]]$Cases, [string]$DatasetName, [int]$Bytes, [int]$FileCount, [string]$DirectionName, [string]$SystemName, [int]$RepeatIndex) {
    $arm = if ($SystemName -eq 'cpnetflux') { 'cpnetflux' } else { 'gridftp' }
    $caseId = "manual_${DatasetName}_${DirectionName}_${SystemName}_lookahead${LookaheadDepth}_r${RepeatIndex}"
    $sourceEndpoint = if ($DirectionName -eq 'shenzhen-to-shanghai') { 'gridflux-beta-shenzhen' } else { 'gridflux-beta-shanghai' }
    $destinationEndpoint = if ($DirectionName -eq 'shenzhen-to-shanghai') { 'gridflux-beta-shanghai' } else { 'gridflux-beta-shenzhen' }
    $Cases.Add([ordered]@{
        case_id = $caseId
        arm = $arm
        system = $SystemName
        dataset = $DatasetName
        logical_bytes = $Bytes
        file_count = $FileCount
        direction = $DirectionName
        repeat = $RepeatIndex
        seed = $Seed
        lookahead_depth = $LookaheadDepth
        topology = [ordered]@{
            manual_launcher = 'windows'
            experiment_controller = 'gridflux-beta-shenzhen'
            transfer_client_host = 'gridflux-beta-shenzhen'
            peer_service_host = 'gridflux-beta-shanghai'
            measured_data_path = 'shenzhen<->shanghai'
            windows_in_data_path = $false
            source_endpoint = $sourceEndpoint
            destination_endpoint = $destinationEndpoint
            data_flows_directly_between_linux_endpoints = $true
        }
        cpnetflux_config = if ($SystemName -eq 'cpnetflux') { [ordered]@{ file_io_backend='posix'; control_reuse='worker'; scheduler='off'; compression='off'; checksum='none'; resume=$false } } else { $null }
        gridftp_config = if ($SystemName -eq 'gridftp') { [ordered]@{ tool='globus-url-copy'; auth='gsi'; data_channel_privacy=$true } } else { $null }
        status = 'planned'
        transfer_status = 'unknown'
        integrity_status = 'unknown'
        evidence_status = 'not_started'
        wire_accounting_status = 'unknown'
        performance_eligible = $false
    }) | Out-Null
}

$directions = switch ($Direction) {
    'both' { @('shenzhen-to-shanghai','shanghai-to-shenzhen'); break }
    default { @($Direction) }
}
$datasets = switch ($Dataset) {
    'all' { @('single','dense','mixed'); break }
    default { @($Dataset) }
}
$datasetSpec = @{
    single = @{ bytes = 268435456; files = 1; label = 'single_256MiB' }
    dense = @{ bytes = 134217728; files = 128; label = 'tree_dense_128MiB' }
    mixed = @{ bytes = 268435456; files = 148; label = 'tree_mixed_256MiB' }
}

if ($Execute -and $Mode -eq 'plan') { throw '-Execute requires -Mode pilot or -Mode full' }
if ($Execute) {
    throw 'Execution is disabled until 05 revision-09 fixed build and 04 dynamic QA publish an explicit release marker. Generate a plan without -Execute.'
}

$cases = [System.Collections.Generic.List[object]]::new()
foreach ($d in $datasets) {
    foreach ($dir in $directions) {
        foreach ($repeat in 0..($Repeats-1)) {
            Add-Case $cases $datasetSpec[$d].label $datasetSpec[$d].bytes $datasetSpec[$d].files $dir 'cpnetflux' $repeat
            Add-Case $cases $datasetSpec[$d].label $datasetSpec[$d].bytes $datasetSpec[$d].files $dir 'gridftp' $repeat
        }
    }
}

New-Item -ItemType Directory -Force -Path $OutputRoot | Out-Null
$inputPaths = @(
    (Join-Path $RepoRoot 'tools\experiments\gridftp_compare\manual_cross_domain_experiment.ps1'),
    (Join-Path $RepoRoot 'docs\tasks\2026-09-24-manual-cross-domain-experiment-package-01.md')
)
$inputHashes = [ordered]@{}
foreach ($path in $inputPaths) { $inputHashes[$path] = Get-Sha256 $path }
$gitHead = (& git -C $RepoRoot rev-parse HEAD).Trim()
$manifest = [ordered]@{
    package = 'MANUAL-CROSS-DOMAIN-EXPERIMENT-PACKAGE-01'
    readiness = 'READY_PENDING_DYNAMIC_QA'
    generated_at = (Get-Date).ToUniversalTime().ToString('o')
    mode = $Mode
    execute_requested = [bool]$Execute
    direction = $Direction
    lookahead_depth = $LookaheadDepth
    repeats = $Repeats
    dataset_selection = $Dataset
    seed = $Seed
    git_head = $gitHead
    fixed_build_gate = '05 revision-09 required; not asserted by this script'
    dynamic_qa_gate = '04 dynamic acceptance required; not asserted by this script'
    ssh = [ordered]@{ executable=$SshExe; batch_mode=$true; strict_host_key_checking='yes'; endpoints=@('gridflux-beta-shenzhen','gridflux-beta-shanghai') }
    topology = [ordered]@{
        manual_launcher = 'windows'
        experiment_controller = 'gridflux-beta-shenzhen'
        transfer_client_host = 'gridflux-beta-shenzhen'
        peer_service_host = 'gridflux-beta-shanghai'
        data_path = 'shenzhen<->shanghai'
        windows_in_data_path = $false
        default_source_endpoint = 'gridflux-beta-shenzhen'
        default_destination_endpoint = 'gridflux-beta-shanghai'
        transfer_client_and_peer_service_hosts_are_direction_invariant = $true
        source_and_destination_endpoints_swap_by_direction = $true
        endpoint_roles = [ordered]@{
            shenzhen = [ordered]@{ ssh_alias='gridflux-beta-shenzhen'; default_direction_role='source/local-destination'; reverse_direction_role='remote-destination' }
            shanghai = [ordered]@{ ssh_alias='gridflux-beta-shanghai'; default_direction_role='peer-service/destination'; reverse_direction_role='peer-service/source' }
        }
    }
    remote_roots = [ordered]@{ shenzhen='user-supplied isolated root only'; shanghai='user-supplied isolated root only' }
    forbidden_paths = @('/root/projects/GridFlux-Beta','/root/projects/CPSS(DCC)')
    input_hashes = $inputHashes
    datasets = $datasetSpec
    cpnetflux_baseline = [ordered]@{ file_io_backend='posix'; control_reuse='worker'; scheduler='off'; compression='off'; checksum='none'; resume=$false; lookahead_depth=$LookaheadDepth }
    timing = [ordered]@{ transfer='01-approved timer contract'; prepare_hash_log_cleanup='excluded'; independent_sha256='required'; tree_hash='required'; wall_not_sum_of_concurrent_stages=$true }
    cases = $cases
    case_count = $cases.Count
    status_semantics = [ordered]@{ pass='all required transfer/integrity/evidence gates pass'; failed='functional or protocol failure'; blocked='preflight/resource/tool/environment gate'; skipped='not run by explicit selection'; unknown='interrupted or incomplete state; never auto-rerun or treat as pass' }
    evidence = [ordered]@{ per_case='state.json, command.jsonl, stdout/stderr, source/destination hash, wire accounting'; run_level='manifest.json, summary.json, failure-list.json, disk-pid-cleanup.json, sha256-manifest.txt'; persist_before_cleanup=$true }
}
$manifestPath = Join-Path $OutputRoot 'case-plan.json'
$manifest | ConvertTo-Json -Depth 12 | Set-Content -Encoding UTF8 -LiteralPath $manifestPath
$readme = @"
MANUAL-CROSS-DOMAIN-EXPERIMENT-PACKAGE-01
readiness: READY_PENDING_DYNAMIC_QA
mode: $Mode
execute_requested: $([bool]$Execute)
case_count: $($cases.Count)
plan: $manifestPath

This invocation only generated a plan. No SSH, remote directory, payload, build, or transfer was started.
Manual launcher: Windows. Experiment controller and transfer client: Shenzhen. Peer service: Shanghai. Measurement data path: Linux endpoint to Linux endpoint (Shenzhen <-> Shanghai); Windows is not in the data path.
"@
$readme | Set-Content -Encoding UTF8 -LiteralPath (Join-Path $OutputRoot 'README.txt')
Write-Output "READY_PENDING_DYNAMIC_QA plan written: $manifestPath"
Write-Output "Cases: $($cases.Count); SSH not invoked; payload not generated."
