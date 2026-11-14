# U21 (lich): one-block stamps the tower's room markers place through the template kit, so a
# spawner gets its spawn data from SpawnerProcessor.compile (upstream's configureBaseSpawner: one mob with
# the custom spawn rules block light 0..7, sky light 0..15, and the marker's spawn range).
$GNS = 'gf:lich_tower/'

$t = New-Gft -Id "${GNS}stamp/spawner" -Size 1, 1, 1 -Out 'lich/root/stamp/spawner'
Set-GftBlock $t 0 0 0 'minecraft:spawner'
Save-Gft $t

$t = New-Gft -Id "${GNS}stamp/sinister_spawner" -Size 1, 1, 1 -Out 'lich/root/stamp/sinister_spawner'
Set-GftBlock $t 0 0 0 'gloamingforest:sinister_spawner'
Save-Gft $t
