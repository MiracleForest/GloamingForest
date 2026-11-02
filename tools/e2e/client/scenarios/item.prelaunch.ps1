# F9 (item), spike S6: the local world's integrated server gets the probe content of
# tools/e2e/server/scenarios/item.s6probe.ps1: the flag that registers the fixture trim material
# iron_darker at load (hotbar 6 of item.spike_client shows a material that was in the registry before
# the player joined) and the probe BP disc of item.spike_record_sounds. It lives only in this run's
# mod folder (the next deployment replaces it). The BDS host of the remote modes has none: there
# item.spike_client appends iron_darker with the late materials.
if ($script:Mode -eq 'local') {
    . (Join-Path $PSScriptRoot '../../server/scenarios/item.s6probe.ps1')
    Write-S6Probe (Join-Path $script:ModsDir $script:ModName) 'item (client, local)'
}
