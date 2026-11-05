# Building (U5): the showcase of the client scenario, written as a function into a deployed mod's
# behavior pack (local: the client's own mod, building.prelaunch.ps1; remote modes: the BDS host's,
# building.host-prelaunch.ps1). It lives only in that run's deployed copy (the next deployment
# replaces it): /function gfbuilding/showcase builds every block of the unit with a visual on the
# flat world, /function gfbuilding/clear takes it down again. Rows (the camera positions are in
# building.ps1):
#   z 20  the nine floor trophies (rotation 8: facing south, towards the camera)
#   z 40  the nine wall trophies on a stone wall (facing south)
#   z 60  the naga trophy in its 16 rotations (x 20 = rotation 0 .. x 35 = rotation 15)
#   z 80  the nine boss spawners; z 83 the five miniature structures (facing south)
#   z 100 nagastone (an L, a head, pillars, etched, spiral, twisted pillar), rope, iron ladders
#   z 120 castle bricks, rune bricks, mazestone, underbrick, towerwood, deadrock, storage blocks

function Get-BuildingShowcase {
    $ns = 'twilightforest'
    $bosses = 'naga', 'lich', 'hydra', 'ur_ghast', 'knight_phantom', 'snow_queen', 'minoshroom', 'alpha_yeti', 'quest_ram'
    $spawners = 'naga', 'lich', 'hydra', 'ur_ghast', 'knight_phantom', 'snow_queen', 'minoshroom', 'alpha_yeti', 'final_boss'
    $minis = 'twilight_portal', 'naga_courtyard', 'lich_tower', 'minotaur_labyrinth', 'dark_tower'
    $lines = [System.Collections.Generic.List[string]]::new()
    $lines.Add('fill 16 -61 16 44 -61 72 grass_block')
    $lines.Add('fill 16 -61 73 44 -61 128 grass_block')
    $lines.Add('fill 16 -60 16 44 -50 72 air')
    $lines.Add('fill 16 -60 73 44 -50 128 air')
    for ($i = 0; $i -lt 9; $i++) {
        $x = 20 + 2 * $i
        $lines.Add("setblock $x -60 20 ${ns}:$($bosses[$i])_trophy [`"${ns}:rotation`"=8]")
        $lines.Add("setblock $x -59 41 ${ns}:$($bosses[$i])_wall_trophy [`"${ns}:facing`"=`"south`"]")
        $lines.Add("setblock $x -60 80 ${ns}:$($spawners[$i])_boss_spawner")
    }
    $lines.Add('fill 19 -60 40 37 -58 40 stone_bricks')
    for ($r = 0; $r -lt 16; $r++) { $lines.Add("setblock $(20 + $r) -60 60 ${ns}:naga_trophy [`"${ns}:rotation`"=$r]") }
    for ($i = 0; $i -lt 5; $i++) { $lines.Add("setblock $(22 + 3 * $i) -60 83 ${ns}:$($minis[$i])_miniature_structure [`"${ns}:facing`"=`"south`"]") }
    # z 100: nagastone L (axis_y over north_up corner over a horizontal z piece), a head facing it
    $lines.Add("setblock 20 -60 100 ${ns}:nagastone [`"${ns}:variant`"=`"axis_z`"]")
    $lines.Add("setblock 20 -60 101 ${ns}:nagastone [`"${ns}:variant`"=`"north_up`"]")
    $lines.Add("setblock 20 -59 101 ${ns}:nagastone [`"${ns}:variant`"=`"axis_y`"]")
    $lines.Add("setblock 20 -58 101 ${ns}:nagastone_head [`"${ns}:facing`"=`"south`"]")
    $lines.Add("setblock 22 -60 101 ${ns}:nagastone_pillar")
    $lines.Add("setblock 22 -59 101 ${ns}:mossy_nagastone_pillar")
    $lines.Add("setblock 22 -58 101 ${ns}:cracked_nagastone_pillar")
    $lines.Add("setblock 23 -60 101 ${ns}:etched_nagastone [`"${ns}:facing`"=`"south`"]")
    $lines.Add("setblock 23 -59 101 ${ns}:mossy_etched_nagastone [`"${ns}:facing`"=`"south`"]")
    $lines.Add("setblock 23 -58 101 ${ns}:cracked_etched_nagastone [`"${ns}:facing`"=`"south`"]")
    $lines.Add("setblock 24 -60 101 ${ns}:nagastone_stairs_left [`"${ns}:facing`"=`"north`"]")
    $lines.Add("setblock 25 -60 101 ${ns}:nagastone_stairs_right [`"${ns}:facing`"=`"north`"]")
    $lines.Add("setblock 26 -60 101 ${ns}:spiral_bricks [`"${ns}:axis`"=`"z`",`"${ns}:diagonal`"=`"top_left`"]")
    $lines.Add("setblock 26 -59 101 ${ns}:spiral_bricks [`"${ns}:axis`"=`"z`",`"${ns}:diagonal`"=`"bottom_right`"]")
    $lines.Add("setblock 27 -60 101 ${ns}:twisted_stone")
    $lines.Add("setblock 28 -60 101 ${ns}:twisted_stone_pillar [`"${ns}:axis`"=`"y`",`"${ns}:up`"=true]")
    $lines.Add("setblock 28 -59 101 ${ns}:twisted_stone_pillar [`"${ns}:axis`"=`"y`",`"${ns}:down`"=true]")
    $lines.Add("setblock 29 -60 101 ${ns}:bold_stone_pillar")
    # rope hanging from a stone bar, a horizontal rope between two posts; iron ladders on a wall
    $lines.Add('fill 31 -56 101 35 -56 101 stone')
    $lines.Add("fill 32 -59 101 32 -57 101 ${ns}:rope")
    $lines.Add('setblock 34 -59 101 stone')
    $lines.Add('setblock 36 -59 101 stone')
    $lines.Add("setblock 35 -59 101 ${ns}:rope [`"${ns}:x`"=true,`"${ns}:y`"=false]")
    $lines.Add('fill 38 -60 102 41 -57 102 stone')
    $lines.Add("fill 38 -60 101 39 -57 101 ${ns}:iron_ladder [`"${ns}:facing`"=`"south`"]")
    $lines.Add("setblock 41 -60 101 ${ns}:iron_ladder [`"${ns}:facing`"=`"south`"]")
    # z 120: the plain building blocks, three high
    $row = @(
        @('castle_brick', 'worn_castle_brick', 'cracked_castle_brick'), @('mossy_castle_brick', 'castle_roof_tile', 'thick_castle_brick'),
        @('bold_castle_brick_tile', 'bold_castle_brick_pillar', 'encased_castle_brick_tile'), @('encased_castle_brick_pillar', 'castle_brick_stairs', 'mossy_castle_brick_stairs'),
        @('pink_castle_rune_brick', 'yellow_castle_rune_brick', 'blue_castle_rune_brick'), @('violet_castle_rune_brick', 'worn_castle_brick_stairs', 'cracked_castle_brick_stairs'),
        @('mazestone', 'mazestone_brick', 'cracked_mazestone'), @('mossy_mazestone', 'cut_mazestone', 'decorative_mazestone'),
        @('mazestone_mosaic', 'mazestone_border', 'underbrick'), @('mossy_underbrick', 'cracked_underbrick', 'underbrick_floor'),
        @('towerwood', 'encased_towerwood', 'cracked_towerwood'), @('mossy_towerwood', 'deadrock', 'cracked_deadrock'),
        @('weathered_deadrock', 'knightmetal_block', 'ironwood_block'), @('fiery_block', 'steeleaf_block', 'arctic_fur_block'),
        @('carminite_block', 'canopy_window', 'canopy_window_pane'))
    for ($i = 0; $i -lt $row.Count; $i++) {
        for ($y = 0; $y -lt 3; $y++) { $lines.Add("setblock $(20 + $i) $(-60 + $y) 120 ${ns}:$($row[$i][$y])") }
    }
    $lines.Add('say building showcase built')
    $lines
}

function Install-BuildingShowcase([string]$ModDir) {
    $dir = Join-Path $ModDir 'behavior_packs/GloamingForest_BP/functions/gfbuilding'
    New-Item -ItemType Directory -Force $dir | Out-Null
    Set-Content -Path (Join-Path $dir 'showcase.mcfunction') -Value ((Get-BuildingShowcase) -join "`n") -Encoding UTF8
    Set-Content -Path (Join-Path $dir 'clear.mcfunction') -Value "fill 16 -60 16 44 -50 72 air`nfill 16 -60 73 44 -50 128 air`nkill @e[type=item]`nsay building showcase cleared" -Encoding UTF8
}
