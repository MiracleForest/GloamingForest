# Before the BDS host of the client harness's remote modes boots (tools/e2e/client/run.ps1 -Mode
# remote-mod | remote-vanilla, scenario client-host): runs the client scenario's host hook
# tools/e2e/client/scenarios/<scenario>.host-prelaunch.ps1 when it exists (the unit's file, rule R3).
#
# The hook is dot-sourced here, in the server harness, after the slot was reset, the mod deployed
# and server.properties written, so it sees the server API (tools/e2e/server/lib.ps1): e.g. a probe
# pack goes into the deployed mod, Join-Path $script:SlotDir "$(Get-ModsDirName $script:SlotDir)\$script:ModName".
# $HostScenario and $HostMode name the client run (remote-mod or remote-vanilla). Nothing needs
# undoing: the next run of the slot syncs it again.
$HostScenario = $env:GF_E2E_HOST_SCENARIO
$HostMode = $env:GF_E2E_HOST_MODE
if ($HostScenario) {
    if ($HostScenario -notmatch '^[A-Za-z0-9_.-]+$') { throw "client-host: bad client scenario name '$HostScenario'" }
    $hostPrelaunch = Join-Path $PSScriptRoot "../../client/scenarios/$HostScenario.host-prelaunch.ps1"
    if (Test-Path $hostPrelaunch) {
        Write-Step "host prelaunch of client scenario '$HostScenario' ($HostMode)"
        . $hostPrelaunch
    }
}
