# F12 (ui): the first launch of the run uses the Classic look (graphics_mode 1) and shows the hand
# (gfx_hidehand 0: the held map step); ui.ps1 switches to Vibrant Visuals for its second half and
# ui.restore.ps1 puts the instance's settings back.
. (Join-Path $PSScriptRoot 'ui.graphics.ps1')
if ($script:Mode -ne 'remote-vanilla') {
    Set-UiGraphicsMode 1
    Set-UiOption 'gfx_hidehand' '0'
}
