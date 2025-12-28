#pragma once

#include <stdbool.h>
#include <libultraship/libultra.h>

#ifdef __cplusplus
extern "C" {
#endif

bool Randomizer_BankCardsEnabled();
s16 Randomizer_BankCards_GetMaxRupees();
s16 Randomizer_BankCards_GetTransactionLimit();
bool Randomizer_BankCards_CanSpend(s32 price);
bool Randomizer_BankCards_ShouldApplyFullWallets();
bool Randomizer_BankCards_FormatRupeeDigits(s16 rupees, s16* counterDigits, s16* firstDigitIndex,
                                            s16* digitCount);

#ifdef __cplusplus
}
#endif

