# U7 (armory) on the client: every TF armour set worn, every tool and material in the hotbar.
#
# Eight armour stands in a row in front of the player wear the eight sets (naga scale tunic and
# leggings; ironwood; fiery; steeleaf; knightmetal; phantom helmet and chestplate; yeti;
# arctic): out/armory/sets-<mode>.png
# (compare with TF's models/armor textures and TFArmorModel / YetiArmorModel / PhantomArmorModel /
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
    @{ x = 36; head = 'air'; chest = 'twilightforest:naga_chestplate'; legs = 'twilightforest:naga_leggings'; feet = 'air' },
    @{ x = 38; head = 'twilightforest:ironwood_helmet'; chest = 'twilightforest:ironwood_chestplate'; legs = 'twilightforest:ironwood_leggings'; feet = 'twilightforest:ironwood_boots' },
    @{ x = 40; head = 'twilightforest:fiery_helmet'; chest = 'twilightforest:fiery_chestplate'; legs = 'twilightforest:fiery_leggings'; feet = 'twilightforest:fiery_boots' },
    @{ x = 42; head = 'twilightforest:steeleaf_helmet'; chest = 'twilightforest:steeleaf_chestplate'; legs = 'twilightforest:steeleaf_leggings'; feet = 'twilightforest:steeleaf_boots' },
    @{ x = 44; head = 'twilightforest:knightmetal_helmet'; chest = 'twilightforest:knightmetal_chestplate'; legs = 'twilightforest:knightmetal_leggings'; feet = 'twilightforest:knightmetal_boots' },
    @{ x = 46; head = 'twilightforest:phantom_helmet'; chest = 'twilightforest:phantom_chestplate'; legs = 'air'; feet = 'air' },
    @{ x = 48; head = 'twilightforest:yeti_helmet'; chest = 'twilightforest:yeti_chestplate'; legs = 'twilightforest:yeti_leggings'; feet = 'twilightforest:yeti_boots' },
    @{ x = 50; head = 'twilightforest:arctic_helmet'; chest = 'twilightforest:arctic_chestplate'; legs = 'twilightforest:arctic_leggings'; feet = 'twilightforest:arctic_boots' }
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
for ($i = 0; $i -lt $hotbar.Count; $i++) { Invoke-Chat "/replaceitem entity @s slot.hotbar $i twilightforest:$($hotbar[$i])" }
$inventory = @('naga_scale', 'raw_ironwood', 'ironwood_ingot', 'steeleaf_ingot', 'fiery_blood', 'fiery_tears', 'fiery_ingot', 'armor_shard',
               'armor_shard_cluster', 'knightmetal_ingot', 'knightmetal_ring', 'arctic_fur', 'alpha_yeti_fur', 'ironwood_shovel', 'ironwood_pickaxe',
               'ironwood_axe', 'ironwood_hoe', 'steeleaf_sword', 'steeleaf_shovel', 'steeleaf_axe', 'steeleaf_hoe', 'knightmetal_sword', 'knightmetal_pickaxe',
               'arctic_chestplate', 'arctic_leggings', 'arctic_boots', 'yeti_helmet')
for ($i = 0; $i -lt $inventory.Count; $i++) { Invoke-Chat "/replaceitem entity @s slot.inventory $i twilightforest:$($inventory[$i])" }
Start-Sleep -Seconds 1
Save-Screenshot "hotbar-$($script:Mode)" | Out-Null
Open-Inventory
Save-Screenshot "inventory-$($script:Mode)" | Out-Null
Press-Key ESC
Start-Sleep -Milliseconds 500

# the player in the yeti set, third person (front)
foreach ($slot in @(@('slot.armor.head', 'yeti_helmet'), @('slot.armor.chest', 'yeti_chestplate'), @('slot.armor.legs', 'yeti_leggings'), @('slot.armor.feet', 'yeti_boots'))) {
    Invoke-Chat "/replaceitem entity @s $($slot[0]) 0 twilightforest:$($slot[1])"
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
