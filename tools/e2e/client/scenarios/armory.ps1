# U7 (armory) on the client: every upstream armour set worn, every tool and material in the hotbar.
#
# Eight armour stands in a row in front of the player wear the eight sets (naga scale tunic and
# leggings; ironwood; fiery; steeleaf; knightmetal; phantom helmet and chestplate; yeti;
# arctic): out/armory/sets-<mode>.png
# (compare with upstream's models/armor textures and GloamingArmorModel / YetiArmorModel / PhantomArmorModel /
# ArcticArmorModel / FieryArmorModel). Then the hotbar and inventory with the tools and materials
# (icons, the arctic icons' dye layer): out/armory/hotbar-<mode>.png, out/armory/inventory-<mode>.png.
# The player itself wears the yeti set in third person: out/armory/player-<mode>.png.
# Local mode also runs the armory.* selftests on the integrated server. A vanilla client sees the same
# attachables (the resource pack); its tooltips are the server's lore.
$remote = $script:Mode -ne 'local'
$hostLine = if ($remote) { '\[host\].*' } else { '' }
Wait-LogLine "${hostLine}GloamingForest world ready" 120 | Out-Null
Wait-HudReady | Out-Null

Invoke-Teleport '/tp @s 40.5 -60 36.5 0 10'
Invoke-Chat '/time set 1000'
Invoke-Chat '/fill 36 -61 34 52 -61 46 stone'
Invoke-Chat '/fill 36 -60 34 52 -55 46 air'
Invoke-Chat '/kill @e[type=armor_stand]'

$sets = @(
    @{ x = 36; head = 'air'; chest = 'gloamingforest:naga_chestplate'; legs = 'gloamingforest:naga_leggings'; feet = 'air' },
    @{ x = 38; head = 'gloamingforest:ironwood_helmet'; chest = 'gloamingforest:ironwood_chestplate'; legs = 'gloamingforest:ironwood_leggings'; feet = 'gloamingforest:ironwood_boots' },
    @{ x = 40; head = 'gloamingforest:fiery_helmet'; chest = 'gloamingforest:fiery_chestplate'; legs = 'gloamingforest:fiery_leggings'; feet = 'gloamingforest:fiery_boots' },
    @{ x = 42; head = 'gloamingforest:steeleaf_helmet'; chest = 'gloamingforest:steeleaf_chestplate'; legs = 'gloamingforest:steeleaf_leggings'; feet = 'gloamingforest:steeleaf_boots' },
    @{ x = 44; head = 'gloamingforest:knightmetal_helmet'; chest = 'gloamingforest:knightmetal_chestplate'; legs = 'gloamingforest:knightmetal_leggings'; feet = 'gloamingforest:knightmetal_boots' },
    @{ x = 46; head = 'gloamingforest:phantom_helmet'; chest = 'gloamingforest:phantom_chestplate'; legs = 'air'; feet = 'air' },
    @{ x = 48; head = 'gloamingforest:yeti_helmet'; chest = 'gloamingforest:yeti_chestplate'; legs = 'gloamingforest:yeti_leggings'; feet = 'gloamingforest:yeti_boots' },
    @{ x = 50; head = 'gloamingforest:arctic_helmet'; chest = 'gloamingforest:arctic_chestplate'; legs = 'gloamingforest:arctic_leggings'; feet = 'gloamingforest:arctic_boots' }
)
foreach ($s in $sets) {
    $x = $s.x
    Invoke-Chat "/summon armor_stand $($x).5 -60 42.5 180"
    Start-Sleep -Milliseconds 300
    $sel = "@e[type=armor_stand,x=$($x).5,y=-60,z=42.5,r=0.6]"
    foreach ($slot in @(@('slot.armor.head', $s.head), @('slot.armor.chest', $s.chest), @('slot.armor.legs', $s.legs), @('slot.armor.feet', $s.feet))) {
        if ($slot[1] -ne 'air') { Invoke-Chat "/replaceitem entity $sel $($slot[0]) 0 $($slot[1])" }
    }
}
Invoke-Teleport '/tp @s 43.5 -60 36.5 0 10'
Start-Sleep -Seconds 2
Save-Screenshot "sets-$($script:Mode)" | Out-Null

# tools and materials in the hotbar / inventory
Invoke-Chat '/clear @s'
$hotbar = @('ironwood_sword', 'steeleaf_pickaxe', 'fiery_sword', 'fiery_pickaxe', 'knightmetal_axe', 'gold_minotaur_axe', 'diamond_minotaur_axe', 'mazebreaker_pickaxe', 'arctic_helmet')
for ($i = 0; $i -lt $hotbar.Count; $i++) { Invoke-Chat "/replaceitem entity @s slot.hotbar $i gloamingforest:$($hotbar[$i])" }
$inventory = @('naga_scale', 'raw_ironwood', 'ironwood_ingot', 'steeleaf_ingot', 'fiery_blood', 'fiery_tears', 'fiery_ingot', 'armor_shard',
               'armor_shard_cluster', 'knightmetal_ingot', 'knightmetal_ring', 'arctic_fur', 'alpha_yeti_fur', 'ironwood_shovel', 'ironwood_pickaxe',
               'ironwood_axe', 'ironwood_hoe', 'steeleaf_sword', 'steeleaf_shovel', 'steeleaf_axe', 'steeleaf_hoe', 'knightmetal_sword', 'knightmetal_pickaxe',
               'arctic_chestplate', 'arctic_leggings', 'arctic_boots', 'yeti_helmet')
for ($i = 0; $i -lt $inventory.Count; $i++) { Invoke-Chat "/replaceitem entity @s slot.inventory $i gloamingforest:$($inventory[$i])" }
Start-Sleep -Seconds 1
Save-Screenshot "hotbar-$($script:Mode)" | Out-Null
Open-Inventory
Save-Screenshot "inventory-$($script:Mode)" | Out-Null
Press-Key ESC
Start-Sleep -Milliseconds 500

# the player in the yeti set, third person (front)
foreach ($slot in @(@('slot.armor.head', 'yeti_helmet'), @('slot.armor.chest', 'yeti_chestplate'), @('slot.armor.legs', 'yeti_leggings'), @('slot.armor.feet', 'yeti_boots'))) {
    Invoke-Chat "/replaceitem entity @s $($slot[0]) 0 gloamingforest:$($slot[1])"
}
Invoke-Chat '/camera @s set minecraft:third_person_front'
Start-Sleep -Seconds 3
Save-Screenshot "player-$($script:Mode)" | Out-Null
Invoke-Chat '/camera @s clear'

if (-not $remote) {
    $r = Invoke-SelfTest 'armory.*'
    if ($r.Fail -gt 0) { throw "$($r.Fail) armory selftest(s) failed on the integrated server" }
}
Invoke-Chat '/kill @e[type=armor_stand]'
