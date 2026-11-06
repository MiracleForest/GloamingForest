# Beanstalk, cloud castle and giants (U29): the giants.* selftests.
#   giants.beans_trigger     magic beans on uberous soil place a grower, one item_used_on_block
#   giants.beanstalk_growth  the grower rumbles, grows a spiral of huge stalk with beanstalk leaves
#                            to max(y + 100, 175) and becomes a huge stalk
#   giants.uberous_soil      farmland under a crop + 15 bone meals, dirt under stone, bone meal spreads
#   giants.giant_blocks      a placed cell is 64 tiled blocks; breaking one removes all with one drop
#   giants.giant_pickaxe     destroy speed x 64; a full cobblestone cell -> one giant cobblestone,
#                            a mixed cell -> its own drops
#   giants.cloud_rain / cloud_snow / cloud_fall  rain fills a cauldron (isRainingAt), snow layers,
#                            10 % fall damage
#   giants.giant_mobs        gear, attributes, ant damage, player-only loot
#   giants.cloud_castle_hash the castle's first piece; with U27's pieces the golden volume hash
#   giants.client_showcase   the client scenario's showcase (no real player here: nothing to show)
Wait-LogLine 'GloamingForest world ready' 120 | Out-Null
$r = Invoke-SelfTest 'giants.*' -TimeoutSeconds 900
if ($r.Fail -gt 0) { throw "$($r.Fail) giants selftest(s) failed: $($r.Failed -join ', ')" }
if ($r.Pass -lt 11) { throw "only $($r.Pass) giants selftests ran" }
