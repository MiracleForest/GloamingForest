# U11 (tgear) before the game starts: Bedrock's own keyboard binding of Z (the mob effects screen,
# options.txt keyboard_type_0_key.mobEffects:90) takes the key upstream's zoom uses by default; while the
# effects screen is open the zoom key is not held in game. Unbind it for this run (the harness puts the
# instance's own value back, Restore-RunOptions); docs/units/tgear.md tells players to remap one of the
# two.
Set-RunOption 'keyboard_type_0_key.mobEffects' '0'
