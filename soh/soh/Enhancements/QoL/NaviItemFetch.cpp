#include "soh/Enhancements/game-interactor/GameInteractor_Hooks.h"
#include "soh/ShipInit.hpp"
#include "soh/ObjectExtension/ObjectExtension.h"

extern "C" {
#include "functions.h"
#include "macros.h"
#include "variables.h"
#include "src/overlays/actors/ovl_En_Elf/z_en_elf.h"

extern PlayState* gPlayState;
}

static constexpr int32_t CVAR_NAVI_ITEM_FETCH_DEFAULT = 0;
#define CVAR_NAVI_ITEM_FETCH_NAME CVAR_ENHANCEMENT("NaviItemFetch")
#define CVAR_NAVI_ITEM_FETCH_VALUE CVarGetInteger(CVAR_NAVI_ITEM_FETCH_NAME, CVAR_NAVI_ITEM_FETCH_DEFAULT)
#define CVAR_NAVI_ITEM_FETCH_RANGE CVarGetInteger(CVAR_ENHANCEMENT("NaviItemFetchRange"), 70)
#define CVAR_NAVI_ITEM_FETCH_EVERYTHING CVarGetInteger(CVAR_ENHANCEMENT("NaviItemFetchEverything"), 0)

enum class NaviFetchState {
    IDLE,
    FLY_TO_ITEM,
    DELIVER,
};

static NaviFetchState sFetchState = NaviFetchState::IDLE;
static Actor* sFetchTarget = nullptr;
static bool sFetchTargetIsFairy = false;
static int32_t sFetchCooldown = 0;
// Navi's position while a fetch is active.
static Vec3f sNaviFlightPos = { 0.0f, 0.0f, 0.0f };

static const f32 DROP_GRAVITY = -0.9f;
// Height of the fly-over waypoint above the item.
static const f32 ARC_HEIGHT = 120.0f;

static bool IsFetchableItem00Type(s16 params) {
    if (CVAR_NAVI_ITEM_FETCH_EVERYTHING) {
        return params >= 0 && params < ITEM00_MAX;
    }
    switch (params) {
        case ITEM00_RUPEE_GREEN:
        case ITEM00_RUPEE_BLUE:
        case ITEM00_RUPEE_RED:
        case ITEM00_RUPEE_ORANGE:
        case ITEM00_RUPEE_PURPLE:
        case ITEM00_HEART:
        case ITEM00_BOMBS_A:
        case ITEM00_BOMBS_B:
        case ITEM00_BOMBS_SPECIAL:
        case ITEM00_ARROWS_SINGLE:
        case ITEM00_ARROWS_SMALL:
        case ITEM00_ARROWS_MEDIUM:
        case ITEM00_ARROWS_LARGE:
        case ITEM00_NUTS:
        case ITEM00_STICK:
        case ITEM00_SEEDS:
        case ITEM00_MAGIC_LARGE:
        case ITEM00_MAGIC_SMALL:
        case ITEM00_BOMBCHU:
            return true;
        default:
            return false;
    }
}

// Returns the fairy's randomizer check flag, or RAND_INF_MAX if it isn't a shuffled fairy.
static RandomizerInf GetFairyCheckInf(Actor* actor) {
    const auto identity = ObjectExtension::GetInstance().Get<CheckIdentity>(actor);
    if (identity == nullptr) {
        return RAND_INF_MAX;
    }
    return identity->randomizerInf;
}

// Fetchable fairies are unobtained shuffled-fairy checks; ordinary fairies are ignored.
static bool IsFetchableFairy(Actor* actor, Player* player) {
    if (actor->id != ACTOR_EN_ELF || actor == player->naviActor) {
        return false;
    }
    RandomizerInf inf = GetFairyCheckInf(actor);
    return inf != RAND_INF_MAX && !Flags_GetRandomizerInf(inf);
}

// Checks that the target actor is still alive in its actor list.
static bool FetchTargetExists() {
    if (sFetchTarget == nullptr || gPlayState == nullptr) {
        return false;
    }
    s32 category = sFetchTargetIsFairy ? ACTORCAT_ITEMACTION : ACTORCAT_MISC;
    s16 expectedId = sFetchTargetIsFairy ? ACTOR_EN_ELF : ACTOR_EN_ITEM00;
    Actor* actor = gPlayState->actorCtx.actorLists[category].head;
    while (actor != nullptr) {
        if (actor == sFetchTarget) {
            return actor->id == expectedId && actor->update != NULL;
        }
        actor = actor->next;
    }
    return false;
}

static void ResetFetchState(bool restoreGravity) {
    if (restoreGravity && !sFetchTargetIsFairy && sFetchState == NaviFetchState::DELIVER && FetchTargetExists()) {
        sFetchTarget->gravity = DROP_GRAVITY;
    }
    sFetchTarget = nullptr;
    sFetchTargetIsFairy = false;
    sFetchState = NaviFetchState::IDLE;
    sFetchCooldown = 1;
}

// Moves pos toward goal with speed proportional to distance. Returns the remaining distance.
static f32 StepTowards(Vec3f* pos, Vec3f* goal, f32 minStep, f32 maxStep) {
    f32 dx = goal->x - pos->x;
    f32 dy = goal->y - pos->y;
    f32 dz = goal->z - pos->z;
    f32 dist = sqrtf(SQ(dx) + SQ(dy) + SQ(dz));
    f32 step = CLAMP(dist * 0.25f, minStep, maxStep);

    if (dist <= step) {
        *pos = *goal;
        return 0.0f;
    }

    pos->x += dx / dist * step;
    pos->y += dy / dist * step;
    pos->z += dz / dist * step;
    return dist - step;
}

static bool SegmentClear(Vec3f from, Vec3f to) {
    CollisionPoly* poly;
    Vec3f hitPos;
    return !BgCheck_AnyLineTest1(&gPlayState->colCtx, &from, &to, &hitPos, &poly, true);
}

static Vec3f LiftedPos(Vec3f* pos, f32 lift) {
    Vec3f lifted = { pos->x, pos->y + lift, pos->z };
    return lifted;
}

static bool HasDirectPath(Vec3f* from, Vec3f* to) {
    return SegmentClear(LiftedPos(from, 20.0f), LiftedPos(to, 20.0f));
}

// Waypoint above the item for flying up and over blocked approaches.
static Vec3f ArcApex(Vec3f* from, Vec3f* to) {
    f32 apexY = (from->y > to->y ? from->y : to->y) + ARC_HEIGHT;
    Vec3f apex = { to->x, apexY, to->z };
    return apex;
}

// Reachable via a straight line or an up-and-over arc.
static bool IsReachable(Vec3f* from, Vec3f* to) {
    if (HasDirectPath(from, to)) {
        return true;
    }
    Vec3f apex = ArcApex(from, to);
    return SegmentClear(LiftedPos(from, 20.0f), apex) && SegmentClear(apex, LiftedPos(to, 20.0f));
}

// Distance and reachability gate, traced from Link's position.
static bool PassesRangeChecks(Actor* actor, Player* player) {
    f32 range = (f32)CVAR_NAVI_ITEM_FETCH_RANGE;
    return actor->xzDistToPlayer > 30.0f && actor->xzDistToPlayer <= range && fabsf(actor->yDistToPlayer) <= 250.0f &&
           IsReachable(&player->actor.world.pos, &actor->world.pos);
}

static bool IsFetchableItem00(Actor* actor) {
    EnItem00* item = (EnItem00*)actor;
    return IsFetchableItem00Type(actor->params) && item->unk_154 <= 0 && // not mid-collection
           (CVAR_NAVI_ITEM_FETCH_EVERYTHING || item->randoCheck == RC_UNKNOWN_CHECK) &&
           ((actor->bgCheckFlags & 3) || actor->gravity == 0.0f); // landed or stationary
}

// Picks the nearest fetchable drop, or shuffled fairy when "Fetch Everything" is on.
static Actor* FindFetchTarget(Player* player, bool* outIsFairy) {
    Actor* best = nullptr;
    bool bestIsFairy = false;
    f32 bestDistSq = 0.0f;

    Actor* actor = gPlayState->actorCtx.actorLists[ACTORCAT_MISC].head;
    while (actor != nullptr) {
        if (actor->id == ACTOR_EN_ITEM00 && actor->update != NULL &&
            (best == nullptr || actor->xyzDistToPlayerSq < bestDistSq) && IsFetchableItem00(actor) &&
            PassesRangeChecks(actor, player)) {
            best = actor;
            bestIsFairy = false;
            bestDistSq = actor->xyzDistToPlayerSq;
        }
        actor = actor->next;
    }

    if (CVAR_NAVI_ITEM_FETCH_EVERYTHING) {
        actor = gPlayState->actorCtx.actorLists[ACTORCAT_ITEMACTION].head;
        while (actor != nullptr) {
            if (actor->update != NULL && (best == nullptr || actor->xyzDistToPlayerSq < bestDistSq) &&
                IsFetchableFairy(actor, player) && PassesRangeChecks(actor, player)) {
                best = actor;
                bestIsFairy = true;
                bestDistSq = actor->xyzDistToPlayerSq;
            }
            actor = actor->next;
        }
    }

    *outIsFairy = bestIsFairy;
    return best;
}

// Holds the target in Link's pickup window (drops) or heal window above his head (fairies).
static void HoldTargetAtPlayer(Actor* target, Player* player) {
    target->world.pos = player->actor.world.pos;
    target->world.pos.y += sFetchTargetIsFairy ? 30.0f : 20.0f;
    target->velocity.x = 0.0f;
    target->velocity.y = 0.0f;
    target->velocity.z = 0.0f;
    target->speedXZ = 0.0f;
    if (!sFetchTargetIsFairy) {
        target->gravity = 0.0f;
    }
    target->flags |= ACTOR_FLAG_UPDATE_CULLING_DISABLED;
}

// Drops flip unk_154 > 0 when collected; fairy checks set their RandomizerInf on heal.
static bool FetchDeliveryComplete() {
    if (sFetchTargetIsFairy) {
        RandomizerInf inf = GetFairyCheckInf(sFetchTarget);
        return inf == RAND_INF_MAX || Flags_GetRandomizerInf(inf);
    }
    return ((EnItem00*)sFetchTarget)->unk_154 > 0;
}

// Pops Navi out of Link's hat (modes 7/8) into the visible follow state (mode 0).
static void ForceNaviVisible(Actor* navi, Player* player) {
    EnElf* elf = (EnElf*)navi;

    player->stateFlags2 |= PLAYER_STATE2_NAVI_ACTIVE;

    if (elf->unk_2A8 == 7 || elf->unk_2A8 == 8) {
        elf->unk_2A8 = 0;
        elf->unk_2AE = 0x400;
        elf->unk_2B0 = 0x200;
        elf->unk_2C0 = 100;
        elf->unk_2B4 = 5.0f;
        elf->unk_2B8 = 20.0f;
        elf->skelAnime.playSpeed = 1.0f;
        navi->scale.x = 0.008f;
    }
}

static void OnNaviUpdateItemFetch(void* refActor) {
    Actor* navi = static_cast<Actor*>(refActor);

    if (gPlayState == nullptr) {
        return;
    }

    Player* player = GET_PLAYER(gPlayState);
    // Only act as the player's Navi, not other EnElf instances.
    if (player == nullptr || navi != player->naviActor) {
        return;
    }

    if (gPlayState->gameOverCtx.state != GAMEOVER_INACTIVE || gPlayState->msgCtx.msgMode != 0 ||
        Player_InCsMode(gPlayState)) {
        ResetFetchState(true);
        return;
    }

    if (sFetchState != NaviFetchState::IDLE && !FetchTargetExists()) {
        sFetchTarget = nullptr;
        sFetchTargetIsFairy = false;
        sFetchState = NaviFetchState::IDLE;
        sFetchCooldown = 1;
    }

    switch (sFetchState) {
        case NaviFetchState::IDLE:
            if (sFetchCooldown > 0) {
                sFetchCooldown--;
                break;
            }
            sFetchTarget = FindFetchTarget(player, &sFetchTargetIsFairy);
            if (sFetchTarget != nullptr) {
                sFetchState = NaviFetchState::FLY_TO_ITEM;
                ForceNaviVisible(navi, player);
                sNaviFlightPos = navi->world.pos;
                Audio_PlaySoundGeneral(NA_SE_EV_FAIRY_DASH, &navi->projectedPos, 4, &gSfxDefaultFreqAndVolScale,
                                       &gSfxDefaultFreqAndVolScale, &gSfxDefaultReverb);
            }
            break;

        case NaviFetchState::FLY_TO_ITEM: {
            // Abort if collected in the meantime or a drop got knocked airborne.
            if (FetchDeliveryComplete() ||
                (!sFetchTargetIsFairy && sFetchTarget->gravity != 0.0f && !(sFetchTarget->bgCheckFlags & 3))) {
                ResetFetchState(false);
                break;
            }
            ForceNaviVisible(navi, player);
            Vec3f itemGoal = sFetchTarget->world.pos;
            itemGoal.y += 10.0f;
            Vec3f goal = itemGoal;
            if (!HasDirectPath(&sNaviFlightPos, &sFetchTarget->world.pos)) {
                goal = ArcApex(&sNaviFlightPos, &sFetchTarget->world.pos);
            }
            StepTowards(&sNaviFlightPos, &goal, 14.0f, 45.0f);
            navi->world.pos = sNaviFlightPos;

            f32 dx = itemGoal.x - sNaviFlightPos.x;
            f32 dy = itemGoal.y - sNaviFlightPos.y;
            f32 dz = itemGoal.z - sNaviFlightPos.z;
            if (SQ(dx) + SQ(dy) + SQ(dz) < SQ(15.0f)) {
                sFetchState = NaviFetchState::DELIVER;
                HoldTargetAtPlayer(sFetchTarget, player);
            }
            break;
        }

        case NaviFetchState::DELIVER:
            if (FetchDeliveryComplete()) {
                ResetFetchState(false);
                break;
            }
            ForceNaviVisible(navi, player);
            HoldTargetAtPlayer(sFetchTarget, player);
            // Navi stays put so the next fetch chains from here.
            navi->world.pos = sNaviFlightPos;
            break;
    }
}

static void RegisterNaviItemFetch() {
    ResetFetchState(true);

    COND_ID_HOOK(OnActorUpdate, ACTOR_EN_ELF, CVAR_NAVI_ITEM_FETCH_VALUE, OnNaviUpdateItemFetch);
    COND_HOOK(OnSceneInit, CVAR_NAVI_ITEM_FETCH_VALUE, [](int32_t) { ResetFetchState(false); });
}

static RegisterShipInitFunc initFunc(RegisterNaviItemFetch, { CVAR_NAVI_ITEM_FETCH_NAME });
