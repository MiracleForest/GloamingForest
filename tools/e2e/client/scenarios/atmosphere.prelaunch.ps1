# U15 (atmosphere): the first launch of the run uses the Classic look (graphics_mode 1); atmosphere.ps1
# switches to Vibrant Visuals for its second half and atmosphere.restore.ps1 puts the instance's
# setting back (F12's option helpers in ui.graphics.ps1). The render distance is pinned to 12 chunks
# (Java's default range; the aurora's extent and the fog planes follow it), put back after the run.
. (Join-Path $PSScriptRoot 'ui.graphics.ps1')
if ($script:Mode -ne 'remote-vanilla') { Set-UiGraphicsMode 1 }
Set-RunOption 'gfx_viewdistance' '192' | Out-Null
