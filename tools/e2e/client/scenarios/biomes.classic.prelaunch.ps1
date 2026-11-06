# Biomes (U1) in the Classic look, before the game starts: graphics_mode 1 (Fancy) for this run with
# F12's option helpers (ui.graphics.ps1: the instance's own value is kept in the e2e home);
# biomes.classic.restore.ps1 puts it back. In the Classic look the game tints water with the biome's
# water colour (as Java does); Vibrant Visuals shades it physically.
. (Join-Path $PSScriptRoot 'ui.graphics.ps1')
Set-UiGraphicsMode 1
