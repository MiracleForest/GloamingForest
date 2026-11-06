# U19 (questcamps): the Questing Ram, the quest grove, camps and TF's template features on BDS. The
# selftests questcamps.* prove: the quest data (16 wools, the reward and loot tables), every one of
# the 57 original templates exists and keeps its golden volume hash (rule R19), the template
# features' 15 x 15 limit and the graves' shared footprint, the ram taking each colour once (by hand)
# and eating dropped wool, the reward (crumble horn and bundle, complete_quest_ram, the conquered
# grove), the grove piece (TFQuest1: box, droppers, the ram with its home), a seeded camp's jigsaw
# layout (start, tents, paths, decos without overlaps, golden layout hash) and the druid hut (markers,
# basement), wells, graveyard (8 graves, wraiths) and ruins placed through their feature code.
Wait-LogLine 'GloamingForest world ready' 120 | Out-Null
Wait-LogLine 'questcamps: registered the quest grove, camps and template features' 30 | Out-Null

$mark = Get-LogMark
$r = Invoke-SelfTest 'questcamps.*'
if ($r.Fail -gt 0) { throw "$($r.Fail) questcamps selftest(s) failed" }
if ($r.Pass -lt 12) { throw "expected at least 12 questcamps selftests to pass, got $($r.Pass)" }
$hash = Get-ServerLog -Since $mark | Select-String -Pattern 'camp layout hash (\S+)' | Select-Object -Last 1
if ($hash) { Write-Step "camp layout hash: $($hash.Matches[0].Groups[1].Value)" }