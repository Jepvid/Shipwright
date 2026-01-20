#include "soh/Enhancements/game-interactor/GameInteractor_Hooks.h"
#include "soh/ResourceManagerHelpers.h"
#include "soh/ShipInit.hpp"

extern "C" {
#include "macros.h"
#include "objects/object_link_boy/object_link_boy_flipbook_DL.h"
#include "objects/object_link_child/object_link_child_flipbook_DL.h"
#include "variables.h"
#include "functions.h"
#include "z64animation.h"
#include "z64player.h"
extern PlayState* gPlayState;
}

extern "C" uint8_t Player_IsCustomLinkModel();

// File-local helpers
// Set this limb index to the head limb in your custom player skeleton.
// Use -1 to disable. Limb index must be < player->skelAnime.limbCount.
static const s16 kHeadLimbIndex = 11;

// Use the texture name conventions and append "DL" for display lists.
static const char* kEyeDlPaths[2][8] = {
    {
        gLinkAdultEyesOpenDL,
        gLinkAdultEyesHalfDL,
        gLinkAdultEyesClosedfDL,
        gLinkAdultEyesRollLeftDL,
        gLinkAdultEyesRollRightDL,
        gLinkAdultEyesShockDL,
        gLinkAdultEyesUnk1DL,
        gLinkAdultEyesUnk2DL,
    },
    {
        gLinkChildEyesOpenDL,
        gLinkChildEyesHalfDL,
        gLinkChildEyesClosedfDL,
        gLinkChildEyesRollLeftDL,
        gLinkChildEyesRollRightDL,
        gLinkChildEyesShockDL,
        gLinkChildEyesUnk1DL,
        gLinkChildEyesUnk2DL,
    },
};

static const char* kMouthDlPaths[2][4] = {
    {
        gLinkAdultMouth1DL,
        gLinkAdultMouth2DL,
        gLinkAdultMouth3DL,
        gLinkAdultMouth4DL,
    },
    {
        gLinkChildMouth1DL,
        gLinkChildMouth2DL,
        gLinkChildMouth3DL,
        gLinkChildMouth4DL,
    },
};

static const u8 kEyeMouthIndexes[16][2] = {
    { 0, 0 }, { 1, 0 }, { 2, 0 }, { 0, 0 }, { 1, 0 }, { 2, 0 }, { 4, 0 }, { 5, 1 },
    { 7, 2 }, { 0, 2 }, { 3, 0 }, { 4, 0 }, { 2, 2 }, { 1, 1 }, { 0, 2 }, { 0, 0 },
};

static bool HasFaceDlConfigured() {
    return kHeadLimbIndex >= 0;
}

static s16 sLastEyeIndex = -1;
static s16 sLastMouthIndex = -1;
static s16 sLastFaceIndex = -1;
static s16 sLastLinkAge = -1;

static void UpdateFaceFlipbookDlists(s16 eyeIndex, s16 mouthIndex, s16 faceIndex, s16 linkAge) {
    if (!GameInteractor::IsSaveLoaded(true) || gPlayState == nullptr) {
        return;
    }

    Player* player = GET_PLAYER(gPlayState);
    if (eyeIndex < 0 || mouthIndex < 0) {
        const s32 face = CLAMP(faceIndex, 0, static_cast<s16>(ARRAY_COUNT(kEyeMouthIndexes) - 1));
        if (eyeIndex < 0) {
            eyeIndex = kEyeMouthIndexes[face][0];
        }
        if (mouthIndex < 0) {
            mouthIndex = kEyeMouthIndexes[face][1];
        }
    }

    sLastEyeIndex = eyeIndex;
    sLastMouthIndex = mouthIndex;
    sLastFaceIndex = faceIndex;
    sLastLinkAge = linkAge;
}

extern "C" void DrawFaceFlipbookDlists(PlayState* play, s32 limbIndex, Gfx** dList, Vec3s* rot, void* thisx) {
    Player* player = (Player*)thisx;

    if (player == nullptr || limbIndex != kHeadLimbIndex) {
        return;
    }

    if (!Player_IsCustomLinkModel() || !HasFaceDlConfigured()) {
        return;
    }

    s16 eyeIndex = sLastEyeIndex;
    s16 mouthIndex = sLastMouthIndex;
    s16 faceIndex = sLastFaceIndex;
    s16 linkAge = sLastLinkAge;

    if (eyeIndex < 0 || mouthIndex < 0) {
        const s32 face = CLAMP(faceIndex, 0, static_cast<s16>(ARRAY_COUNT(kEyeMouthIndexes) - 1));
        if (eyeIndex < 0) {
            eyeIndex = kEyeMouthIndexes[face][0];
        }
        if (mouthIndex < 0) {
            mouthIndex = kEyeMouthIndexes[face][1];
        }
    }

    eyeIndex = CLAMP(eyeIndex, 0, static_cast<s32>(ARRAY_COUNT(kEyeDlPaths[0]) - 1));
    mouthIndex = CLAMP(mouthIndex, 0, static_cast<s32>(ARRAY_COUNT(kMouthDlPaths[0]) - 1));

    const s32 ageIndex = CLAMP(linkAge, 0, 1);
    const char* eyeDl = kEyeDlPaths[ageIndex][eyeIndex];
    const char* mouthDl = kMouthDlPaths[ageIndex][mouthIndex];

    OPEN_DISPS(play->state.gfxCtx);

    const bool hasEyeDl = eyeDl != nullptr && ResourceMgr_IsAltAssetsEnabled() && ResourceMgr_FileAltExists(eyeDl);
    const bool hasMouthDl =
        mouthDl != nullptr && ResourceMgr_IsAltAssetsEnabled() && ResourceMgr_FileAltExists(mouthDl);

    if (hasEyeDl) {
        gSPDisplayList(POLY_OPA_DISP++, (Gfx*)eyeDl);
    }

    if (hasMouthDl) {
        gSPDisplayList(POLY_OPA_DISP++, (Gfx*)mouthDl);
    }

    CLOSE_DISPS(play->state.gfxCtx);
}

static void RegisterFaceFlipbookDlists() {
    COND_HOOK(OnPlayerFaceUpdate, true, UpdateFaceFlipbookDlists);
    COND_HOOK(OnPlayerPostLimbDraw, true, DrawFaceFlipbookDlists);
}

static RegisterShipInitFunc initFunc(RegisterFaceFlipbookDlists);
