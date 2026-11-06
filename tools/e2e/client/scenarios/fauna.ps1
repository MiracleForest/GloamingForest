# Passive and ambient fauna (U16), client: screenshots of what U16 adds, for the acceptance run.
#   mob-<name>          each of the ten mobs, summoned in front of the camera and animating (the
#                       quadrupeds walking, the squirrel's tail, the mosquito swirl, the kobold's arms)
#   bighorn-colours     sixteen bighorns, one per fleece colour (the fur layer tinted by colour)
#   bighorn-sheared     a sheared bighorn next to a woolly one
#   tiny-bird-variants  blue, brown, gold and red tiny birds
#   dwarf-rabbit-variants  brown, dutch and white dwarf rabbits
#   bird-flying-1..3    a tiny bird and a raven taking off (wing flaps, feet raised in flight)
#   kobold-jaw          a kobold jumping (the jaw open)
#   babies              a boar, deer, bighorn, penguin and dwarf rabbit baby (the antlers and horns hidden)
#   raven-feather, spawn-eggs   the raven feather and the ten spawn eggs
# Out: out/fauna[-<mode>]/*.png - open each one and compare it with TF's textures and models.
if ($script:Mode -eq 'local') { Wait-LogLine 'GloamingForest world ready' 120 | Out-Null }
else { Wait-LogLine '\[host\].*GloamingForest world ready' 120 | Out-Null }

Invoke-Chat '/gamemode creative'
Invoke-Chat '/time set noon'
Invoke-Chat '/gamerule domobspawning false'
Invoke-Chat '/difficulty normal'

$x = 4600; $z = 4600; $y = -60
function Set-Camera([double]$cx, [double]$cy, [double]$cz, [string]$facing) {
    Invoke-Teleport "/tp @s $cx $cy $cz"
    Invoke-Chat "/setblock $([math]::Floor($cx)) $([math]::Floor($cy) - 1) $([math]::Floor($cz)) glass"
    Invoke-Teleport "/tp @s $cx $cy $cz facing $facing"
    Start-Sleep -Seconds 3
}
# the mobs' own spawn set-up (fleece colour, variant) runs on their first tick: colours and variants
# for the shots are set a moment later with the BP's events
function Set-MobEvent([string]$type, [double]$ex, [double]$ez, [string]$event) {
    Invoke-Chat "/event entity @e[type=twilightforest:$type,x=$ex,y=$y,z=$ez,r=0.7,c=1] $event"
}
function Clear-Pen { Invoke-Chat "/kill @e[type=!player,x=$x,y=$y,z=$z,r=30]" }

# a grass pen for the animals (ice for the penguin)
Invoke-Teleport "/tp @s $($x + 4) $($y + 4) $($z - 4)"
Start-Sleep -Seconds 4
Invoke-Chat "/fill $($x - 6) $($y - 1) $($z - 2) $($x + 14) $($y - 1) $($z + 12) grass_block"
Invoke-Chat "/fill $($x - 6) $($y) $($z - 2) $($x + 14) $($y + 4) $($z + 12) air"

# ---- one shot per mob --------------------------------------------------------------------------------
foreach ($m in @('boar', 'bighorn_sheep', 'deer', 'penguin', 'tiny_bird', 'raven', 'squirrel', 'dwarf_rabbit', 'kobold', 'mosquito_swarm')) {
    Clear-Pen
    if ($m -eq 'penguin') { Invoke-Chat "/fill $($x + 2) $($y - 1) $($z + 2) $($x + 6) $($y - 1) $($z + 6) ice" }
    Set-Camera ($x + 4.5) ($y + 1.5) ($z - 1.5) "$($x + 4.5) $($y + 0.5) $($z + 4.5)"
    Invoke-Chat "/summon twilightforest:$m $($x + 4.5) $($y) $($z + 4.5) facing @p"
    Start-Sleep -Seconds 2
    Save-Screenshot "mob-$m" | Out-Null
    if ($m -eq 'penguin') { Invoke-Chat "/fill $($x + 2) $($y - 1) $($z + 2) $($x + 6) $($y - 1) $($z + 6) grass_block" }
}

# ---- bighorn fleece colours ---------------------------------------------------------------------------------
Clear-Pen
Set-Camera ($x + 4.5) ($y + 6) ($z - 4) "$($x + 4.5) $($y) $($z + 5)"
foreach ($c in 0..15) {
    $cx = $x + ($c % 8) * 1.2; $cz = $z + 3 + [math]::Floor($c / 8) * 2.5
    Invoke-Chat "/summon twilightforest:bighorn_sheep $cx $y $cz"
}
Start-Sleep -Seconds 1
foreach ($c in 0..15) {
    $cx = $x + ($c % 8) * 1.2; $cz = $z + 3 + [math]::Floor($c / 8) * 2.5
    Set-MobEvent 'bighorn_sheep' $cx $cz "set_color_$c"
}
Start-Sleep -Seconds 2
Save-Screenshot 'bighorn-colours' | Out-Null
Clear-Pen
Invoke-Chat "/summon twilightforest:bighorn_sheep $($x + 3.5) $y $($z + 4.5)"
Invoke-Chat "/summon twilightforest:bighorn_sheep $($x + 5.5) $y $($z + 4.5)"
Start-Sleep -Seconds 1
Set-MobEvent 'bighorn_sheep' ($x + 3.5) ($z + 4.5) 'shear'
Set-Camera ($x + 4.5) ($y + 1.5) ($z - 1) "$($x + 4.5) $($y + 0.5) $($z + 4.5)"
Save-Screenshot 'bighorn-sheared' | Out-Null

# ---- variants ---------------------------------------------------------------------------------------------
Clear-Pen
foreach ($v in 0..3) { Invoke-Chat "/summon twilightforest:tiny_bird $($x + 2.5 + $v * 1.2) $y $($z + 3.5)" }
Start-Sleep -Seconds 1
foreach ($v in 0..3) { Set-MobEvent 'tiny_bird' ($x + 2.5 + $v * 1.2) ($z + 3.5) "set_variant_$v" }
Set-Camera ($x + 4.5) ($y + 1) ($z + 1) "$($x + 4.5) $($y) $($z + 3.5)"
Save-Screenshot 'tiny-bird-variants' | Out-Null
Clear-Pen
foreach ($v in 0..2) { Invoke-Chat "/summon twilightforest:dwarf_rabbit $($x + 3 + $v * 1.2) $y $($z + 3.5)" }
Start-Sleep -Seconds 1
foreach ($v in 0..2) { Set-MobEvent 'dwarf_rabbit' ($x + 3 + $v * 1.2) ($z + 3.5) "set_variant_$v" }
Set-Camera ($x + 4.5) ($y + 1) ($z + 1) "$($x + 4.5) $($y) $($z + 3.5)"
Save-Screenshot 'dwarf-rabbit-variants' | Out-Null

# ---- flight: a bird spooked by a hit takes off (wing flaps, feet raised) ---------------------------------------
Clear-Pen
Invoke-Chat "/summon twilightforest:tiny_bird $($x + 4) $y $($z + 4)"
Invoke-Chat "/summon twilightforest:raven $($x + 5) $y $($z + 4)"
Set-Camera ($x + 4.5) ($y + 2) ($z - 1) "$($x + 4.5) $($y + 1.5) $($z + 4)"
Invoke-Chat "/damage @e[type=twilightforest:tiny_bird,c=1] 1 entity_attack entity @e[type=twilightforest:raven,c=1]"
Invoke-Chat "/damage @e[type=twilightforest:raven,c=1] 1 entity_attack entity @e[type=twilightforest:tiny_bird,c=1]"
foreach ($k in 1..3) { Start-Sleep -Milliseconds 400; Save-Screenshot "bird-flying-$k" | Out-Null }

# ---- the kobold's jaw while jumping ----------------------------------------------------------------------------
Clear-Pen
Invoke-Chat "/summon twilightforest:kobold $($x + 4.5) $y $($z + 3.5) facing @p"
Set-Camera ($x + 4.5) ($y + 1.2) ($z + 1) "$($x + 4.5) $($y + 1) $($z + 3.5)"
Invoke-Chat "/effect @e[type=twilightforest:kobold] jump_boost 5 3 true"
Start-Sleep -Seconds 1
Save-Screenshot 'kobold-jaw' | Out-Null

# ---- babies -----------------------------------------------------------------------------------------------------
Clear-Pen
$i = 0
foreach ($m in @('boar', 'deer', 'bighorn_sheep', 'penguin', 'dwarf_rabbit')) {
    Invoke-Chat "/summon twilightforest:$m $($x + 1.5 + $i * 1.6) $y $($z + 3.5) minecraft:entity_born"
    $i++
}
Set-Camera ($x + 4.5) ($y + 1.5) ($z - 0.5) "$($x + 4.5) $($y + 0.3) $($z + 3.5)"
Save-Screenshot 'babies' | Out-Null

# ---- items ------------------------------------------------------------------------------------------------------
Invoke-Chat '/clear @s'
Invoke-Chat '/give @s twilightforest:raven_feather 1'
Start-Sleep -Seconds 1
Save-Screenshot 'raven-feather' | Out-Null
Invoke-Chat '/clear @s'
foreach ($m in @('boar', 'bighorn_sheep', 'deer', 'penguin', 'tiny_bird', 'raven', 'squirrel', 'dwarf_rabbit', 'kobold', 'mosquito_swarm')) {
    Invoke-Chat "/give @s twilightforest:$($m)_spawn_egg 1"
}
Start-Sleep -Seconds 1
Save-Screenshot 'spawn-eggs' | Out-Null
Clear-Pen
