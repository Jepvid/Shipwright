#include <libultraship/bridge/consolevariablebridge.h>

#include "soh/Enhancements/game-interactor/GameInteractor.h"
#include "soh/ShipInit.hpp"

extern "C" {
#include "functions.h"
#include "macros.h"
#include "variables.h"
#include "overlays/actors/ovl_En_M_Thunder/z_en_m_thunder.h"
extern PlayState* gPlayState;
}

#define CVAR_SPIN_ATTACK_PICKUP_NAME CVAR_ENHANCEMENT("SpinAttackPickup")
#define CVAR_SPIN_ATTACK_PICKUP_VALUE CVarGetInteger(CVAR_SPIN_ATTACK_PICKUP_NAME, 0)

// Sword tip sweep during the spin animation
static bool SwordReaches(Player* player, EnItem00* item) {
    if (player->meleeWeaponState == 0 || player->meleeWeaponAnimation < PLAYER_MWA_SPIN_ATTACK_1H ||
        !player->meleeWeaponInfo[0].active || fabsf(item->actor.yDistToPlayer) > 50.0f) {
        return false;
    }

    f32 reach =
        Math_Vec3f_DistXZ(&player->actor.world.pos, &player->meleeWeaponInfo[0].tip) + item->collider.dim.radius;

    return item->actor.xzDistToPlayer <= reach;
}

// Magic wave cylinder from charged and magic spins
static bool SpinWaveReaches(EnItem00* item) {
    for (Actor* actor = gPlayState->actorCtx.actorLists[ACTORCAT_ITEMACTION].head; actor != nullptr;
         actor = actor->next) {
        if (actor->id != ACTOR_EN_M_THUNDER) {
            continue;
        }

        Cylinder16* wave = &((EnMThunder*)actor)->collider.dim;
        if (wave->radius <= 0) {
            continue;
        }

        f32 dx = wave->pos.x - item->actor.world.pos.x;
        f32 dz = wave->pos.z - item->actor.world.pos.z;
        f32 reach = wave->radius + item->collider.dim.radius;
        f32 waveBottom = wave->pos.y + wave->yShift;

        if ((SQ(dx) + SQ(dz) <= SQ(reach)) && (item->actor.world.pos.y <= waveBottom + wave->height) &&
            (item->actor.world.pos.y + item->collider.dim.height >= waveBottom)) {
            return true;
        }
    }

    return false;
}

static void RegisterSpinAttackPickup() {
    COND_ID_HOOK(ShouldActorUpdate, ACTOR_EN_ITEM00, CVAR_SPIN_ATTACK_PICKUP_VALUE, [](void* actorRef, bool*) {
        EnItem00* item = (EnItem00*)actorRef;
        Player* player = GET_PLAYER(gPlayState);

        // GI-entry drops keep normal pickup
        if (item->actor.params == ITEM00_SOH_GIVE_ITEM_ENTRY_GI) {
            return;
        }

        if (SwordReaches(player, item) || SpinWaveReaches(item)) {
            item->actor.xzDistToPlayer = 0.0f;
            item->actor.yDistToPlayer = 0.0f;
        }
    });
}

static RegisterShipInitFunc initFunc(RegisterSpinAttackPickup, { CVAR_SPIN_ATTACK_PICKUP_NAME });
