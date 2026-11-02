# Families (F11) in the Classic look, before the game starts: the families prelaunch (the fixture
# families on the deployed mod), then graphics_mode 1 (Fancy) for this run with F12's option helpers
# (ui.graphics.ps1: the instance's own value is kept in the e2e home); families.classic.restore.ps1
# puts it back.
. (Join-Path $PSScriptRoot 'families.prelaunch.ps1')
Set-UiGraphicsMode 1
