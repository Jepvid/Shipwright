/**
 * Custom Actor Table
 *
 * One row per Ship actor. Ids continue from ACTOR_ID_EXTRA_BASE, so the id in the left
 * comment is what gets typed into the fast64 actor field in Blender.
 *
 * Consumed twice: z64actor_enum.h builds the ActorIDExtra entries, CustomActors.cpp builds
 * the matching ActorDB registrations. Adding a row does both.
 *
 * Columns: name, desc, enum, category, flags, objectId, struct, init, destroy, update, draw
 */

/* 0x01D8 */ DEFINE_CUSTOM_ACTOR(En_Partner, "Ivan", ACTOR_EN_PARTNER, ACTORCAT_ITEMACTION,
                                 ACTOR_FLAG_UPDATE_CULLING_DISABLED | ACTOR_FLAG_DRAW_CULLING_DISABLED |
                                     ACTOR_FLAG_HOOKSHOT_PULLS_PLAYER | ACTOR_FLAG_CAN_PRESS_SWITCHES,
                                 OBJECT_GAMEPLAY_KEEP, EnPartner,
                                 EnPartner_Init, EnPartner_Destroy, EnPartner_Update, EnPartner_Draw)

