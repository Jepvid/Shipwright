#include "soh/ActorDB.h"
#include "soh/ShipInit.hpp"

extern "C" {
#include "src/overlays/actors/ovl_En_Partner/z_en_partner.h"
#include "macros.h"
#include "functions.h"
}

// Builds one ActorDB entry per row of the custom actor table.
#define DEFINE_CUSTOM_ACTOR(name, desc, enum, category, flags, objectId, T, init, destroy, update, draw) \
    ActorDB::Instance->AddEntry(ActorDBInit{ #name, desc, enum, category, flags, objectId, sizeof(T),    \
                                             (ActorFunc)init, (ActorFunc)destroy, (ActorFunc)update,     \
                                             (ActorFunc)draw, nullptr });

static bool registered = false;

static void RegisterCustomActors() {
    // ShipInit also fires on config load, and AddEntry asserts on a duplicate name.
    if (registered) {
        return;
    }
    registered = true;

#include "tables/actor_table_custom.h"
}

#undef DEFINE_CUSTOM_ACTOR

static RegisterShipInitFunc registerCustomActors(RegisterCustomActors);
