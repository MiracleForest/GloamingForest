# Travellers' Gear movement (U12): the server half of the twelve movement modifiers, with simulated
# players (src/feature/tgmove/TgMoveTests.cpp): modifier activity and broken gear, the double jump
# (Jump Boost II from the wings, slimy boost, safe fall +2, once per airtime, restored on landing),
# TF's anti-abuse validator (2 free, snap back, disconnect at 5; refused companion packets), the
# sidestep (1.6 push, 40 tick cooldown, ready sound, not crouching), the glide (fall distance v^2/2g,
# off without the modifier, the vanilla Slow Falling fallback), step up (+0.5, off crouching), slimy
# soles (cancelled landing, sqrt(0.5) bounce, sneaking lands), unrestrained (slowdown immunity),
# straight ahead (x1.4 forward only), the wing state (SIDESTEP lock 11 ticks, synced to companions)
# and aquatic agility's oxygen bonus. The client scenario measures the client half.
Wait-LogLine 'GloamingForest world ready' 120 | Out-Null
Wait-LogLine 'tgmove: travellers movement modifiers installed' 30 | Out-Null

$r = Invoke-SelfTest 'tgmove.*' -TimeoutSeconds 300
if ($r.Fail -gt 0) { throw "$($r.Fail) tgmove selftest(s) failed" }
if ($r.Pass -lt 13) { throw "expected 13 tgmove selftests to pass, got $($r.Pass)" }
