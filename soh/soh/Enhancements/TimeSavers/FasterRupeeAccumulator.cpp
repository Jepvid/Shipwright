#include "soh/Enhancements/game-interactor/GameInteractor.h"
#include "soh/ShipInit.hpp"
#include "soh/Enhancements/randomizer/BankCards.h"

extern "C" {
#include "z64save.h"
#include "macros.h"
#include "variables.h"
#include "functions.h"
extern PlayState* gPlayState;
extern SaveContext gSaveContext;
}

void RegisterFasterRupeeAccumulator() {
    COND_HOOK(OnInterfaceUpdate, CVarGetInteger(CVAR_ENHANCEMENT("FasterRupeeAccumulator"), 0), []() {
        if (gSaveContext.rupeeAccumulator == 0) {
            return;
        }

        // Gaining rupees
        if (gSaveContext.rupeeAccumulator > 0) {
            s16 maxRupees = Randomizer_BankCards_GetMaxRupees();
            // Wallet is full
            if (gSaveContext.rupees >= maxRupees) {
                return;
            }

            if (gSaveContext.rupeeAccumulator >= 10 && gSaveContext.rupees + 10 < maxRupees) {
                gSaveContext.rupeeAccumulator -= 10;
                gSaveContext.rupees += 10;
            }
            // Losing rupees
        } else if (gSaveContext.rupeeAccumulator < 0) {
            // No rupees to lose
            if (gSaveContext.rupees == 0) {
                return;
            }

            if (gSaveContext.rupeeAccumulator <= -10 && gSaveContext.rupees > 10) {
                gSaveContext.rupeeAccumulator += 10;
                gSaveContext.rupees -= 10;
            }
        }
    });
}

static RegisterShipInitFunc initFunc(RegisterFasterRupeeAccumulator, { CVAR_ENHANCEMENT("FasterRupeeAccumulator") });
