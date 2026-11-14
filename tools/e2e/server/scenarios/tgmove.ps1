# Travellers' Gear movement (U12): the server half of the twelve movement modifiers, with simulated
# players (src/feature/tgmove/TgMoveTests.cpp): modifier activity and broken gear, the double jump
# (Jump Boost II from the wings, slimy boost, safe fall +2, once per airtime, restored on landing),
# upstream's anti-abuse validator (2 free, snap back, disconnect at 5; refused companion packets), the
# sidestep (1.6 push, 40 tick cooldown, ready sound, not crouching), the glide (fall distance v^2/2g,
# off without the modifier, the vanilla Slow Falling fallback), step up (+0.5, off crouching), slimy
# soles (cancelled landing, sqrt(0.5) bounce, sneaking lands), unrestrained (slowdown immunity),
# straight ahead (x1.4 with the forward key only), the wing state (SIDESTEP lock 11 ticks, synced to
# companions), aquatic agility's oxygen bonus and x5 submerged mining, Jump Boost II over a weaker
# potion (which comes back), everything put back when a modifier ends or the state is dropped, the
# safe fall before slimy soles, the creative fly guard of the vanilla double jump, mobs wearing the
# gear (glide, high jump, leash override cleanup) and last_damage_armor_time; unrestrained on ice,
# slime, soul sand and honey (slides, walks, the honey jump; tgmove.unrestrained_blocks), agile ranger's
# slowdown back when the state is dropped mid-draw, a companion packet before its move (retried, no
# strike) and the leash override's presets (tgmove.leash). The e2e helpers
# (tgmove.e2e.*) touch only players tagged gf_e2e.tgmove: none here. The client scenario measures
# the client half.
Wait-LogLine 'GloamingForest world ready' 120 | Out-Null
Wait-LogLine 'tgmove: travellers movement modifiers installed' 30 | Out-Null

$r = Invoke-SelfTest 'tgmove.*' -TimeoutSeconds 300
if ($r.Fail -gt 0) { throw "$($r.Fail) tgmove selftest(s) failed" }
if ($r.Pass -lt 25) { throw "expected 25 tgmove selftests to pass, got $($r.Pass)" }
