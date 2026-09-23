#!/usr/bin/env python3
"""Anchor fake-player testbed.

Spawns N simulated Anchor clients that connect to a server, join a room, load
into Hyrule Field, and behave like players: they run, walk, jump, slash, roll,
swap held items and gear, and play item-use animations -- all driven by real
OoT animation data.

Self-contained: Python 3.8+ standard library only, no dependencies.

The Anchor protocol is plain TCP carrying NUL-delimited ('\\0') JSON objects.
Every packet has a "type" and a "clientId". The server assigns each connection
a clientId and echoes the full roster back in ALL_CLIENT_STATE.

To exercise the version-gated packet handlers on the real client, a fake player
must report the *exact* 7-char commit hash the local build was compiled with.
That value lives in soh/src/boot/build.c (generated at cmake configure time),
which is what the running game actually reports -- not necessarily git HEAD.
"""

import argparse
import json
import math
import os
import random
import re
import signal
import socket
import sys
import threading
import time

try:
    from anchor_anims import (AnimationLibrary, AnimationError, ANIM_FPS,
                              frame_to_joint_table)
    import anchor_items
except ImportError as e:  # pragma: no cover
    sys.stderr.write(f"missing sibling module: {e}\n")
    raise

# --- Game constants (mirrored from the SoH headers) ---
SCENE_HYRULE_FIELD = 0x51           # scene_table.h
ENTR_HYRULE_FIELD_SPAWN = 0xCD      # ENTR_HYRULE_FIELD_PAST_BRIDGE_SPAWN
LINK_AGE_ADULT = 0
LINK_AGE_CHILD = 1

PLAYER_STATE1_SHIELDING = 1 << 22

# Packet type strings (Anchor.h)
HANDSHAKE = "HANDSHAKE"
ALL_CLIENT_STATE = "ALL_CLIENT_STATE"
UPDATE_CLIENT_STATE = "UPDATE_CLIENT_STATE"
PLAYER_UPDATE = "PLAYER_UPDATE"
SERVER_MESSAGE = "SERVER_MESSAGE"

# The open field outside the drawbridge to Hyrule Market.
# ENTR_HYRULE_FIELD_PAST_BRIDGE_SPAWN (0xCD) is spawn index 0 of spot00_scene,
# read from the scene's SetStartPositionList command: pos (160, 0, 1415),
# rotY -3641. That yaw faces *away* from the bridge, and OoT's forward vector
# is (sin(yaw), cos(yaw)) -- see Actor_UpdateVelocityXZGravity -- so the open
# field lies along (-0.342, +0.940) from the spawn.
#
# Put the spawn on the near edge of the roam circle and push the circle out
# along that heading, so the bots use the field in front of you when you step
# off the bridge instead of the bridge itself.
MARKET_BRIDGE_SPAWN = (160.0, 0.0, 1415.0)
MARKET_BRIDGE_SPAWN_YAW = -3641  # binang

FIELD_RADIUS = 400.0
_spawn_rad = MARKET_BRIDGE_SPAWN_YAW / 0x10000 * 2.0 * math.pi
FIELD_CENTER = (
    MARKET_BRIDGE_SPAWN[0] + math.sin(_spawn_rad) * FIELD_RADIUS,
    MARKET_BRIDGE_SPAWN[1],
    MARKET_BRIDGE_SPAWN[2] + math.cos(_spawn_rad) * FIELD_RADIUS,
)

BINANG = 0x10000  # s16 angle wrap

# --- Animation pools --------------------------------------------------------
# Sword swings. Bots chain one or two of these for a combo.
SLASH_ANIMS = [
    "link_fighter_normal_kiru",
    "link_fighter_Lnormal_kiru",
    "link_fighter_Lside_kiru",
    "link_fighter_Rside_kiru",
    "link_fighter_LLside_kiru",
    "link_fighter_LRside_kiru",
    "link_fighter_pierce_kiru",
    "link_fighter_Lpierce_kiru",
    "link_fighter_upper_pierce_kiru",
]
SPIN_ANIMS = ["link_fighter_rolling_kiru", "link_fighter_Wrolling_kiru"]
JUMP_ATTACK_ANIMS = ["link_fighter_jump_kiru"]
IDLE_VARIANT_ANIMS = [
    "link_normal_wait_typeA_20f",
    "link_normal_wait_typeB_20f",
    "link_normal_wait_typeC_20f",
    "link_waitF_typeD_20f",
]
TURN_ANIMS = ["link_normal_45_turn", "link_normal_45_turn_free"]
ROLL_ANIMS = ["link_normal_landing_roll"]
SHIELD_ANIMS = ["link_fighter_defense_wait"]
# Jump: airborne pose, then the landing recovery.
JUMP_SEQUENCE = ["link_normal_jump", "link_normal_landing"]
JUMP_HEIGHT = 70.0

# Behaviour states
ST_IDLE = "idle"
ST_WALK = "walk"
ST_RUN = "run"
ST_ACTION = "action"   # generic one-shot animation sequence
ST_JUMP = "jump"       # one-shot sequence plus a vertical arc

_print_lock = threading.Lock()


def log(tag, msg):
    with _print_lock:
        print(f"[{time.strftime('%H:%M:%S')}] {tag}: {msg}", flush=True)


def read_commit_hash(shipwright_root):
    """Parse gGitCommitHash from the local build's build.c."""
    build_c = os.path.join(shipwright_root, "soh", "src", "boot", "build.c")
    if not os.path.isfile(build_c):
        raise FileNotFoundError(
            f"could not find build.c at {build_c}; pass --commit to set the "
            f"version manually or --root to point at your Shipwright checkout")
    with open(build_c, "r", encoding="utf-8", errors="replace") as f:
        text = f.read()
    m = re.search(r'gGitCommitHash\[\]\s*=\s*"([^"]*)"', text)
    if not m:
        raise ValueError(f"gGitCommitHash not found in {build_c}")
    return m.group(1)


def find_o2r(shipwright_root, explicit=None):
    """Locate a ROM-extracted .o2r containing player animations."""
    candidates = []
    if explicit:
        candidates.append(explicit)
    else:
        for rel in ("build/soh/oot.o2r", "build/soh/oot-mq.o2r", "oot.o2r"):
            candidates.append(os.path.join(shipwright_root, rel))
    for path in candidates:
        if os.path.isfile(path):
            try:
                lib = AnimationLibrary(path)
            except AnimationError:
                continue
            if lib.has("link_normal_wait"):
                return lib
            # soh.o2r holds only custom assets; keep looking.
    tried = "\n  ".join(candidates)
    raise AnimationError(
        "no .o2r with player animations found. Tried:\n  " + tried +
        "\nPass --o2r with your ROM-extracted archive (oot.o2r).")


def yaw_to_binang(dx, dz):
    """OoT yaw: atan2(x, z) mapped to a signed 16-bit binary angle."""
    rad = math.atan2(dx, dz)
    val = int(rad / (2 * math.pi) * BINANG) & 0xFFFF
    return val - 0x10000 if val >= 0x8000 else val


def wrap_binang(v):
    v &= 0xFFFF
    return v - 0x10000 if v >= 0x8000 else v


def approach_angle(current, target, max_step):
    """Turn `current` toward `target` by at most `max_step` binang."""
    diff = wrap_binang(target - current)
    if abs(diff) <= max_step:
        return wrap_binang(target)
    return wrap_binang(current + max_step * (1 if diff > 0 else -1))


class AnimPlayer:
    """Plays one animation, or a sequence of them, at the game's 20 fps."""

    def __init__(self, library):
        self.library = library
        self.queue = []          # list of Animation
        self.index = 0
        self.cursor = 0.0        # frames into the current animation
        self.loop = True
        self.base_transl = None
        self.name = None
        self._missing = set()

    def _resolve(self, name):
        try:
            return self.library.load(name)
        except AnimationError:
            if name not in self._missing:
                self._missing.add(name)
                log("anim", f"unavailable: {name}")
            return None

    def play(self, name, loop=True):
        self.play_sequence([name], loop=loop)

    def play_sequence(self, names, loop=False):
        anims = [a for a in (self._resolve(n) for n in names) if a is not None]
        if not anims:
            return
        self.queue = anims
        self.index = 0
        self.cursor = 0.0
        self.loop = loop
        self.name = anims[0].name
        # Pin horizontal root motion to frame 0 so looping locomotion doesn't
        # slide the model off the actor origin (mirrors z_player.c).
        self.base_transl = anims[0].frames[0][0:3]

    @property
    def current(self):
        if not self.queue:
            return None
        return self.queue[min(self.index, len(self.queue) - 1)]

    def advance(self, dt):
        cur = self.current
        if cur is None:
            return
        self.cursor += dt * ANIM_FPS
        while self.cursor >= cur.frame_count:
            if self.index < len(self.queue) - 1:
                self.cursor -= cur.frame_count
                self.index += 1
                cur = self.queue[self.index]
                self.name = cur.name
            elif self.loop:
                self.cursor -= cur.frame_count
            else:
                self.cursor = cur.frame_count - 1
                break

    def finished(self):
        if not self.queue or self.loop:
            return False
        return (self.index >= len(self.queue) - 1
                and self.cursor >= self.current.frame_count - 1)

    def segment_progress(self):
        """0..1 progress through the current animation in the sequence."""
        cur = self.current
        if cur is None or cur.frame_count <= 1:
            return 1.0
        return min(self.cursor / (cur.frame_count - 1), 1.0)

    def joint_table(self):
        cur = self.current
        if cur is None:
            return [0] * 72
        frame = cur.frame_at(int(self.cursor), loop=self.loop)
        return frame_to_joint_table(frame, base_transl=self.base_transl)


class FakePlayer(threading.Thread):
    def __init__(self, index, args, library, stop_event):
        super().__init__(daemon=True, name=f"fake-{index}")
        self.index = index
        self.args = args
        self.stop_event = stop_event
        self.sock = None
        self.rx_buffer = b""
        self.client_id = 0
        self.roster = {}

        self.name = f"{args.name_prefix}{index}"
        self.color = self._pick_color(index)
        self.child = child_for_index(index, args.age)
        self.link_age = LINK_AGE_CHILD if self.child else LINK_AGE_ADULT

        self.items = anchor_items.resolve(args.items, child=self.child)
        self.item = random.choice(self.items)

        tunics, shields, boots = anchor_items.gear_options(
            child=self.child, any_gear=args.any_gear)
        self._tunics, self._shields, self._boots = tunics, shields, boots
        self.tunic = random.choice(tunics)
        self.shield = random.choice(shields)
        self.boots = random.choice(boots)

        self.anim = AnimPlayer(library)
        self.state = ST_IDLE
        self.state_until = 0.0
        self.next_item_swap = 0.0
        self.next_gear_swap = 0.0
        self.shielding = False
        self.ground_y = args.center[1]
        self.jump_from = 0.0

        cx, cy, cz = args.center
        angle = random.uniform(0, 2 * math.pi)
        r = random.uniform(0, args.radius)
        self.pos = [cx + math.cos(angle) * r, cy, cz + math.sin(angle) * r]
        self.yaw = random.randint(-0x8000, 0x7FFF)
        self.target = None
        self._new_target()

    @staticmethod
    def _pick_color(index):
        palette = [
            (255, 100, 100), (100, 255, 100), (100, 160, 255),
            (255, 220, 100), (220, 100, 255), (100, 255, 230),
            (255, 150, 60), (180, 180, 180),
        ]
        return palette[index % len(palette)]

    def _new_target(self):
        cx, cy, cz = self.args.center
        angle = random.uniform(0, 2 * math.pi)
        r = random.uniform(0, self.args.radius)
        self.target = [cx + math.cos(angle) * r, cy, cz + math.sin(angle) * r]

    # --- networking ---
    def _send(self, payload):
        payload["clientId"] = self.client_id
        self.sock.sendall(json.dumps(payload).encode("utf-8") + b"\x00")

    def _drain_incoming(self):
        try:
            chunk = self.sock.recv(8192)
        except socket.timeout:
            return True
        except OSError:
            return False
        if not chunk:
            return False
        self.rx_buffer += chunk
        while b"\x00" in self.rx_buffer:
            raw, self.rx_buffer = self.rx_buffer.split(b"\x00", 1)
            if not raw:
                continue
            try:
                payload = json.loads(raw.decode("utf-8", errors="replace"))
            except json.JSONDecodeError:
                continue
            self._handle(payload)
        return True

    def _handle(self, payload):
        ptype = payload.get("type")
        if ptype == ALL_CLIENT_STATE:
            roster = {}
            for client in payload.get("state", []):
                cid = client.get("clientId", 0)
                roster[cid] = client
                if client.get("self") and self.client_id != cid:
                    self.client_id = cid
                    log(self.name, f"assigned clientId {cid}")
            self.roster = roster
        elif ptype == SERVER_MESSAGE and self.index == 0:
            log(self.name, f"server message: {payload.get('message')}")

    # --- packet builders ---
    def _room_state(self):
        return {
            "ownerClientId": self.client_id,
            "pvpMode": self.args.pvp,
            "showLocationsMode": 1,
            "teleportMode": 1,
            "syncItemsAndFlags": 1 if self.args.sync else 0,
        }

    def _client_state(self):
        return {
            "name": self.name,
            "color": {"r": self.color[0], "g": self.color[1], "b": self.color[2]},
            "clientVersion": self.args.commit,
            "teamId": self.args.team,
            "online": True,
            "seed": 0,
            "isSaveLoaded": True,
            "isGameComplete": False,
            "sceneNum": SCENE_HYRULE_FIELD,
            "curRoomNum": 0,
            "entranceIndex": ENTR_HYRULE_FIELD_SPAWN,
        }

    def _send_handshake(self):
        self._send({
            "type": HANDSHAKE,
            "roomId": self.args.room,
            "roomState": self._room_state(),
            "clientState": self._client_state(),
        })

    def _send_client_state(self):
        self._send({"type": UPDATE_CLIENT_STATE, "state": self._client_state()})

    def _send_player_update(self):
        item = self.item
        model_group = anchor_items.model_group_for(item, self.child, self.shield[0])
        payload = {
            "type": PLAYER_UPDATE,
            "sceneNum": SCENE_HYRULE_FIELD,
            "entranceIndex": ENTR_HYRULE_FIELD_SPAWN,
            "linkAge": self.link_age,
            "posRot": {
                "pos": {"x": self.pos[0], "y": self.pos[1], "z": self.pos[2]},
                "rot": {"x": 0, "y": self.yaw, "z": 0},
            },
            "prevTransl": {"x": 0, "y": 0, "z": 0},
            # 0 so the receiver doesn't re-apply animation root motion on top
            # of the position we already drive.
            "movementFlags": 0,
            "jointTable": self.anim.joint_table(),
            "upperLimbRot": {"x": 0, "y": 0, "z": 0},
            "currentBoots": self.boots[0],
            "currentShield": self.shield[0],
            "currentTunic": self.tunic[0],
            "stateFlags1": PLAYER_STATE1_SHIELDING if self.shielding else 0,
            "stateFlags2": 0,
            "buttonItem0": item.button_item,
            "itemAction": item.item_action,
            "heldItemAction": item.item_action,
            "modelGroup": model_group,
            "invincibilityTimer": 0,
            "unk_862": 0,
            "unk_85C": 0.0,
            "actionVar1": 0,
            "quiet": True,
        }
        for cid, client in list(self.roster.items()):
            if cid == self.client_id or not client.get("online"):
                continue
            if client.get("sceneNum") != SCENE_HYRULE_FIELD:
                continue
            payload["targetClientId"] = cid
            self._send(payload)

    # --- behaviour ---
    def _swap_item(self, now):
        if len(self.items) > 1:
            self.item = random.choice([i for i in self.items if i is not self.item])
        self.next_item_swap = now + random.uniform(
            self.args.item_interval * 0.6, self.args.item_interval * 1.4)
        log(self.name, f"equipped {self.item.label}")

    def _swap_gear(self, now):
        self.tunic = random.choice(self._tunics)
        self.shield = random.choice(self._shields)
        self.boots = random.choice(self._boots)
        self.next_gear_swap = now + random.uniform(
            self.args.gear_interval * 0.6, self.args.gear_interval * 1.4)
        log(self.name, f"gear: {self.tunic[1]}, {self.shield[1]}, {self.boots[1]}")

    def _enter_state(self, state, now, sequence=None, loop=True, cap=4.0):
        self.state = state
        self.shielding = False
        if state == ST_RUN:
            self.anim.play(self.item.run_anim, loop=True)
            self.state_until = now + random.uniform(2.5, 6.0)
        elif state == ST_WALK:
            self.anim.play(self.item.walk_anim, loop=True)
            self.state_until = now + random.uniform(2.0, 4.5)
        elif state == ST_IDLE:
            self.anim.play(sequence or self.item.idle_anim, loop=True)
            self.state_until = now + random.uniform(1.5, 4.0)
        elif state == ST_JUMP:
            self.anim.play_sequence(JUMP_SEQUENCE, loop=False)
            self.jump_from = self.pos[1]
            self.state_until = now + cap
        else:  # ST_ACTION
            self.anim.play_sequence(sequence or ["link_normal_check"], loop=loop)
            self.state_until = now + cap

    def _pick_action_sequence(self):
        """Pick a random one-shot the bot can plausibly perform right now."""
        choices = []
        if self.item.melee:
            # A combo of one or two swings reads as a real attack.
            combo = random.sample(SLASH_ANIMS, random.choice([1, 1, 2]))
            choices += [(combo, 6), (random.choice(SPIN_ANIMS), 2),
                        (random.choice(JUMP_ATTACK_ANIMS), 2)]
        if self.item.use_anim:
            choices.append(([self.item.use_anim], 5))
        choices.append((random.choice(ROLL_ANIMS), 2))
        choices.append((random.choice(TURN_ANIMS), 2))
        if self.shield[0] != anchor_items.PLAYER_SHIELD_NONE:
            choices.append((random.choice(SHIELD_ANIMS), 2))
        population = [c for c, _ in choices]
        weights = [w for _, w in choices]
        pick = random.choices(population, weights=weights, k=1)[0]
        return pick if isinstance(pick, list) else [pick]

    def _pick_next_state(self, now):
        if self.args.behavior == "circle":
            self._enter_state(ST_RUN, now)
            self.state_until = now + 999.0
            return
        if self.args.behavior == "still":
            roll = random.random()
            if roll < 0.25:
                self._enter_state(ST_JUMP, now)
            elif roll < 0.75:
                self._enter_state(ST_ACTION, now,
                                  sequence=self._pick_action_sequence(),
                                  loop=False)
            else:
                self._enter_state(ST_IDLE, now,
                                  sequence=random.choice(IDLE_VARIANT_ANIMS))
            return

        roll = random.random()
        if roll < 0.34:
            self._enter_state(ST_RUN, now)
            self._new_target()
        elif roll < 0.52:
            self._enter_state(ST_WALK, now)
            self._new_target()
        elif roll < 0.74:
            seq = self._pick_action_sequence()
            self._enter_state(ST_ACTION, now, sequence=seq, loop=False)
            self.shielding = seq[0] in SHIELD_ANIMS
        elif roll < 0.86:
            self._enter_state(ST_JUMP, now)
        elif roll < 0.94:
            self._enter_state(ST_IDLE, now,
                              sequence=random.choice(IDLE_VARIANT_ANIMS))
        else:
            self._enter_state(ST_IDLE, now)

    def _step(self, dt, now):
        if now >= self.next_item_swap:
            self._swap_item(now)
            if self.state in (ST_IDLE, ST_WALK, ST_RUN):
                self._enter_state(self.state, now)
        if now >= self.next_gear_swap:
            self._swap_gear(now)

        if self.state in (ST_ACTION, ST_JUMP):
            if self.anim.finished() or now >= self.state_until:
                self.pos[1] = self.ground_y
                self._pick_next_state(now)
        elif now >= self.state_until:
            self._pick_next_state(now)

        self.anim.advance(dt)

        # Vertical arc while the jump's airborne segment plays.
        if self.state == ST_JUMP:
            if self.anim.index == 0:
                t = self.anim.segment_progress()
                self.pos[1] = self.jump_from + JUMP_HEIGHT * 4.0 * t * (1.0 - t)
            else:
                self.pos[1] = self.ground_y

        if self.args.behavior == "circle":
            cx, _cy, cz = self.args.center
            omega = self.args.speed / max(self.args.radius, 1.0)
            t = now * omega + self.index * (2 * math.pi / max(self.args.players, 1))
            self.pos[0] = cx + math.cos(t) * self.args.radius
            self.pos[2] = cz + math.sin(t) * self.args.radius
            self.yaw = yaw_to_binang(-math.sin(t), math.cos(t))
            return

        if self.state not in (ST_WALK, ST_RUN):
            return

        speed = self.args.speed if self.state == ST_RUN else self.args.speed * 0.4
        dx = self.target[0] - self.pos[0]
        dz = self.target[2] - self.pos[2]
        dist = math.hypot(dx, dz)
        if dist < 40.0:
            self._new_target()
            return
        step = min(speed * dt, dist)
        self.pos[0] += dx / dist * step
        self.pos[2] += dz / dist * step
        self.yaw = approach_angle(self.yaw, yaw_to_binang(dx, dz),
                                  int(0x10000 * 1.5 * dt))

    # --- main loop ---
    def run(self):
        while not self.stop_event.is_set():
            try:
                self._connect_and_serve()
            except OSError as e:
                if self.stop_event.is_set():
                    break
                log(self.name, f"connection error: {e}; retrying in 2s")
                self.stop_event.wait(2.0)

    def _connect_and_serve(self):
        self.sock = socket.create_connection(
            (self.args.host, self.args.port), timeout=5.0)
        self.sock.settimeout(0.02)
        self.rx_buffer = b""
        self.client_id = 0
        log(self.name, f"connected to {self.args.host}:{self.args.port} "
                       f"as {'child' if self.child else 'adult'} Link")

        self._send_handshake()
        self._send_client_state()

        now = time.monotonic()
        self.next_item_swap = now + random.uniform(1.0, self.args.item_interval)
        self.next_gear_swap = now + random.uniform(1.0, self.args.gear_interval)
        self._enter_state(ST_IDLE, now)

        tick = 1.0 / self.args.rate
        next_update = now
        last = now
        try:
            while not self.stop_event.is_set():
                if not self._drain_incoming():
                    log(self.name, "server closed connection")
                    break
                now = time.monotonic()
                if now >= next_update:
                    dt = min(now - last, 0.25)
                    last = now
                    self._step(dt, now)
                    if self.client_id != 0:
                        self._send_player_update()
                    next_update += tick
                    if next_update < now:
                        next_update = now + tick
        finally:
            try:
                self.sock.close()
            except OSError:
                pass


def child_for_index(index, age):
    """Assign an age to bot `index`.

    'mixed' alternates so the split is exactly 50/50 for an even player count
    (and one extra adult when odd), rather than coin-flipping each bot -- with
    a handful of bots random draws often land lopsided.
    """
    if age == "child":
        return True
    if age == "adult":
        return False
    return index % 2 == 1


def parse_center(text):
    parts = text.split(",")
    if len(parts) != 3:
        raise argparse.ArgumentTypeError("center must be x,y,z")
    return [float(p) for p in parts]


def parse_items(text):
    return [t.strip() for t in text.split(",") if t.strip()]


def build_arg_parser():
    default_root = os.path.abspath(
        os.path.join(os.path.dirname(__file__), "..", ".."))
    p = argparse.ArgumentParser(
        description="Spawn fake Anchor players for multiplayer playtesting.",
        formatter_class=argparse.ArgumentDefaultsHelpFormatter)
    p.add_argument("-n", "--players", type=int, default=3,
                   help="number of fake players to simulate")
    p.add_argument("--host", default="anchor.hm64.org", help="Anchor server host")
    p.add_argument("--port", type=int, default=43383, help="Anchor server port")
    p.add_argument("--room", default="soh-testbed",
                   help="room id to join (match your game's Anchor RoomId)")
    p.add_argument("--team", default="default", help="team id")
    p.add_argument("--commit", default=None,
                   help="7-char client version; default reads it from build.c")
    p.add_argument("--root", default=default_root,
                   help="Shipwright checkout root (for build.c and oot.o2r)")
    p.add_argument("--o2r", default=None,
                   help="path to ROM-extracted .o2r holding player animations")
    p.add_argument("--name-prefix", default="Bot",
                   help="fake player display name prefix")
    p.add_argument("--behavior", choices=["wander", "circle", "still"],
                   default="wander", help="movement pattern")
    p.add_argument("--items", type=parse_items, default=["all"],
                   help="comma-separated item keys to cycle, or 'all'")
    p.add_argument("--item-interval", type=float, default=12.0,
                   help="average seconds between item swaps")
    p.add_argument("--gear-interval", type=float, default=18.0,
                   help="average seconds between tunic/shield/boots swaps")
    p.add_argument("--any-gear", action="store_true",
                   help="allow age-inappropriate gear (Deku shield on adult, etc)")
    p.add_argument("--list-items", action="store_true",
                   help="print the item catalogue and exit")
    p.add_argument("--center", type=parse_center, default=list(FIELD_CENTER),
                   help="spawn/roam center as x,y,z")
    p.add_argument("--radius", type=float, default=FIELD_RADIUS,
                   help="roam radius around center")
    p.add_argument("--speed", type=float, default=200.0,
                   help="run speed (units/sec); walking is 40%% of this")
    p.add_argument("--rate", type=float, default=20.0,
                   help="PLAYER_UPDATE send rate (Hz)")
    p.add_argument("--age", choices=["mixed", "adult", "child"], default="mixed",
                   help="link age per bot; 'mixed' alternates 50/50")
    p.add_argument("--child", action="store_true",
                   help="shorthand for --age child")
    p.add_argument("--pvp", type=int, default=0, choices=[0, 1, 2],
                   help="pvpMode sent in handshake (room owner wins if room exists)")
    p.add_argument("--sync", action="store_true",
                   help="request syncItemsAndFlags in handshake room state")
    return p


def main():
    args = build_arg_parser().parse_args()
    if args.child:
        args.age = "child"

    if args.list_items:
        for item in anchor_items.CATALOGUE:
            tags = []
            if item.adult_only:
                tags.append("adult only")
            if item.melee:
                tags.append("melee")
            suffix = f" ({', '.join(tags)})" if tags else ""
            print(f"  {item.key:<16} {item.label}{suffix}")
        return 0

    if args.commit is None:
        try:
            args.commit = read_commit_hash(args.root)
        except (FileNotFoundError, ValueError) as e:
            log("error", str(e))
            return 1

    try:
        library = find_o2r(args.root, args.o2r)
    except AnimationError as e:
        log("error", str(e))
        return 1

    ages = {child_for_index(i, args.age) for i in range(max(args.players, 1))}
    pools = {}
    try:
        for child in sorted(ages):
            pools[child] = anchor_items.resolve(args.items, child=child)
    except KeyError as e:
        log("error", f"unknown item key {e}; use --list-items")
        return 1

    n_child = sum(1 for i in range(args.players) if child_for_index(i, args.age))
    log("init", f"client version (commit): {args.commit}")
    log("init", f"animations: {library.path}")
    log("init", f"server {args.host}:{args.port}  room '{args.room}'  "
                f"players {args.players}  behavior {args.behavior}")
    log("init", f"ages: {args.players - n_child} adult / {n_child} child "
                f"({args.age})")
    for child, pool in pools.items():
        label = "child" if child else "adult"
        log("init", f"{label} item pool ({len(pool)}): "
                    f"{', '.join(i.key for i in pool[:8])}"
                    f"{' ...' if len(pool) > 8 else ''}")

    stop_event = threading.Event()

    def handle_sig(signum, frame):
        log("shutdown", "stopping...")
        stop_event.set()
    signal.signal(signal.SIGINT, handle_sig)
    signal.signal(signal.SIGTERM, handle_sig)

    players = [FakePlayer(i, args, library, stop_event)
               for i in range(args.players)]
    for pl in players:
        pl.start()
        time.sleep(0.15)

    try:
        while not stop_event.is_set():
            stop_event.wait(1.0)
    finally:
        stop_event.set()
        for pl in players:
            pl.join(timeout=3.0)
    log("shutdown", "done")
    return 0


if __name__ == "__main__":
    sys.exit(main())
