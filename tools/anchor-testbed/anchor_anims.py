"""Link animation loader for the Anchor testbed.

Reads real player animations out of a ROM-extracted .o2r archive so fake
players move with genuine OoT poses instead of synthetic ones.

Format, as produced by torch and consumed by SoH:

  Every resource file starts with a 0x40-byte header:
    0x00 int8   endianness (0 = little, which is what torch writes natively)
    0x01 int8   isCustom
    0x04 uint32 resource type
    0x08 uint32 version
    ... padding to 0x40

  A player animation *header* (objects/gameplay_keep/gPlayerAnim_*) then has:
    uint32 animType (1 = Link)
    int16  frameCount
    uint32 strLen + chars -> "__OTR__misc/link_animetion/gPlayerAnimData_XXXXXX"

  The referenced *data* blob (misc/link_animetion/gPlayerAnimData_*) has:
    uint32 numEntries
    int16  values[numEntries]

  The data is a flat int16 array split into frames. `AnimationContext_SetLoadFrame`
  memcpy's `sizeof(Vec3s) * limbCount + 2` bytes per frame, and Link's limbCount
  is PLAYER_LIMB_MAX (22), so the stride is 6*22+2 = 134 bytes = 67 int16:

    [0..2]   jointTable[0]  -> root TRANSLATION (not a rotation)
    [3..65]  jointTable[1..21] -> limb rotations, PLAYER_LIMB_ROOT..TORSO
    [66]     spills into jointTable[22].x (face/eye texture index)

  Note the root translation is hip-height (y is ~3377 in the idle pose), so
  sending zeros there sinks the model into the ground.
"""

import struct
import zipfile

RESOURCE_HEADER_SIZE = 0x40
PLAYER_LIMB_COUNT = 22                       # PLAYER_LIMB_MAX
FRAME_INT16S = (PLAYER_LIMB_COUNT * 3) + 1   # 67
ANIM_PREFIX = "objects/gameplay_keep/gPlayerAnim_"

# Animations are authored at the game's 20 fps animation rate.
ANIM_FPS = 20.0


class AnimationError(Exception):
    pass


class Animation:
    """A decoded player animation: a list of frames, each 67 int16 values."""

    __slots__ = ("name", "frames", "frame_count")

    def __init__(self, name, frames):
        self.name = name
        self.frames = frames
        self.frame_count = len(frames)

    def frame_at(self, index, loop=True):
        if self.frame_count == 0:
            raise AnimationError(f"{self.name} has no frames")
        if loop:
            return self.frames[index % self.frame_count]
        return self.frames[min(index, self.frame_count - 1)]

    def __repr__(self):
        return f"<Animation {self.name} frames={self.frame_count}>"


class AnimationLibrary:
    """Lazily decodes gPlayerAnim_* entries from an .o2r archive."""

    def __init__(self, o2r_path):
        try:
            self._zip = zipfile.ZipFile(o2r_path)
        except (OSError, zipfile.BadZipFile) as e:
            raise AnimationError(f"cannot open {o2r_path}: {e}") from e
        self._path = o2r_path
        self._names = set(self._zip.namelist())
        self._cache = {}

    @property
    def path(self):
        return self._path

    def has(self, anim_name):
        return (ANIM_PREFIX + anim_name) in self._names

    def available(self):
        return sorted(
            n[len(ANIM_PREFIX):] for n in self._names if n.startswith(ANIM_PREFIX))

    def load(self, anim_name):
        """Load gPlayerAnim_<anim_name>, following its pointer to the data blob."""
        if anim_name in self._cache:
            return self._cache[anim_name]

        header_path = ANIM_PREFIX + anim_name
        if header_path not in self._names:
            raise AnimationError(f"animation '{anim_name}' not in {self._path}")

        blob = self._zip.read(header_path)
        if len(blob) < RESOURCE_HEADER_SIZE + 10:
            raise AnimationError(f"'{anim_name}' header truncated")

        off = RESOURCE_HEADER_SIZE
        anim_type = struct.unpack_from("<I", blob, off)[0]
        off += 4
        frame_count = struct.unpack_from("<h", blob, off)[0]
        off += 2
        str_len = struct.unpack_from("<I", blob, off)[0]
        off += 4
        if off + str_len > len(blob):
            raise AnimationError(f"'{anim_name}' data path truncated")
        data_path = blob[off:off + str_len].decode("utf-8", errors="replace")
        data_path = data_path.replace("__OTR__", "")

        if anim_type != 1:
            raise AnimationError(
                f"'{anim_name}' is animType {anim_type}, expected 1 (Link)")

        frames = self._load_data(data_path, anim_name)
        if frame_count > 0:
            frames = frames[:frame_count]
        anim = Animation(anim_name, frames)
        self._cache[anim_name] = anim
        return anim

    def _load_data(self, data_path, anim_name):
        if data_path not in self._names:
            raise AnimationError(
                f"'{anim_name}' points at missing data '{data_path}'")
        blob = self._zip.read(data_path)
        if len(blob) < RESOURCE_HEADER_SIZE + 4:
            raise AnimationError(f"data '{data_path}' truncated")
        count = struct.unpack_from("<I", blob, RESOURCE_HEADER_SIZE)[0]
        payload_off = RESOURCE_HEADER_SIZE + 4
        available = (len(blob) - payload_off) // 2
        if count > available:
            count = available
        values = struct.unpack_from(f"<{count}h", blob, payload_off)

        total_frames = count // FRAME_INT16S
        if total_frames == 0:
            raise AnimationError(f"data '{data_path}' has no complete frames")
        return [list(values[i * FRAME_INT16S:(i + 1) * FRAME_INT16S])
                for i in range(total_frames)]


def frame_to_joint_table(frame, base_transl=None):
    """Expand a 67-int16 animation frame into the 24-joint array Anchor sends.

    Anchor's PLAYER_UPDATE carries 24 joints (72 int16). Frames only fill
    joints 0..21 plus the trailing value that lands in joint 22's x.

    `base_transl` pins the root translation's x/z so the model doesn't drift
    away from the actor origin during looping locomotion -- this mirrors what
    z_player.c does when it isn't applying animation root motion.
    """
    out = list(frame) + [0] * (24 * 3 - FRAME_INT16S)
    if base_transl is not None:
        out[0] = base_transl[0]
        out[2] = base_transl[2]
    return out
