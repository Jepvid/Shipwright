"""Item catalogue for the Anchor testbed.

Each entry pairs the three fields a dummy player needs to visibly hold an item
with the animations that sell it:

  itemAction / heldItemAction  PLAYER_IA_* (z64player.h)
  modelGroup                   sActionModelGroups[itemAction] (z_player_lib.c)
  buttonItem0                  ITEM_* id (z64item.h), drawn from equips slot 0

The receiving client applies these straight onto the dummy Player in
DummyPlayer_Update -- there is no inventory check anywhere in that path, so a
fake player can hold anything regardless of what it "owns".
"""

# PLAYER_IA_* (z64player.h)
IA_NONE = 0x00
IA_SWORD_MASTER = 0x03
IA_SWORD_KOKIRI = 0x04
IA_SWORD_BIGGORON = 0x05
IA_DEKU_STICK = 0x06
IA_HAMMER = 0x07
IA_BOW = 0x08
IA_BOW_FIRE = 0x09
IA_BOW_ICE = 0x0A
IA_BOW_LIGHT = 0x0B
IA_SLINGSHOT = 0x0F
IA_HOOKSHOT = 0x10
IA_LONGSHOT = 0x11
IA_BOMB = 0x12
IA_BOMBCHU = 0x13
IA_BOOMERANG = 0x14
IA_FARORES_WIND = 0x18
IA_NAYRUS_LOVE = 0x19
IA_DINS_FIRE = 0x1A
IA_DEKU_NUT = 0x1B
IA_OCARINA_FAIRY = 0x1C
IA_OCARINA_OF_TIME = 0x1D
IA_BOTTLE = 0x1E
IA_MAGIC_BEAN = 0x2E
IA_LENS_OF_TRUTH = 0x42

# PLAYER_MODELGROUP_* (z64player.h)
MG_SWORD_AND_SHIELD = 2
MG_DEFAULT = 3
MG_BGS = 5
MG_BOW_SLINGSHOT = 6
MG_EXPLOSIVES = 7
MG_BOOMERANG = 8
MG_HOOKSHOT = 9
MG_10 = 10          # deku stick / fishing pole
MG_HAMMER = 11
MG_CHILD_HYLIAN_SHIELD = 1
MG_OCARINA = 12
MG_OOT = 13
MG_BOTTLE = 14

# Equipment (z64player.h)
PLAYER_SHIELD_NONE = 0
PLAYER_SHIELD_DEKU = 1
PLAYER_SHIELD_HYLIAN = 2
PLAYER_SHIELD_MIRROR = 3

PLAYER_TUNIC_KOKIRI = 0
PLAYER_TUNIC_GORON = 1
PLAYER_TUNIC_ZORA = 2

PLAYER_BOOTS_KOKIRI = 0
PLAYER_BOOTS_IRON = 1
PLAYER_BOOTS_HOVER = 2

# ITEM_* (z64item.h)
ITEM_STICK = 0x00
ITEM_NUT = 0x01
ITEM_BOMB = 0x02
ITEM_BOW = 0x03
ITEM_DINS_FIRE = 0x05
ITEM_SLINGSHOT = 0x06
ITEM_OCARINA_FAIRY = 0x07
ITEM_OCARINA_TIME = 0x08
ITEM_BOMBCHU = 0x09
ITEM_HOOKSHOT = 0x0A
ITEM_LONGSHOT = 0x0B
ITEM_FARORES_WIND = 0x0D
ITEM_BOOMERANG = 0x0E
ITEM_LENS = 0x0F
ITEM_BEAN = 0x10
ITEM_HAMMER = 0x11
ITEM_NAYRUS_LOVE = 0x13
ITEM_BOTTLE = 0x14
ITEM_BOW_ARROW_FIRE = 0x38
ITEM_BOW_ARROW_ICE = 0x39
ITEM_BOW_ARROW_LIGHT = 0x3A
ITEM_SWORD_KOKIRI = 0x3B
ITEM_SWORD_MASTER = 0x3C
ITEM_SWORD_BGS = 0x3D
ITEM_NONE = 0xFF


class Item:
    """One holdable item plus the animations used while holding/using it."""

    __slots__ = ("key", "label", "item_action", "model_group", "button_item",
                 "idle_anim", "use_anim", "run_anim", "walk_anim", "adult_only",
                 "melee")

    def __init__(self, key, label, item_action, model_group, button_item,
                 idle_anim="link_normal_wait", use_anim=None,
                 run_anim="link_normal_run", walk_anim="link_normal_walk",
                 adult_only=False, melee=False):
        self.key = key
        self.label = label
        self.item_action = item_action
        self.model_group = model_group
        self.button_item = button_item
        self.idle_anim = idle_anim
        self.use_anim = use_anim
        self.run_anim = run_anim
        self.walk_anim = walk_anim
        self.adult_only = adult_only
        self.melee = melee

    def __repr__(self):
        return f"<Item {self.key}>"


# Free-handed locomotion reads better for items held in one hand.
_FREE_RUN = "link_normal_run_free"
_FREE_WALK = "link_normal_walk_free"

CATALOGUE = [
    Item("none", "Empty handed", IA_NONE, MG_DEFAULT, ITEM_NONE,
         idle_anim="link_normal_wait", use_anim="link_normal_check"),

    Item("kokiri_sword", "Kokiri Sword", IA_SWORD_KOKIRI, MG_SWORD_AND_SHIELD,
         ITEM_SWORD_KOKIRI, idle_anim="link_fighter_wait_long",
         use_anim="link_fighter_normal_kiru", melee=True),
    Item("master_sword", "Master Sword", IA_SWORD_MASTER, MG_SWORD_AND_SHIELD,
         ITEM_SWORD_MASTER, idle_anim="link_fighter_wait_long",
         use_anim="link_fighter_normal_kiru", adult_only=True, melee=True),
    Item("biggoron_sword", "Biggoron's Sword", IA_SWORD_BIGGORON, MG_BGS,
         ITEM_SWORD_BGS, idle_anim="link_fighter_wait_long",
         use_anim="link_fighter_normal_kiru", adult_only=True, melee=True),

    Item("deku_stick", "Deku Stick", IA_DEKU_STICK, MG_10, ITEM_STICK,
         idle_anim="link_fighter_wait_long", use_anim="link_fighter_normal_kiru",
         melee=True),
    Item("hammer", "Megaton Hammer", IA_HAMMER, MG_HAMMER, ITEM_HAMMER,
         idle_anim="link_fighter_wait_long", use_anim="link_hammer_hit",
         adult_only=True, melee=True),

    Item("bow", "Fairy Bow", IA_BOW, MG_BOW_SLINGSHOT, ITEM_BOW,
         idle_anim="link_bow_bow_wait", use_anim="link_bow_bow_shoot",
         run_anim=_FREE_RUN, walk_anim=_FREE_WALK, adult_only=True),
    Item("fire_arrow", "Fire Arrow", IA_BOW_FIRE, MG_BOW_SLINGSHOT,
         ITEM_BOW_ARROW_FIRE, idle_anim="link_bow_bow_wait",
         use_anim="link_bow_bow_shoot", run_anim=_FREE_RUN, walk_anim=_FREE_WALK,
         adult_only=True),
    Item("ice_arrow", "Ice Arrow", IA_BOW_ICE, MG_BOW_SLINGSHOT,
         ITEM_BOW_ARROW_ICE, idle_anim="link_bow_bow_wait",
         use_anim="link_bow_bow_shoot", run_anim=_FREE_RUN, walk_anim=_FREE_WALK,
         adult_only=True),
    Item("light_arrow", "Light Arrow", IA_BOW_LIGHT, MG_BOW_SLINGSHOT,
         ITEM_BOW_ARROW_LIGHT, idle_anim="link_bow_bow_wait",
         use_anim="link_bow_bow_shoot", run_anim=_FREE_RUN, walk_anim=_FREE_WALK,
         adult_only=True),
    Item("slingshot", "Fairy Slingshot", IA_SLINGSHOT, MG_BOW_SLINGSHOT,
         ITEM_SLINGSHOT, idle_anim="link_bow_bow_wait",
         use_anim="link_bow_bow_shoot", run_anim=_FREE_RUN, walk_anim=_FREE_WALK),

    Item("hookshot", "Hookshot", IA_HOOKSHOT, MG_HOOKSHOT, ITEM_HOOKSHOT,
         idle_anim="link_hook_wait", use_anim="link_hook_shot_ready",
         run_anim=_FREE_RUN, walk_anim=_FREE_WALK, adult_only=True),
    Item("longshot", "Longshot", IA_LONGSHOT, MG_HOOKSHOT, ITEM_LONGSHOT,
         idle_anim="link_hook_wait", use_anim="link_hook_shot_ready",
         run_anim=_FREE_RUN, walk_anim=_FREE_WALK, adult_only=True),

    Item("bomb", "Bomb", IA_BOMB, MG_EXPLOSIVES, ITEM_BOMB,
         idle_anim="link_normal_wait", use_anim="link_normal_light_bom",
         run_anim=_FREE_RUN, walk_anim=_FREE_WALK),
    Item("bombchu", "Bombchu", IA_BOMBCHU, MG_EXPLOSIVES, ITEM_BOMBCHU,
         idle_anim="link_normal_wait", use_anim="link_normal_light_bom",
         run_anim=_FREE_RUN, walk_anim=_FREE_WALK),

    Item("boomerang", "Boomerang", IA_BOOMERANG, MG_BOOMERANG, ITEM_BOOMERANG,
         idle_anim="link_boom_throw_waitR", use_anim="link_boom_throwR",
         run_anim=_FREE_RUN, walk_anim=_FREE_WALK),

    Item("dins_fire", "Din's Fire", IA_DINS_FIRE, MG_DEFAULT, ITEM_DINS_FIRE,
         idle_anim="link_normal_wait", use_anim="link_magic_honoo1"),
    Item("farores_wind", "Farore's Wind", IA_FARORES_WIND, MG_DEFAULT,
         ITEM_FARORES_WIND, idle_anim="link_normal_wait",
         use_anim="link_magic_kaze1"),
    Item("nayrus_love", "Nayru's Love", IA_NAYRUS_LOVE, MG_DEFAULT,
         ITEM_NAYRUS_LOVE, idle_anim="link_normal_wait",
         use_anim="link_magic_tamashii1"),

    Item("deku_nut", "Deku Nut", IA_DEKU_NUT, MG_DEFAULT, ITEM_NUT,
         idle_anim="link_normal_wait", use_anim="link_normal_put"),
    Item("magic_bean", "Magic Bean", IA_MAGIC_BEAN, MG_DEFAULT, ITEM_BEAN,
         idle_anim="link_normal_wait", use_anim="link_normal_put"),
    Item("lens", "Lens of Truth", IA_LENS_OF_TRUTH, MG_DEFAULT, ITEM_LENS,
         idle_anim="link_normal_wait", use_anim="link_normal_check"),

    Item("ocarina_fairy", "Fairy Ocarina", IA_OCARINA_FAIRY, MG_OCARINA,
         ITEM_OCARINA_FAIRY, idle_anim="link_normal_wait",
         use_anim="link_normal_check", run_anim=_FREE_RUN, walk_anim=_FREE_WALK),
    Item("ocarina_time", "Ocarina of Time", IA_OCARINA_OF_TIME, MG_OOT,
         ITEM_OCARINA_TIME, idle_anim="link_normal_wait",
         use_anim="link_normal_check", run_anim=_FREE_RUN, walk_anim=_FREE_WALK),

    Item("bottle", "Bottle", IA_BOTTLE, MG_BOTTLE, ITEM_BOTTLE,
         idle_anim="link_normal_wait", use_anim="link_bottle_drink_demo",
         run_anim=_FREE_RUN, walk_anim=_FREE_WALK),
]

BY_KEY = {item.key: item for item in CATALOGUE}


def resolve(keys, child=False):
    """Turn a list of item keys (or 'all') into Item objects."""
    if not keys or "all" in keys:
        chosen = list(CATALOGUE)
    else:
        chosen = []
        for k in keys:
            if k not in BY_KEY:
                raise KeyError(k)
            chosen.append(BY_KEY[k])
    if child:
        chosen = [i for i in chosen if not i.adult_only]
        if not chosen:
            chosen = [BY_KEY["none"]]
    return chosen


# --- Gear -------------------------------------------------------------------
# currentTunic / currentShield / currentBoots are applied to the dummy Player
# verbatim, so any combination renders. Defaults stay age-appropriate; pass
# any_gear=True to allow every combination.

TUNICS = [
    (PLAYER_TUNIC_KOKIRI, "Kokiri Tunic"),
    (PLAYER_TUNIC_GORON, "Goron Tunic"),
    (PLAYER_TUNIC_ZORA, "Zora Tunic"),
]

SHIELDS_ALL = [
    (PLAYER_SHIELD_NONE, "no shield"),
    (PLAYER_SHIELD_DEKU, "Deku Shield"),
    (PLAYER_SHIELD_HYLIAN, "Hylian Shield"),
    (PLAYER_SHIELD_MIRROR, "Mirror Shield"),
]
# Deku shield is child-only, Mirror shield adult-only in vanilla.
SHIELDS_CHILD = [s for s in SHIELDS_ALL if s[0] != PLAYER_SHIELD_MIRROR]
SHIELDS_ADULT = [s for s in SHIELDS_ALL if s[0] != PLAYER_SHIELD_DEKU]

BOOTS_ALL = [
    (PLAYER_BOOTS_KOKIRI, "Kokiri Boots"),
    (PLAYER_BOOTS_IRON, "Iron Boots"),
    (PLAYER_BOOTS_HOVER, "Hover Boots"),
]
# Child has no iron/hover boots in vanilla, but rando hands them out.
BOOTS_CHILD = [BOOTS_ALL[0]]


def gear_options(child=False, any_gear=False):
    """Return (tunics, shields, boots) lists appropriate for the age."""
    if any_gear:
        return TUNICS, SHIELDS_ALL, BOOTS_ALL
    if child:
        return TUNICS, SHIELDS_CHILD, BOOTS_CHILD
    return TUNICS, SHIELDS_ADULT, BOOTS_ALL


def model_group_for(item, child, shield):
    """Mirror Player_ActionToModelGroup's child + Hylian shield special case."""
    if (item.model_group == MG_SWORD_AND_SHIELD and child
            and shield == PLAYER_SHIELD_HYLIAN):
        return MG_CHILD_HYLIAN_SHIELD
    return item.model_group
