# F12 (ui): the first launch of the run uses the Classic look (graphics_mode 1); ui.ps1 switches
# to Vibrant Visuals for its second half and ui.restore.ps1 puts the instance's setting back.
. (Join-Path $PSScriptRoot 'ui.graphics.ps1')
if ($script:Mode -ne 'remote-vanilla') { Set-UiGraphicsMode 1 }
