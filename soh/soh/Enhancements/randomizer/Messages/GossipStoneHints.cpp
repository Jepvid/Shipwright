/**
 * This file handles the custom messages for Gossip Stone
 * hints, and intercepts stone interaction for Gacha Mode.
 */
#include <soh/OTRGlobals.h>
#include "soh/Enhancements/randomizer/GachaMachine.h"

extern "C" {
extern PlayState* gPlayState;
#include <macros.h>
#include <functions.h>
#include <variables.h>
}

void BuildHintStoneMessage(uint16_t* textId, bool* loadFromMessageTable) {
    // Gacha Mode: redirect all stone interactions to the gacha machine handler.
    if (RAND_GET_OPTION(RSK_GACHA_MODE).Is(RO_GENERIC_ON)) {
        GachaMachine_Interact(textId, loadFromMessageTable);
        return;
    }

    if ((RAND_GET_OPTION(RSK_GOSSIP_STONE_HINTS).Is(RO_GOSSIP_STONES_NEED_TRUTH) &&
         Player_GetMask(gPlayState) == PLAYER_MASK_TRUTH) ||
        (RAND_GET_OPTION(RSK_GOSSIP_STONE_HINTS).Is(RO_GOSSIP_STONES_NEED_STONE) &&
         CHECK_QUEST_ITEM(QUEST_STONE_OF_AGONY) == 0)) {
        return;
    }
    CustomMessage msg;
    Actor* stone = GET_PLAYER(gPlayState)->talkActor;
    RandomizerHint stoneHint = RH_NONE;
    int16_t hintParams = stone->params & 0xFF;

    if (Rando::StaticData::stoneParamsToHint.contains(hintParams)) {
        stoneHint = Rando::StaticData::stoneParamsToHint[hintParams];
    } else if (hintParams == 0x18) {
        for (size_t i = 0; i < ACTORCAT_MAX; i++) {
            if (gPlayState->actorCtx.actorLists[i].length) {
                if (gPlayState->actorCtx.actorLists[i].head->id == 10 &&
                    Rando::StaticData::grottoChestParamsToHint.contains(
                        gPlayState->actorCtx.actorLists[i].head->params)) {
                    stoneHint =
                        Rando::StaticData::grottoChestParamsToHint[gPlayState->actorCtx.actorLists[i].head->params];
                }
            }
        }
    }
    if (stoneHint == RH_NONE) {
        msg = CustomMessage("INVALID STONE. PARAMS: " + std::to_string(hintParams));
    } else {
        msg = OTRGlobals::Instance->gRandoContext->GetHint(stoneHint)->GetHintMessage(MF_AUTO_FORMAT);
    }
    // Remove "Buy " if present.
    msg.Replace("Buy ", "");
    msg.Replace("Acheter: ", "");
    msg.Replace(" kaufen ", "");
    msg.Replace(" kaufen", "");
    msg.LoadIntoFont();
    *loadFromMessageTable = false;
}

void RegisterGossipStoneHints() {
    // Fire for hint mode OR gacha mode (gacha mode overrides hint behaviour inside BuildHintStoneMessage).
    COND_ID_HOOK(OnOpenText, TEXT_RANDOMIZER_GOSSIP_STONE_HINTS,
                 RAND_GET_OPTION(RSK_GOSSIP_STONE_HINTS).IsNot(RO_GOSSIP_STONES_NONE) ||
                 RAND_GET_OPTION(RSK_GACHA_MODE).Is(RO_GENERIC_ON),
                 BuildHintStoneMessage);
}

static RegisterShipInitFunc initFunc(RegisterGossipStoneHints, { "IS_RANDO" });