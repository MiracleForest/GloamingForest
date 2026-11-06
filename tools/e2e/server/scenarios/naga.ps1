# U20 (naga): the Naga and the Naga courtyard on BDS. The selftests naga.* prove: the attributes and
# the difficulty health boost (120 / 200 / 250), the 12 segments following the head as linked part
# actors and popping one by one after damage, the movement pattern with a seeded random (CIRCLE ->
# INTIMIDATE -> CHARGE -> CIRCLE, doDaze 60..99 ticks, more than 15 damage while dazed forces the
# circle), the home invulnerability (46 / 7 box, explosions) and the 2/3 segment damage, the loot
# table (6..11 scales + the trophy), the boss spawner (LONG_RANGE 50, home radius 40), a whole fight
# (the death chest under the home with the scales and the trophy, the player_killed_entity and
# hurt_boss triggers of progress_naga), the courtyard landmark generator (forced start) and the
# golden volume hash of a seeded courtyard at a fixed site (rule R19, compared in every world).
# Also: the death waits for the last segment (NagaSegment#tick resets deathTime: the chest comes
# 124 ticks after the last pop), damage through a direct entity outside the home is refused, the
# shield cases of doHurtTarget (daze; the 200-tick cooldown that keeps the shield down),
# EntityUtil#canDestroyBlock (containers, canEntityDestroy, vanishing locks) and a Naga that comes
# back without its state, and a player inside a segment is never pushed (no motion packet), only hit.
Wait-LogLine 'GloamingForest world ready' 120 | Out-Null
Wait-LogLine 'naga: registered the Naga, its spawner and boss lifecycle' 30 | Out-Null

$mark = Get-LogMark
$r = Invoke-SelfTest 'naga.*'
if ($r.Fail -gt 0) { throw "$($r.Fail) naga selftest(s) failed" }
if ($r.Pass -lt 16) { throw "expected at least 16 naga selftests to pass, got $($r.Pass)" }
$states = Get-ServerLog -Since $mark | Select-String -Pattern 'naga\.state_machine: states: (.*)' | Select-Object -Last 1
if ($states) { Write-Step "movement pattern: $($states.Matches[0].Groups[1].Value)" }
$hash = Get-ServerLog -Since $mark | Select-String -Pattern 'courtyard volume hash (\S+)' | Select-Object -Last 1
if ($hash) { Write-Step "courtyard volume hash: $($hash.Matches[0].Groups[1].Value)" }
