[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string] $StateFile,

    [switch] $AutoRemove
)

$ErrorActionPreference = "Stop"

if (-not (Test-Path -Path $StateFile)) {
    throw "State file not found: $StateFile"
}

$records = Get-Content -Path $StateFile -Raw | ConvertFrom-Json
if (-not $records) {
    throw "No state records found in $StateFile"
}

foreach ($record in @($records)) {
    $name = [string]$record.Name
    try {
        if ($record.StartMode -eq "Auto") {
            Set-Service -Name $name -StartupType Automatic -ErrorAction SilentlyContinue
        } elseif ($record.StartMode -eq "Manual") {
            Set-Service -Name $name -StartupType Manual -ErrorAction SilentlyContinue
        } elseif ($record.StartMode -eq "Disabled") {
            Set-Service -Name $name -StartupType Disabled -ErrorAction SilentlyContinue
        } else {
            Set-Service -Name $name -StartupType Automatic -ErrorAction SilentlyContinue
        }

        if ($record.State -eq "Running") {
            Write-Host "[*] Starting $name"
            Start-Service -Name $name -ErrorAction SilentlyContinue
        } else {
            Write-Host "[*] Keeping $name in stopped state"
        }
    } catch {
        Write-Warning "Could not restore $name : $_"
    }
}

if ($AutoRemove) {
    try {
        Remove-Item -Path $StateFile -Force
    } catch { }
}

Write-Host "[+] Restore complete from $StateFile"
