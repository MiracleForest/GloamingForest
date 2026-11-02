# F8 (entity): the boss bar screenshots need the HUD. A run that hid it (F1 persists as
# options.txt `gfx_hidehud:1`) would leave the shared game instance without a HUD, and a hidden HUD
# never asks for the bars' colours: show it again (the game's default), as the harness does for
# the camera perspective.
$data = Get-GameDataDir
foreach ($o in Get-ChildItem -Recurse -Path $data -Filter 'options.txt' -ErrorAction SilentlyContinue) {
    $lines = Get-Content $o.FullName
    if ($lines -match '^gfx_hidehud:1') {
        $lines -replace '^gfx_hidehud:1', 'gfx_hidehud:0' | Set-Content $o.FullName
        Write-Step "showing the HUD again (gfx_hidehud:0) in $($o.FullName)"
    }
}
