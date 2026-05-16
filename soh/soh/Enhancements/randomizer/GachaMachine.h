#pragma once

#include <cstdint>
#include "randomizerTypes.h"

// Returns whether the gossip stone currently being talked to is an active gacha machine.
// If false, the caller should show the "dormant stone" message instead of hints or gacha.
bool GachaMachine_IsStoneActive();

// Claim all items earned since the last visit: advances gachaListIndex up to gachaTokens,
// queues each RC for delivery, and loads the result message into the text system.
// Tokens are a monotonically increasing counter; nothing is ever deducted.
// Always sets *loadFromMessageTable = false so our message is used.
void GachaMachine_Interact(uint16_t* textId, bool* loadFromMessageTable);

// If all stone categories are disabled, replaces the token at rc with its real gacha reward
// before the item give fires. Returns true if substitution happened; caller must re-fetch
// the GetItemEntry after a true return.
bool GachaMachine_SubstituteToken(RandomizerCheck rc);

// Returns the next pending gacha RandomizerCheck to push into the randomizer queue, or
// RC_UNKNOWN_CHECK if nothing is pending. Called every frame from RandomizerOnGameFrameUpdateHandler.
RandomizerCheck GachaMachine_PopNextPendingRC();
