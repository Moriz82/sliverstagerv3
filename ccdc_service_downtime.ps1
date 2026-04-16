[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string[]] $Services,

    [string] $StateFile = ".\\ccdc_services_state.json",

    [switch] $DryRun,

    [int] $AutoRestoreMinutes = 0
)

$ErrorActionPreference = "Stop"

function Capture-ServiceState {
    param([string]$Name)
    $svc = Get-Service -Name $Name
    $cfg = Get-CimInstance -ClassName Win32_Service -Filter "Name='$Name'"

    [PSCustomObject]@{
        Name              = $cfg.Name
        DisplayName       = $cfg.DisplayName
        State             = $svc.Status
        StartMode         = $cfg.StartMode
        StartName         = $cfg.StartName
        PathName          = $cfg.PathName
        AutoStart         = $cfg.StartMode
        SnapshotTimestamp = (Get-Date).ToString("s")
    }
}

$records = @()
$state = @{}

Write-Host "[*] Capturing service states"
foreach ($svcName in $Services) {
    try {
        $record = Capture-ServiceState -Name $svcName
        $records += $record
    } catch {
        Write-Warning "Cannot read service '$svcName' ($_ )"
    }
}

if (-not $records) {
    throw "No services captured; aborting downtime workflow."
}

$records | ConvertTo-Json -Depth 4 | Set-Content -Path $StateFile -Encoding UTF8
Write-Host "[+] Captured $($records.Count) service records to $StateFile"

if ($DryRun) {
    Write-Host "[dry-run] No changes applied"
    $records | Format-Table Name,State,StartMode,StartName
    return
}

foreach ($record in $records) {
    $name = $record.Name
    try {
        if ($record.State -eq "Running") {
            Write-Host "[*] Stopping $name"
            Stop-Service -Name $name -Force -ErrorAction SilentlyContinue
        }
        Write-Host "[*] Setting startup type for $name -> Disabled"
        Set-Service -Name $name -StartupType Disabled -ErrorAction SilentlyContinue
    } catch {
        Write-Warning "Failed to modify $name : $_"
    }
}

if ($AutoRestoreMinutes -gt 0) {
    $restoreScript = Join-Path $PSScriptRoot "ccdc_service_restore.ps1"
    if (-not (Test-Path -Path $restoreScript)) {
        Write-Warning "Auto-restore requested but ccdc_service_restore.ps1 is missing: $restoreScript"
    } else {
        $runAt = (Get-Date).AddMinutes($AutoRestoreMinutes)
        $taskName = "CCDC-ServiceRestore-$((Get-Date).ToString('yyyyMMddHHmmss'))"
        Write-Host "[*] Scheduling auto-restore at $runAt"
        $action = New-ScheduledTaskAction -Execute "$PSHOME\\powershell.exe" -Argument "-NoProfile -NonInteractive -ExecutionPolicy Bypass -File `"$restoreScript`" -StateFile `"$StateFile`""
        $trigger = New-ScheduledTaskTrigger -Once -At $runAt
        Register-ScheduledTask -TaskName $taskName -Action $action -Trigger $trigger -RunLevel Highest -Force | Out-Null
    }
}

Write-Host "[+] Service downtime applied. Use ccdc_service_restore.ps1 to restore from $StateFile."
