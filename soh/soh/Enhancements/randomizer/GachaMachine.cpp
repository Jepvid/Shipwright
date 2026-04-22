#include "GachaMachine.h"

#include <soh/OTRGlobals.h>

extern "C" {
#include <macros.h>
#include <functions.h>
#include <variables.h>
extern PlayState* gPlayState;
}

#include "SeedContext.h"
#include "static_data.h"
#include "soh/Enhancements/custom-message/CustomMessageManager.h"

#include <queue>
#include <string>

using namespace Rando;
using namespace std::string_literals;

static std::queue<RandomizerCheck> sGachaPendingRCs;

// ---------------------------------------------------------------------------
// Scene-based category check
// ---------------------------------------------------------------------------

static bool IsDungeonScene(int scene) {
    return (scene >= SCENE_DEKU_TREE && scene <= SCENE_INSIDE_GANONS_CASTLE) ||
           scene == SCENE_INSIDE_GANONS_CASTLE_COLLAPSE;
}

static bool IsCityVillageScene(int scene) {
    switch (scene) {
        case SCENE_KOKIRI_FOREST:
        case SCENE_KAKARIKO_VILLAGE:
        case SCENE_GORON_CITY:
        case SCENE_ZORAS_DOMAIN:
        case SCENE_MARKET_DAY:
        case SCENE_MARKET_NIGHT:
        case SCENE_MARKET_RUINS:
        case SCENE_TEMPLE_OF_TIME:
        case SCENE_HYRULE_CASTLE:
        case SCENE_BACK_ALLEY_DAY:
        case SCENE_BACK_ALLEY_NIGHT:
            return true;
        default:
            return false;
    }
}

bool GachaMachine_IsStoneActive() {
    int scene = gPlayState->sceneNum;
    if (IsDungeonScene(scene))
        return RAND_GET_OPTION(RSK_GACHA_STONES_DUNGEON).Is(RO_GENERIC_ON);
    if (IsCityVillageScene(scene))
        return RAND_GET_OPTION(RSK_GACHA_STONES_CITY).Is(RO_GENERIC_ON);
    return RAND_GET_OPTION(RSK_GACHA_STONES_OVERWORLD).Is(RO_GENERIC_ON);
}

// ---------------------------------------------------------------------------
// Helper: build a CustomMessage from a raw string and load it properly.
// ---------------------------------------------------------------------------

static void GachaLoadMessage(const std::string& text) {
    CustomMessage raw(text);
    CustomMessage formatted(raw.GetEnglish(MF_FORMATTED),
                            raw.GetGerman(MF_FORMATTED),
                            raw.GetFrench(MF_FORMATTED));
    formatted.LoadIntoFont();
}

// ---------------------------------------------------------------------------
// Interaction handler (called from OnOpenText hook)
// ---------------------------------------------------------------------------

void GachaMachine_Interact(uint16_t* textId, bool* loadFromMessageTable) {
    *loadFromMessageTable = false;

    // Clear any leftover pending RCs from a previous interaction.
    while (!sGachaPendingRCs.empty()) sGachaPendingRCs.pop();

    if (!GachaMachine_IsStoneActive()) {
        GachaLoadMessage("This stone is dormant.^Find an active&Gacha Stone to spin.");
        return;
    }

    auto& saveData = gSaveContext.ship.quest.data.randomizer;

    if (saveData.gachaItemCount == 0) {
        GachaLoadMessage("This gacha machine&has no items to give.");
        return;
    }

    if (saveData.gachaListIndex >= saveData.gachaItemCount) {
        GachaLoadMessage("The gacha machine is empty.&You have received all items!");
        return;
    }

    if (saveData.gachaTokens <= saveData.gachaListIndex) {
        GachaLoadMessage("You have no Gacha Tokens.^Find them at locations&throughout the world.");
        return;
    }

    // Claim all unclaimed items: indices [gachaListIndex, gachaTokens), capped at the list size.
    uint32_t newIndex = saveData.gachaTokens < saveData.gachaItemCount ? saveData.gachaTokens : saveData.gachaItemCount;
    uint32_t count = newIndex - saveData.gachaListIndex;

    auto ctx = Context::GetInstance();
    for (uint32_t i = 0; i < count; i++) {
        uint32_t idx = saveData.gachaListIndex + i;
        RandomizerCheck rc = (RandomizerCheck)saveData.gachaChecks[idx];
        RandomizerGet rg = (RandomizerGet)saveData.gachaItems[idx];

        // Restore the real item at this location and clear its obtained status
        // so the standard RC queue handler can process it normally.
        auto loc = ctx->GetItemLocation(rc);
        loc->SetPlacedItem(rg);
        loc->SetCheckStatus(RCSHOW_UNCHECKED);

        sGachaPendingRCs.push(rc);
    }

    saveData.gachaListIndex = newIndex;

    GachaLoadMessage("You claimed " + std::to_string(count) + " items!");
}

// ---------------------------------------------------------------------------
// RC delivery (called from RandomizerOnGameFrameUpdateHandler)
// ---------------------------------------------------------------------------

RandomizerCheck GachaMachine_PopNextPendingRC() {
    if (sGachaPendingRCs.empty()) return RC_UNKNOWN_CHECK;
    RandomizerCheck rc = sGachaPendingRCs.front();
    sGachaPendingRCs.pop();
    return rc;
}
