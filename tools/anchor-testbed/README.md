# Anchor Testbed

A self-contained tool that spawns fake Anchor players to playtest / stress the
multiplayer layer without needing multiple real game instances. Useful for
reproducing crashes caused by patched global data (memory corruption) that only
show up with other players present.

- **No dependencies.** Python 3.8+ standard library only.
- **Not part of the build.** Lives under `tools/`, run it directly.

The bots run, walk, jump, slash, spin-attack, roll, turn and shield around
Hyrule Field using **real OoT animation data**, and randomly swap both held
items and gear (tunic / shield / boots). They default to the open field just
outside the drawbridge to Hyrule Market, and to a 50/50 mix of child and adult
Link.

## Files

| File | Role |
|---|---|
| `anchor_testbed.py` | CLI, networking, behaviour state machine |
| `anchor_anims.py` | Decodes player animations out of a `.o2r` archive |
| `anchor_items.py` | Item catalogue + gear tables (itemAction / modelGroup / buttonItem0 + anims) |

## How it works

Anchor is plain TCP carrying NUL-delimited (`\0`) JSON. Each fake player opens
its own connection, sends a `HANDSHAKE`, and the server assigns it a `clientId`
and broadcasts the roster via `ALL_CLIENT_STATE`. Each bot then loads into
Hyrule Field and sends `PLAYER_UPDATE` every frame to the other clients in the
scene — including your real game client, which renders them as dummy players.

### Version matching matters

The real client drops almost every packet from a client whose `clientVersion`
doesn't match its own (see `Anchor::OnIncomingJson`). Only `ALL_CLIENT_STATE`,
`UPDATE_CLIENT_STATE`, and `PLAYER_UPDATE` are exempt — so a version mismatch
still renders a moving dummy, but none of the item/flag/save handlers fire.

The version string is the 7-char commit hash compiled into your build via
`soh/src/boot/build.c`, which is **not** necessarily `git HEAD` (it's whatever
your last cmake configure captured). The tool reads that file by default so the
bots always match your running build. Override with `--commit`.

### Animations

Poses are read from a ROM-extracted `.o2r` (auto-detected at
`build/soh/oot.o2r`; override with `--o2r`). Note `soh.o2r` holds only SoH's
*custom* assets and won't work — you need the ROM-extracted archive.

Each `gPlayerAnim_*` header resolves to a blob in `misc/link_animetion`. Frames
are `6 * 22 + 2` = 134 bytes (67 `int16`), matching what
`AnimationContext_SetLoadFrame` memcpy's for Link's 22 limbs:

```
[0..2]   jointTable[0]      root TRANSLATION (not a rotation)
[3..65]  jointTable[1..21]  limb rotations, PLAYER_LIMB_ROOT..TORSO
[66]     jointTable[22].x   face/eye texture index
```

The root translation is hip-height (`y` ≈ 3377 when idle), so sending zeros
there sinks the model into the ground. The tool pins the root's `x`/`z` to
frame 0 and sends `movementFlags: 0`, so looping locomotion doesn't slide the
model off the actor origin or double-apply root motion on top of the position
the tool already drives — the same thing `z_player.c` does.

### Where they spawn

The default roam area is the open field outside the Market drawbridge.
`ENTR_HYRULE_FIELD_PAST_BRIDGE_SPAWN` (`0xCD`) is spawn index 0 of
`spot00_scene`, read out of the scene's `SetStartPositionList` command:
**(160, 0, 1415)** with `rotY -3641`.

That yaw is -20°, and OoT's forward vector is `(sin(yaw), cos(yaw))` (see
`Actor_UpdateVelocityXZGravity`), so Link faces `(-0.342, +0.940)` — *away*
from the bridge. The tool therefore treats the spawn as the **near edge** of
the roam circle and pushes the circle out along that heading:

```
center = spawn + forward * radius  ->  (23, 0, 1791), radius 400
```

So when you step off the bridge the bots are in the field ahead of you, never
behind you on the bridge itself. Override with `--center x,y,z` and `--radius`
to test elsewhere; nothing is scene-specific except `--center`,
`SCENE_HYRULE_FIELD` and the entrance constant at the top of
`anchor_testbed.py`.

### Ages

`--age` controls the child/adult split and defaults to `mixed`, which
alternates by bot index so the split is exactly 50/50 on an even player count
(one extra adult when odd). Coin-flipping each bot was avoided because with a
handful of bots it often lands lopsided.

Age is fixed per bot for the whole session. That matters: the receiving client
compares `linkAge` in `HandlePacket_PlayerUpdate` and sets `shouldRefreshActors`
on any change, which kills and respawns *every* dummy player. Bots that flipped
age mid-session would thrash that path continuously.

`--age adult` / `--age child` force a single age, and `--child` is shorthand
for the latter. Each bot's item pool and gear options are filtered by its own
age, so child bots never hold a Master Sword and never wear the Mirror Shield.

### Items and gear

`--list-items` prints the catalogue (25 entries). Each pairs `itemAction`,
`modelGroup` (from `sActionModelGroups`) and `buttonItem0` with idle / use /
run / walk animations, and flags melee weapons.

Gear (`currentTunic` / `currentShield` / `currentBoots`) is rerolled on its own
timer. Defaults stay age-appropriate — Deku shield is child-only, Mirror shield
adult-only, and child keeps Kokiri boots — and `--any-gear` unlocks every
combination. The child + Hylian shield case overrides `modelGroup` to
`PLAYER_MODELGROUP_CHILD_HYLIAN_SHIELD`, matching `Player_ActionToModelGroup`.

**Bots can hold anything.** `DummyPlayer_Update` applies these fields straight
onto the dummy Player with no inventory check anywhere in that path, so a fake
player needs no save data to brandish a Biggoron's Sword. Adult-only items are
filtered automatically for child bots.

### What the bots do

Each bot runs a weighted state machine: run / walk to a new point, or play a
one-shot action, or idle on one of four idle variants.

| Action | Notes |
|---|---|
| Slash | 9 swing animations, sometimes chained into a 2-hit combo |
| Spin attack | `rolling_kiru` / `Wrolling_kiru` |
| Jump attack | `fighter_jump_kiru` |
| Jump | `normal_jump` → `normal_landing`, with a real vertical arc on `pos.y` |
| Roll | `normal_landing_roll` |
| Turn | 45° turn in place |
| Shield | sets `PLAYER_STATE1_SHIELDING` so the shield goes to the hand |
| Item use | the held item's own use animation |

Melee-only actions (slash, spin, jump attack) are offered only while a melee
weapon is held; shielding only while a shield is equipped.

## Usage

```sh
# 3 bots roaming Hyrule Field, cycling every item, version auto-detected
python3 tools/anchor-testbed/anchor_testbed.py --room my-test-room

# 8 bots against a locally-hosted server
python3 tools/anchor-testbed/anchor_testbed.py \
    -n 8 --host 127.0.0.1 --port 43383 --room my-test-room

# melee only, swapping fast -- lots of slashing
python3 tools/anchor-testbed/anchor_testbed.py --room my-test-room \
    --items kokiri_sword,master_sword,biggoron_sword,deku_stick,hammer \
    --item-interval 4 --gear-interval 6

# stand in place and just cycle actions (slashes, jumps, item use)
python3 tools/anchor-testbed/anchor_testbed.py --room my-test-room \
    --behavior still --any-gear

# all child Link bots (adult-only items dropped automatically)
python3 tools/anchor-testbed/anchor_testbed.py --room my-test-room --age child

python3 tools/anchor-testbed/anchor_testbed.py --list-items
```

In your game: enable Anchor, set the **Room ID** to the same `--room` value,
set the same host/port, and load a save into Hyrule Field. Walk out of the
Market across the drawbridge and the bots will be right there. Ctrl-C stops the
tool and disconnects them.

## Key options

| Option | Default | Notes |
|---|---|---|
| `-n, --players` | 3 | number of fake players |
| `--host` / `--port` | `anchor.hm64.org` / `43383` | server |
| `--room` | `soh-testbed` | must match your game's Anchor Room ID |
| `--commit` | (from `build.c`) | client version; must match the build under test |
| `--root` | repo root | where to find `build.c` and `oot.o2r` |
| `--o2r` | `build/soh/oot.o2r` | ROM-extracted archive with animations |
| `--behavior` | `wander` | `wander`, `circle`, or `still` |
| `--items` | `all` | comma-separated keys, see `--list-items` |
| `--item-interval` | 12 | average seconds between item swaps |
| `--gear-interval` | 18 | average seconds between tunic/shield/boots swaps |
| `--any-gear` | off | allow age-inappropriate gear combinations |
| `--center` / `--radius` | `23,0,1791` / 400 | roam area; default puts the bridge spawn on its near edge |
| `--speed` / `--rate` | 200 / 20 | run speed (walk is 40%) / update rate (Hz) |
| `--age` | `mixed` | `mixed` (50/50), `adult` or `child` |
| `--child` | off | shorthand for `--age child` |
| `--pvp` | 0 | pvpMode sent in handshake (room owner wins if room exists) |
| `--sync` | off | request item/flag sync in handshake room state |

## Notes & limitations

- Movement is driven by the tool and the animation supplies the pose, so the
  two aren't foot-locked — bots can appear to skate slightly if `--speed` is
  far from the animation's natural pace. The default 200 is tuned for
  `link_normal_run`.
- Bots send only `PLAYER_UPDATE` — they are a presence/rendering load, not a
  packet fuzzer. The handlers that patch globals (`UPDATE_TEAM_STATE`,
  `SET_FLAG`, `UPDATE_DUNGEON_ITEMS`, ...) are not exercised.
- There is no ground collision: bot height is whatever `--center`'s y says, so
  the default area works because the field there sits at `y = 0`. Point
  `--center` at sloped terrain and the bots will float or sink.
- The room owner's settings win once a room exists, so `--pvp`/`--sync` only
  take effect if a bot creates the room before your client joins.
