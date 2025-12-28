#include "BankCards.h"
#include "soh/OTRGlobals.h"
#include "randomizerTypes.h"

extern "C" {
#include "macros.h"
#include "z64save.h"
#include "functions.h"
#include "variables.h"
extern s32 Flags_GetRandomizerInf(RandomizerInf flag);
}

namespace Rando::BankCards {

static constexpr s16 BANK_CARD_MAX_RUPEES = 9999;

static bool HasRandomizerContext() {
    return OTRGlobals::Instance != nullptr && OTRGlobals::Instance->gRandomizer != nullptr;
}

bool IsEnabled() {
    return IS_RANDO && HasRandomizerContext() &&
           OTRGlobals::Instance->gRandomizer->GetRandoSettingValue(RSK_BANK_CARDS) != 0;
}

s16 GetMaxRupees() {
    // When the setting is off we behave exactly like the vanilla wallet cap.
    if (!IsEnabled()) {
        return CUR_CAPACITY(UPG_WALLET);
    }

    return BANK_CARD_MAX_RUPEES;
}

s16 GetTransactionLimit() {
    if (IsEnabled() && !Flags_GetRandomizerInf(RAND_INF_HAS_WALLET)) {
        return 0;
    }

    return CUR_CAPACITY(UPG_WALLET);
}

bool CanSpend(s32 price) {
    if (price < 0) {
        return false;
    }

    // Bank Cards restrict a single purchase to the current wallet size; otherwise only check the total rupees.
    if (IsEnabled() && price > GetTransactionLimit()) {
        return false;
    }

    return gSaveContext.rupees >= price;
}

bool ShouldApplyFullWallets() {
    if (!HasRandomizerContext() || IsEnabled()) {
        return false;
    }

    return OTRGlobals::Instance->gRandomizer->GetRandoSettingValue(RSK_FULL_WALLETS) != 0;
}

bool FormatRupeeDigits(s16 rupees, s16* counterDigits, s16* firstDigitIndex, s16* digitCount) {
    if (!IsEnabled() || counterDigits == nullptr || firstDigitIndex == nullptr || digitCount == nullptr) {
        return false;
    }

    s16 cappedRupees = rupees;
    if (cappedRupees < 0) {
        cappedRupees = 0;
    }
    s16 maxRupees = GetMaxRupees();
    if (cappedRupees > maxRupees) {
        cappedRupees = maxRupees;
    }

    counterDigits[0] = cappedRupees / 1000;
    counterDigits[1] = (cappedRupees / 100) % 10;
    counterDigits[2] = (cappedRupees / 10) % 10;
    counterDigits[3] = cappedRupees % 10;

    if (cappedRupees >= 1000) {
        *firstDigitIndex = 0;
        *digitCount = 4;
    } else if (cappedRupees >= 100) {
        *firstDigitIndex = 0;
        *digitCount = 3;
    } else {
        *firstDigitIndex = 1;
        *digitCount = 2;
    }

    return true;
}

} // namespace Rando::BankCards

extern "C" bool Randomizer_BankCardsEnabled() {
    return Rando::BankCards::IsEnabled();
}

extern "C" s16 Randomizer_BankCards_GetMaxRupees() {
    return Rando::BankCards::GetMaxRupees();
}

extern "C" s16 Randomizer_BankCards_GetTransactionLimit() {
    return Rando::BankCards::GetTransactionLimit();
}

extern "C" bool Randomizer_BankCards_CanSpend(s32 price) {
    return Rando::BankCards::CanSpend(price);
}

extern "C" bool Randomizer_BankCards_ShouldApplyFullWallets() {
    return Rando::BankCards::ShouldApplyFullWallets();
}

extern "C" bool Randomizer_BankCards_FormatRupeeDigits(s16 rupees, s16* counterDigits, s16* firstDigitIndex,
                                                       s16* digitCount) {
    return Rando::BankCards::FormatRupeeDigits(rupees, counterDigits, firstDigitIndex, digitCount);
}
