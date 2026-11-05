# Worldgen (F5), remote modes: runs on the BDS host before it boots (server/scenarios/
# client-host.prelaunch.ps1, in the server harness). Puts the custom probe biome of spike S2 into the
# host's deployed test packs, as the server scenario's worldgen.prelaunch.ps1 does: the behavior
# pack biome registers gf_test:worldgen_probe on the host, and the resource pack part (its client
# look) reaches a joining client with the host's world resource packs (PackDeployer). Then
# worldgen.custom_biome paints the probe 3000 blocks east of the player as well, and scenarios/
# worldgen.ps1 shows what a client without the probe in its own packs draws there.
. "$PSScriptRoot/../../server/scenarios/worldgen.probe.ps1"
Install-WorldgenProbe (Join-Path $script:SlotDir "$(Get-ModsDirName $script:BaseDir)\$script:ModName")
