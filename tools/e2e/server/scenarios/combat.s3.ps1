# Spike S3 (F10): what the game builds for data-driven block entity / container components
# (probe blocks from combat.s3.prelaunch.ps1), and whether a probe container keeps its items over a
# world reload. Evidence for docs/spikes/S3.md; the probes are not shipped.
# The world is created on the first boot; experiments forced through $env:GF_S3_EXPERIMENTS apply
# when level.dat is read, so the probes run after a first restart.
Wait-LogLine 'GloamingForest world ready' 120 | Out-Null
$r = Invoke-SelfTest 'combat.s3_probe'
Restart-Server
$r = Invoke-SelfTest 'combat.s3_probe'
$r = Invoke-SelfTest 'combat.s3_persist_write'
Restart-Server
$r = Invoke-SelfTest 'combat.s3_persist_read'
