#include "gacha_fill.hpp"

#include "../SeedContext.h"
#include "../static_data.h"
#include "random.hpp"

#include <spdlog/spdlog.h>
#include <algorithm>
#include <utility>
#include <vector>

using namespace Rando;

// Returns true if this location should remain as-is (vanilla placement or excluded from the
// randomized pool) rather than being replaced by a gacha token.
static bool IsVanillaPlaced(RandomizerCheck rc) {
    auto ctx = Context::GetInstance();
    auto loc = StaticData::GetLocation(rc);
    auto itemLoc = ctx->GetItemLocation(rc);

    if (itemLoc->GetPlacedRandomizerGet() == RG_NONE) {
        return true; // start-with or excluded — not in the location table
    }

    // Link's Pocket: only replace with a token when the setting is "Anything".
    // All other settings (Dungeon Reward, Advancement, specific reward type) must keep their item.
    if (rc == RC_LINKS_POCKET) {
        return !ctx->GetOption(RSK_LINKS_POCKET).Is(RO_LINKS_POCKET_ANYTHING);
    }

    // Skip Child Zelda: these locations are given directly at save init, not from the field.
    if (ctx->GetOption(RSK_SKIP_CHILD_ZELDA).Is(RO_GENERIC_ON)) {
        if (rc == RC_SONG_FROM_IMPA || rc == RC_HC_MALON_EGG || rc == RC_HC_ZELDAS_LETTER) {
            return true;
        }
    }

    // Master Sword shuffle with adult start: given at save init, not from the field.
    if (ctx->GetOption(RSK_SHUFFLE_MASTER_SWORD).Is(RO_GENERIC_ON) &&
        ctx->GetOption(RSK_SELECTED_STARTING_AGE).Is(RO_AGE_ADULT)) {
        if (rc == RC_TOT_MASTER_SWORD) {
            return true;
        }
    }

    switch (loc->GetRCType()) {
        case RCTYPE_MAP:
        case RCTYPE_COMPASS:
            return ctx->GetOption(RSK_SHUFFLE_MAPANDCOMPASS).Is(RO_DUNGEON_ITEM_LOC_VANILLA);
        case RCTYPE_SMALL_KEY:
            return ctx->GetOption(RSK_KEYSANITY).Is(RO_DUNGEON_ITEM_LOC_VANILLA);
        case RCTYPE_GF_KEY:
            return ctx->GetOption(RSK_GERUDO_KEYS).Is(RO_GERUDO_KEYS_VANILLA);
        case RCTYPE_BOSS_KEY:
            // Ganon's boss key uses its own setting
            if (ctx->GetOption(RSK_GANONS_BOSS_KEY).Is(RO_GANON_BOSS_KEY_VANILLA)) {
                return true;
            }
            return ctx->GetOption(RSK_BOSS_KEYSANITY).Is(RO_DUNGEON_ITEM_LOC_VANILLA);
        case RCTYPE_GOSSIP_STONE:
        case RCTYPE_STATIC_HINT:
            return true; // stones never hold items
        default:
            return false;
    }
}

void BuildGachaList() {
    auto ctx = Context::GetInstance();

    bool noLogic = ctx->GetOption(RSK_LOGIC_RULES).Is(RO_LOGIC_NO_LOGIC);

    // --- Collect (RC, RG) pairs ---
    // Advancement pairs are grouped by sphere; pairs within each sphere are shuffled together
    // so the RC<->RG binding is preserved throughout the zone-placement algorithm.
    using Pair = std::pair<RandomizerCheck, RandomizerGet>;

    std::vector<std::vector<Pair>> sphereAdvPairs;
    std::vector<RandomizerCheck> advancementLocs; // for dedup in other loop

    if (!noLogic) {
        for (auto& sphere : ctx->playthroughLocations) {
            std::vector<Pair> spherePairs;
            for (RandomizerCheck rc : sphere) {
                if (IsVanillaPlaced(rc)) continue;
                RandomizerGet item = ctx->GetItemLocation(rc)->GetPlacedRandomizerGet();
                if (item == RG_NONE || item == RG_GACHA_TOKEN) continue;
                spherePairs.push_back({rc, item});
                advancementLocs.push_back(rc);
            }
            Shuffle(spherePairs);
            sphereAdvPairs.push_back(std::move(spherePairs));
        }
    }

    std::vector<Pair> otherPairs;
    for (RandomizerCheck rc : ctx->allLocations) {
        if (IsVanillaPlaced(rc)) continue;
        if (!noLogic &&
            std::find(advancementLocs.begin(), advancementLocs.end(), rc) != advancementLocs.end()) continue;
        RandomizerGet item = ctx->GetItemLocation(rc)->GetPlacedRandomizerGet();
        if (item == RG_NONE || item == RG_GACHA_TOKEN) continue;
        otherPairs.push_back({rc, item});
    }
    Shuffle(otherPairs);

    size_t totalAdv = 0;
    for (auto& v : sphereAdvPairs) totalAdv += v.size();
    size_t totalLocations = totalAdv + otherPairs.size();

    // --- Build the final lists ---
    std::vector<RandomizerGet> gachaList;
    std::vector<RandomizerCheck> gachaCheckList;

    auto appendPairs = [&](const std::vector<Pair>& pairs) {
        for (auto& [rc, rg] : pairs) {
            gachaList.push_back(rg);
            gachaCheckList.push_back(rc);
        }
    };

    if (noLogic || totalAdv == 0) {
        // No logic or nothing to sequence: fully random order.
        appendPairs(otherPairs);
    } else if (otherPairs.empty()) {
        for (auto& sphere : sphereAdvPairs)
            appendPairs(sphere);
    } else {
        // Zone-based placement with logic guarantee.
        //
        // For each sphere S the player's token budget is proportional to how many
        // advancement locations they've unlocked relative to the total:
        //   tokenLimit[S] = round(unlockedAdv[S] / totalAdv * totalLocations)
        //
        // Sphere S advancement items are placed anywhere within 0..tokenLimit[S]-1.
        // Each zone is filled with the sphere's advancement pairs plus enough junk
        // to reach the limit, then the zone is shuffled — random placement within
        // the constraint, no forced even junk distribution.
        gachaList.reserve(totalLocations);
        gachaCheckList.reserve(totalLocations);
        size_t unlockedAdv = 0;
        size_t otherIdx = 0;

        for (auto& spherePairs : sphereAdvPairs) {
            if (spherePairs.empty()) continue;
            unlockedAdv += spherePairs.size();

            size_t tokenLimit = (size_t)round((double)unlockedAdv / totalAdv * totalLocations);
            size_t currentSize = gachaList.size();

            size_t junkForZone = 0;
            if (tokenLimit > currentSize + spherePairs.size()) {
                size_t available = otherPairs.size() - otherIdx;
                junkForZone = std::min(tokenLimit - currentSize - spherePairs.size(), available);
            }

            std::vector<Pair> zone(spherePairs.begin(), spherePairs.end());
            for (size_t j = 0; j < junkForZone; j++) {
                zone.push_back(otherPairs[otherIdx++]);
            }
            Shuffle(zone);
            appendPairs(zone);
        }

        // Remaining junk after all sphere zones.
        while (otherIdx < otherPairs.size()) {
            auto& [rc, rg] = otherPairs[otherIdx++];
            gachaList.push_back(rg);
            gachaCheckList.push_back(rc);
        }
    }

    SPDLOG_INFO("GachaFill: built list of {} items ({} advancement, {} other)",
                gachaList.size(), totalAdv, otherPairs.size());

    ctx->SetGachaList(gachaList);
    ctx->SetGachaCheckList(gachaCheckList);

    // --- Replace all non-vanilla randomized locations with RG_GACHA_TOKEN ---
    for (RandomizerCheck rc : ctx->allLocations) {
        if (IsVanillaPlaced(rc)) continue;
        ctx->PlaceItemInLocation(rc, RG_GACHA_TOKEN);
    }
}
