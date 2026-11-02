# Worldgen (F5): before the world loads, put the custom probe biome (spike S2) into the deployed
# test behavior pack, so worldgen.custom_biome can write a custom biome id into a chunk.
. "$PSScriptRoot/worldgen.probe.ps1"
Install-WorldgenProbe (Join-Path $script:SlotDir "$(Get-ModsDirName $script:BaseDir)\$script:ModName")
