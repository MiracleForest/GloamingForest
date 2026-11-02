# F4 (dimension): the Twilight Forest dimension (spike S1), the portal and the teleporter.
Wait-LogLine 'GloamingForest world ready' 120 | Out-Null
$reg = Get-ServerLog | Select-String -Pattern 'dimension: twilightforest:twilight_forest registered as dimension' | Select-Object -First 1
if (-not $reg) { throw 'the Twilight Forest dimension was not registered (no "registered as dimension" line)' }
Write-Step $reg.Line

$r = Invoke-SelfTest 'dimension.*'
if ($r.Fail -gt 0) { throw "$($r.Fail) dimension selftest(s) failed" }
if ($r.Pass -lt 25) { throw "expected at least 25 dimension selftests to pass, got $($r.Pass)" }

# the trips logged their lines (one per user-visible action): a player's and an item's
foreach ($p in 'dimension: portal lit at', 'enters the portal at', 'travelled from minecraft:overworld to twilightforest:twilight_forest',
    'travelled from twilightforest:twilight_forest to minecraft:overworld', 'minecraft:item travelled from minecraft:overworld', 'built a portal at',
    'function twilightforest:tp_twilight .*moves', "brightness ramp now has ambient light", 'teleported from minecraft:overworld to twilightforest:twilight_forest') {
    if (-not (Get-ServerLog | Select-String -Pattern $p -Quiet)) { throw "no '$p' line" }
}

# the cache and the world survive a restart: the dimension keeps its id
$id = [regex]::Match($reg.Line, 'registered as dimension (\d+)').Groups[1].Value
Restart-Server
$reg2 = Get-ServerLog | Select-String -Pattern 'dimension: twilightforest:twilight_forest registered as dimension (\d+)' | Select-Object -Last 1
if (-not $reg2) { throw 'the Twilight Forest dimension was not registered after the restart' }
if ($reg2.Matches[0].Groups[1].Value -ne $id) { throw "the dimension id changed across the restart ($id -> $($reg2.Matches[0].Groups[1].Value))" }
$r = Invoke-SelfTest 'dimension.enter'
if ($r.Pass -ne 1) { throw 'dimension.enter failed after the restart' }
