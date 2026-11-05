# Building (U5), remote modes: runs on the BDS host before it boots (server/scenarios/
# client-host.prelaunch.ps1, in the server harness): the showcase functions in the host's deployed mod
# (building.showcase.ps1).
. (Join-Path $PSScriptRoot 'building.showcase.ps1')
Install-BuildingShowcase (Join-Path $script:SlotDir "$(Get-ModsDirName $script:BaseDir)\$script:ModName")
