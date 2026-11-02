# F9 (item), spike S6: before the server boots, the probe content of item.s6probe.ps1 (the flag that
# makes this run register the fixture trim material iron_darker at load, so
# item.spike_trim_materials finds it in the level's registry before anyone joined; a probe BP with a
# data-driven disc for item.spike_record_sounds). It lives only in this run's slot (the next run's
# deployment replaces the mod folder); no other world has it.
. (Join-Path $PSScriptRoot 'item.s6probe.ps1')
Write-S6Probe (Join-Path $script:SlotDir "$(Get-ModsDirName $script:SlotDir)\$script:ModName") 'item (server)'
