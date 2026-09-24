#pragma once

#ifndef Z64ACTOR_ENUM_H
#define Z64ACTOR_ENUM_H

#define DEFINE_ACTOR_INTERNAL(_0, enum, _2) enum,
#define DEFINE_ACTOR_UNSET(enum) enum,
#define DEFINE_ACTOR(_0, enum, _2) DEFINE_ACTOR_INTERNAL(_0, enum, _2)

// Keeps only the enum column of the custom actor table; the rest is for CustomActors.cpp.
#define DEFINE_CUSTOM_ACTOR(_name, _desc, enum, ...) enum,

enum ActorID {
#include "tables/actor_table.h"
    /* 0x01D7 */ ACTOR_ID_MAX // originally "ACTOR_DLF_MAX"
};

// Actors added by Ship. They start past ACTOR_ID_MAX so it keeps working as the vanilla bound and as
// the "no actor" sentinel (see location_list.cpp). ActorDB hands out ids from ACTOR_ID_EXTRA_MAX up
// to anything registering dynamically at runtime.
enum ActorIDExtra {
    ACTOR_ID_EXTRA_BASE = ACTOR_ID_MAX, // anchor; the first custom actor is ACTOR_ID_MAX + 1
#include "tables/actor_table_custom.h"
    ACTOR_ID_EXTRA_MAX
};

#undef DEFINE_ACTOR
#undef DEFINE_ACTOR_INTERNAL
#undef DEFINE_ACTOR_UNSET
#undef DEFINE_CUSTOM_ACTOR

#endif