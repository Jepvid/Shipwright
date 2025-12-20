#include <initializer_list>

#include "src/overlays/actors/ovl_En_Elf/z_en_elf.h"
#include "objects/object_link_boy/object_link_boy.h"
#include "objects/object_link_child/object_link_child.h"
#include "objects/object_custom_equip/object_custom_equip.h"
#include "soh/Enhancements/game-interactor/GameInteractor.h"
#include "soh/ShipInit.hpp"
#include "soh/ResourceManagerHelpers.h"
#include "soh_assets.h"
#include "kaleido.h"

extern SaveContext gSaveContext;
extern PlayState* gPlayState;
extern void Overlay_DisplayText(float duration, const char* text);

static void UpdatePatchCustomEquipmentDlists();
static void UpdatePatchHand();

struct PatchRequest {
    const char* resource = nullptr;
    const char* gfx = nullptr;
    const char* dlist1 = nullptr;
    const char* dlist2 = nullptr;
    const char* dlist3 = nullptr;
    const char* alternateDL = nullptr;
};

struct UnpatchRequest {
    const char* resource = nullptr;
    const char* dlist = nullptr;
};

static void PatchOrUnpatch(const PatchRequest& request, bool useAltAssets) {
    if (request.resource == NULL || request.gfx == NULL || request.dlist1 == NULL || request.dlist2 == NULL) {
        return;
    }

    if (!useAltAssets) {
        ResourceMgr_UnpatchGfxByName(request.resource, request.dlist1);
        ResourceMgr_UnpatchGfxByName(request.resource, request.dlist2);
        if (request.dlist3 != NULL) {
            ResourceMgr_UnpatchGfxByName(request.resource, request.dlist3);
        }
        return;
    }

    if (!ResourceGetIsCustomByName(request.gfx)) {
        return;
    }

    if (request.alternateDL != NULL && !ResourceGetIsCustomByName(request.alternateDL) &&
        !ResourceMgr_FileExists(request.alternateDL)) {
        return;
    }

    ResourceMgr_PatchCustomGfxByName(request.resource, request.dlist1, 0, gsSPDisplayListOTRFilePath(request.gfx));

    if (request.dlist3 == NULL) {
        ResourceMgr_PatchCustomGfxByName(request.resource, request.dlist2, 1, gsSPEndDisplayList());
    } else {
        ResourceMgr_PatchCustomGfxByName(request.resource, request.dlist2, 1,
                                         gsSPDisplayListOTRFilePath(request.alternateDL));
        ResourceMgr_PatchCustomGfxByName(request.resource, request.dlist3, 2, gsSPEndDisplayList());
    }
}

static void ApplyPatchRequests(std::initializer_list<PatchRequest> requests, bool useAltAssets) {
    for (const PatchRequest& request : requests) {
        PatchOrUnpatch(request, useAltAssets);
    }
}

static void ApplyUnpatchRequests(std::initializer_list<UnpatchRequest> requests) {
    for (const UnpatchRequest& request : requests) {
        ResourceMgr_UnpatchGfxByName(request.resource, request.dlist);
    }
}

static void UpdateCustomEquipmentSetModel(u8 ModelGroup) {
    if (!GameInteractor::IsSaveLoaded() || gPlayState == NULL) {
        return;
    }

    UpdatePatchHand();
    UpdatePatchCustomEquipmentDlists();
}

static void UpdateCustomEquipment() {
    if (!GameInteractor::IsSaveLoaded() || gPlayState == NULL) {
        return;
    }

    UpdatePatchHand();
    UpdatePatchCustomEquipmentDlists();
}

static void PatchCustomEquipment() {
    COND_HOOK(OnPlayerSetModels, true, UpdateCustomEquipmentSetModel);
    COND_HOOK(OnSceneSpawnActors, true, UpdateCustomEquipment); // To be changed when kaleido hook is made
    // COND_HOOK(OnLinkSkeletonInit, true, UpdateCustomEquipment); //To be added once custom tunic fix is pulled
    COND_HOOK(OnAssetAltChange, true, UpdateCustomEquipment);
}

static RegisterShipInitFunc initFunc(PatchCustomEquipment);

void UpdatePatchHand() {
    const bool equipmentAlwaysVisible = CVarGetInteger(CVAR_ENHANCEMENT("EquipmentAlwaysVisible"), 0) != 0;
    const bool fixHammerHand = CVarGetInteger("gEnhancements.FixHammerHand", 0) != 0;

    if (equipmentAlwaysVisible && LINK_IS_CHILD) {
        ResourceMgr_PatchGfxByName(gLinkAdultLeftHandHoldingHammerNearDL, "childHammer1", 92,
                                   gsSPDisplayListOTRFilePath(gLinkChildLeftFistNearDL));
        ResourceMgr_PatchGfxByName(gLinkAdultLeftHandHoldingHammerNearDL, "childHammer2", 93, gsSPEndDisplayList());
        ResourceMgr_PatchGfxByName(gLinkAdultRightHandHoldingHookshotNearDL, "childHookshot1", 84,
                                   gsSPDisplayListOTRFilePath(gLinkChildRightHandClosedNearDL));
        ResourceMgr_PatchGfxByName(gLinkAdultRightHandHoldingHookshotNearDL, "childHookshot2", 85,
                                   gsSPEndDisplayList());
        ResourceMgr_PatchGfxByName(gLinkAdultRightHandHoldingBowNearDL, "childBow1", 51,
                                   gsSPDisplayListOTRFilePath(gLinkChildRightHandClosedNearDL));
        ResourceMgr_PatchGfxByName(gLinkAdultRightHandHoldingBowNearDL, "childBow2", 52, gsSPEndDisplayList());
        ResourceMgr_PatchGfxByName(gLinkAdultLeftHandHoldingMasterSwordNearDL, "childMasterSword1", 104,
                                   gsSPDisplayListOTRFilePath(gLinkChildLeftFistNearDL));
        ResourceMgr_PatchGfxByName(gLinkAdultLeftHandHoldingMasterSwordNearDL, "childMasterSword2", 105,
                                   gsSPEndDisplayList());
        ResourceMgr_PatchGfxByName(gLinkAdultLeftHandHoldingBgsNearDL, "childBiggoronSword1", 79,
                                   gsSPDisplayListOTRFilePath(gLinkChildLeftFistNearDL));
        ResourceMgr_PatchGfxByName(gLinkAdultLeftHandHoldingBgsNearDL, "childBiggoronSword2", 80, gsSPEndDisplayList());
        ResourceMgr_PatchGfxByName(gLinkAdultHandHoldingBrokenGiantsKnifeDL, "childBrokenGiantsKnife1", 76,
                                   gsSPDisplayListOTRFilePath(gLinkChildLeftFistNearDL));
        ResourceMgr_PatchGfxByName(gLinkAdultHandHoldingBrokenGiantsKnifeDL, "childBrokenGiantsKnife2", 77,
                                   gsSPEndDisplayList());
    } else {
        ResourceMgr_UnpatchGfxByName(gLinkAdultLeftHandHoldingHammerNearDL, "childHammer1");
        ResourceMgr_UnpatchGfxByName(gLinkAdultLeftHandHoldingHammerNearDL, "childHammer2");
        ResourceMgr_UnpatchGfxByName(gLinkAdultRightHandHoldingHookshotNearDL, "childHookshot1");
        ResourceMgr_UnpatchGfxByName(gLinkAdultRightHandHoldingHookshotNearDL, "childHookshot2");
        ResourceMgr_UnpatchGfxByName(gLinkAdultRightHandHoldingBowNearDL, "childBow1");
        ResourceMgr_UnpatchGfxByName(gLinkAdultRightHandHoldingBowNearDL, "childBow2");
        ResourceMgr_UnpatchGfxByName(gLinkAdultLeftHandHoldingMasterSwordNearDL, "childMasterSword1");
        ResourceMgr_UnpatchGfxByName(gLinkAdultLeftHandHoldingMasterSwordNearDL, "childMasterSword2");
        ResourceMgr_UnpatchGfxByName(gLinkAdultLeftHandHoldingBgsNearDL, "childBiggoronSword1");
        ResourceMgr_UnpatchGfxByName(gLinkAdultLeftHandHoldingBgsNearDL, "childBiggoronSword2");
        ResourceMgr_UnpatchGfxByName(gLinkAdultHandHoldingBrokenGiantsKnifeDL, "childBrokenGiantsKnife1");
        ResourceMgr_UnpatchGfxByName(gLinkAdultHandHoldingBrokenGiantsKnifeDL, "childBrokenGiantsKnife2");
    }
    if ((CVarGetInteger(CVAR_ENHANCEMENT("EquipmentAlwaysVisible"), 0)) && LINK_IS_ADULT) {
        ResourceMgr_PatchGfxByName(gLinkChildLeftFistAndKokiriSwordNearDL, "adultKokiriSword", 13,
                                   gsSPDisplayListOTRFilePath(gLinkAdultLeftHandClosedNearDL));
        ResourceMgr_PatchGfxByName(gLinkChildRightHandHoldingSlingshotNearDL, "adultSlingshot", 13,
                                   gsSPDisplayListOTRFilePath(gLinkAdultRightHandClosedNearDL));
        ResourceMgr_PatchGfxByName(gLinkChildLeftFistAndBoomerangNearDL, "adultBoomerang", 50,
                                   gsSPDisplayListOTRFilePath(gLinkAdultLeftHandClosedNearDL));
        ResourceMgr_PatchGfxByName(gLinkChildRightFistAndDekuShieldNearDL, "adultDekuShield", 49,
                                   gsSPDisplayListOTRFilePath(gLinkAdultRightHandClosedNearDL));
    } else {
        ResourceMgr_UnpatchGfxByName(gLinkChildLeftFistAndKokiriSwordNearDL, "adultKokiriSword");
        ResourceMgr_UnpatchGfxByName(gLinkChildRightHandHoldingSlingshotNearDL, "adultSlingshot");
        ResourceMgr_UnpatchGfxByName(gLinkChildLeftFistAndBoomerangNearDL, "adultBoomerang");
        ResourceMgr_UnpatchGfxByName(gLinkChildRightFistAndDekuShieldNearDL, "adultDekuShield");
    }
    if (fixHammerHand && LINK_IS_ADULT) {
        ResourceMgr_PatchGfxByName(gLinkAdultLeftHandHoldingHammerNearDL, "hammerHand1", 92,
                                   gsSPDisplayListOTRFilePath(gLinkAdultLeftHandClosedNearDL));
        ResourceMgr_PatchGfxByName(gLinkAdultLeftHandHoldingHammerNearDL, "hammerHand2", 93, gsSPEndDisplayList());
    } else {
        ResourceMgr_UnpatchGfxByName(gLinkAdultLeftHandHoldingHammerNearDL, "hammerHand1");
        ResourceMgr_UnpatchGfxByName(gLinkAdultLeftHandHoldingHammerNearDL, "hammerHand2");
    }
}

void UpdatePatchCustomEquipmentDlists() {
    const u8 equippedSword = gSaveContext.equips.buttonItems[0];
    const bool useAltAssets = CVarGetInteger(CVAR_ENHANCEMENT("AltAssets"), 0) != 0;
    const bool equipmentAlwaysVisible = CVarGetInteger(CVAR_ENHANCEMENT("EquipmentAlwaysVisible"), 0) != 0;

    switch (equippedSword) {
        case ITEM_NONE:
            if (LINK_IS_CHILD) {
                ApplyPatchRequests(
                    {
                        {gLinkChildDekuShieldWithMatrixDL, gCustomDekuShieldOnBackDL, "customChildShieldOnly1",
                         "customChildShieldOnly2", NULL, NULL},
                    },
                    useAltAssets);

                ApplyUnpatchRequests({
                    {gLinkChildSwordAndSheathNearDL, "customKokiriSwordSheath1"},
                    {gLinkChildSwordAndSheathNearDL, "customKokiriSwordSheath2"},
                    {gLinkChildSheathNearDL, "customKokiriSheath1"},
                    {gLinkChildSheathNearDL, "customKokiriSheath2"},
                    {gLinkChildDekuShieldSwordAndSheathNearDL, "customDekuShieldSword1"},
                    {gLinkChildDekuShieldSwordAndSheathNearDL, "customDekuShieldSword2"},
                    {gLinkChildDekuShieldSwordAndSheathNearDL, "customDekuShieldSword3"},
                    {gLinkChildHylianShieldSwordAndSheathNearDL, "customChildHylianShieldSword1"},
                    {gLinkChildHylianShieldSwordAndSheathNearDL, "customChildHylianShieldSword2"},
                    {gLinkChildHylianShieldSwordAndSheathNearDL, "customChildHylianShieldSword3"},
                });
            }

            if (LINK_IS_ADULT) {
                ApplyPatchRequests(
                    {
                        {gLinkAdultHylianShieldSwordAndSheathNearDL, gCustomHylianShieldOnBackDL,
                         "customAdultShieldOnly1", "customAdultShieldOnly2", NULL, NULL},
                        {gLinkAdultMirrorShieldSwordAndSheathNearDL, gCustomMirrorShieldOnBackDL,
                         "customAdultMirrorOnly1", "customAdultMirrorOnly2", NULL, NULL},
                    },
                    useAltAssets);

                ApplyUnpatchRequests({
                    {gLinkAdultMasterSwordAndSheathNearDL, "customMasterSwordSheath1"},
                    {gLinkAdultMasterSwordAndSheathNearDL, "customMasterSwordSheath2"},
                });
            }
            break;
        case ITEM_SWORD_KOKIRI:
            ApplyPatchRequests(
                {
                    {gLinkChildSheathNearDL, gCustomKokiriSwordSheathDL, "customKokiriSheath1", "customKokiriSheath2",
                     NULL, NULL},
                    {gLinkChildSwordAndSheathNearDL, gCustomKokiriSwordInSheathDL, "customKokiriSwordSheath1",
                     "customKokiriSwordSheath2", NULL, NULL},
                    {gLinkChildDekuShieldSwordAndSheathNearDL, gCustomKokiriSwordInSheathDL, "customDekuShieldSword1",
                     "customDekuShieldSword2", "customDekuShieldSword3", gCustomDekuShieldOnBackDL},
                    {gLinkChildDekuShieldAndSheathNearDL, gCustomKokiriSwordSheathDL, "customDekuShieldSheath1",
                     "customDekuShieldSheath2", "customDekuShieldSheath3", gCustomDekuShieldOnBackDL},
                    {gLinkChildHylianShieldSwordAndSheathNearDL, gCustomKokiriSwordInSheathDL,
                     "customChildHylianShieldSword1", "customChildHylianShieldSword2",
                     "customChildHylianShieldSword3", gCustomHylianShieldOnChildBackDL},
                    {gLinkChildHylianShieldAndSheathNearDL, gCustomKokiriSwordSheathDL, "customChildHylianShieldSheath1",
                     "customChildHylianShieldSheath2", "customChildHylianShieldSheath3",
                     gCustomHylianShieldOnChildBackDL},
                    {gLinkAdultSheathNearDL, gCustomKokiriSwordSheathDL, "customSheath1", "customSheath2", NULL, NULL},
                    {gLinkAdultHylianShieldSwordAndSheathNearDL, gCustomKokiriSwordInSheathDL,
                     "customHylianShieldSword1", "customHylianShieldSword2", "customHylianShieldSword3",
                     gCustomHylianShieldOnBackDL},
                    {gLinkAdultMasterSwordAndSheathNearDL, gCustomKokiriSwordInSheathDL, "customMasterSwordSheath1",
                     "customMasterSwordSheath2", NULL, NULL},
                    {gLinkAdultHylianShieldAndSheathNearDL, gCustomKokiriSwordSheathDL, "customHylianShieldSheath1",
                     "customHylianShieldSheath2", "customHylianShieldSheath3", gCustomHylianShieldOnBackDL},
                    {gLinkAdultMirrorShieldSwordAndSheathNearDL, gCustomKokiriSwordInSheathDL,
                     "customMirrorShieldSword1", "customMirrorShieldSword2", "customMirrorShieldSword3",
                     gCustomMirrorShieldOnBackDL},
                },
                useAltAssets);
            break;
        case ITEM_SWORD_MASTER:
            ApplyPatchRequests(
                {
                    {gLinkChildDekuShieldWithMatrixDL, gCustomMasterSwordInSheathDL, "customDekuShieldBack1",
                     "customDekuShieldBack2", "customDekuShieldBack3", gCustomDekuShieldOnBackDL},
                    {gLinkChildHylianShieldAndSheathNearDL, gCustomMasterSwordSheathDL, "customChildHylianShieldSheath1",
                     "customChildHylianShieldSheath2", "customChildHylianShieldSheath3",
                     gCustomHylianShieldOnChildBackDL},
                    {gLinkChildSheathNearDL, gCustomMasterSwordSheathDL, "customKokiriSheath1", "customKokiriSheath2",
                     NULL, NULL},
                    {gLinkChildSwordAndSheathNearDL, gCustomMasterSwordInSheathDL, "customKokiriSwordSheath1",
                     "customKokiriSwordSheath2", NULL, NULL},
                    {gLinkChildDekuShieldSwordAndSheathNearDL, gCustomMasterSwordInSheathDL, "customDekuShieldSword1",
                     "customDekuShieldSword2", "customDekuShieldSword3", gCustomDekuShieldOnBackDL},
                    {gLinkChildDekuShieldWithMatrixDL, gCustomMasterSwordInSheathDL, "customDekuShieldBack1",
                     "customDekuShieldBack2", "customDekuShieldBack2", gCustomDekuShieldOnBackDL},
                    {gLinkChildDekuShieldAndSheathNearDL, gCustomMasterSwordInSheathDL, "customDekuShieldSheath1",
                     "customDekuShieldSheath2", "customDekuShieldSheath3", gCustomDekuShieldOnBackDL},
                    {gLinkChildHylianShieldSwordAndSheathNearDL, gCustomMasterSwordInSheathDL,
                     "customChildHylianShieldSword1", "customChildHylianShieldSword2",
                     "customChildHylianShieldSword3", gCustomHylianShieldOnChildBackDL},
                    {gLinkChildHylianShieldAndSheathNearDL, gCustomMasterSwordSheathDL, "customChildHylianShieldSheath1",
                     "customChildHylianShieldSheath2", "customChildHylianShieldSheath3",
                     gCustomHylianShieldOnChildBackDL},
                    {gLinkAdultSheathNearDL, gCustomMasterSwordSheathDL, "customSheath1", "customSheath2", NULL, NULL},
                    {gLinkAdultHylianShieldSwordAndSheathNearDL, gCustomMasterSwordInSheathDL, "customHylianShieldSword1",
                     "customHylianShieldSword2", "customHylianShieldSword3", gCustomHylianShieldOnBackDL},
                    {gLinkAdultHylianShieldAndSheathNearDL, gCustomMasterSwordSheathDL, "customHylianShieldSheath1",
                     "customHylianShieldSheath2", "customHylianShieldSheath3", gCustomHylianShieldOnBackDL},
                    {gLinkAdultMirrorShieldSwordAndSheathNearDL, gCustomMasterSwordInSheathDL,
                     "customMirrorShieldSword1", "customMirrorShieldSword2", "customMirrorShieldSword3",
                     gCustomMirrorShieldOnBackDL},
                },
                useAltAssets);
            break;
        case ITEM_SWORD_BGS:
            if (gSaveContext.bgsFlag) {
                ApplyPatchRequests(
                    {
                        {gLinkChildDekuShieldWithMatrixDL, gCustomLongswordSheathDL, "customDekuShieldBack1",
                         "customDekuShieldBack2", "customDekuShieldBack3", gCustomDekuShieldOnBackDL},
                        {gLinkChildHylianShieldAndSheathNearDL, gCustomLongswordSheathDL, "customChildHylianShieldSheath1",
                         "customChildHylianShieldSheath2", "customChildHylianShieldSheath3",
                         gCustomHylianShieldOnChildBackDL},
                        {gLinkChildDekuShieldAndSheathNearDL, gCustomLongswordInSheathDL, "customDekuShieldSheath1",
                         "customDekuShieldSheath2", "customDekuShieldSheath3", gCustomDekuShieldOnBackDL},
                        {gLinkAdultLeftHandHoldingBgsNearDL, gCustomLongswordDL, "customBGS1", "customBGS2",
                         "customBGS3", gLinkAdultLeftHandClosedNearDL},
                        {gLinkChildSheathNearDL, gCustomLongswordSheathDL, "customKokiriSheath1", "customKokiriSheath2",
                         NULL, NULL},
                        {gLinkChildSwordAndSheathNearDL, gCustomLongswordInSheathDL, "customKokiriSwordSheath1",
                         "customKokiriSwordSheath2", NULL, NULL},
                        {gLinkChildDekuShieldSwordAndSheathNearDL, gCustomLongswordSheathDL, "customDekuShieldSword1",
                         "customDekuShieldSword2", "customDekuShieldSword3", gCustomDekuShieldOnBackDL},
                        {gLinkChildDekuShieldWithMatrixDL, gCustomLongswordSheathDL, "customDekuShieldBack1",
                         "customDekuShieldBack2", "customDekuShieldBack2", gCustomDekuShieldOnBackDL},
                        {gLinkChildDekuShieldWithMatrixDL, gCustomLongswordInSheathDL, "customDekuShieldBack1",
                         "customDekuShieldBack2", "customDekuShieldBack2", gCustomDekuShieldOnBackDL},
                        {gLinkChildHylianShieldSwordAndSheathNearDL, gCustomLongswordInSheathDL,
                         "customChildHylianShieldSword1", "customChildHylianShieldSword2",
                         "customChildHylianShieldSword3", gCustomHylianShieldOnChildBackDL},
                        {gLinkAdultSheathNearDL, gCustomLongswordSheathDL, "customSheath1", "customSheath2", NULL, NULL},
                        {gLinkAdultHylianShieldSwordAndSheathNearDL, gCustomLongswordInSheathDL,
                         "customHylianShieldSword1", "customHylianShieldSword2", "customHylianShieldSword3",
                         gCustomHylianShieldOnBackDL},
                        {gLinkAdultHylianShieldAndSheathNearDL, gCustomLongswordSheathDL, "customHylianShieldSheath1",
                         "customHylianShieldSheath2", "customHylianShieldSheath3", gCustomHylianShieldOnBackDL},
                        {gLinkAdultMirrorShieldSwordAndSheathNearDL, gCustomLongswordInSheathDL,
                         "customMirrorShieldSword1", "customMirrorShieldSword2", "customMirrorShieldSword3",
                         gCustomMirrorShieldOnBackDL},
                        {gLinkAdultMirrorShieldAndSheathNearDL, gCustomLongswordSheathDL, "customMirrorShieldSheath1",
                         "customMirrorShieldSheath2", "customMirrorShieldSheath3", gCustomMirrorShieldOnBackDL},
                    },
                    useAltAssets);
            } else {
                ApplyPatchRequests(
                    {
                        {gLinkChildDekuShieldWithMatrixDL, gCustomBreakableLongswordSheathDL, "customDekuShieldBack1",
                         "customDekuShieldBack2", "customDekuShieldBack3", gCustomDekuShieldOnBackDL},
                        {gLinkChildHylianShieldAndSheathNearDL, gCustomBreakableLongswordSheathDL,
                         "customChildHylianShieldSheath1", "customChildHylianShieldSheath2",
                         "customChildHylianShieldSheath3", gCustomHylianShieldOnChildBackDL},
                        {gLinkChildDekuShieldAndSheathNearDL, gCustomBreakableLongswordInSheathDL,
                         "customDekuShieldSheath1", "customDekuShieldSheath2", "customDekuShieldSheath3",
                         gCustomDekuShieldOnBackDL},
                        {gLinkAdultLeftHandHoldingBgsNearDL, gCustomBreakableLongswordDL, "customGK1", "customGK2",
                         "customGK3", gLinkAdultLeftHandClosedNearDL},
                        {gLinkChildSheathNearDL, gCustomBreakableLongswordSheathDL, "customKokiriSheath1",
                         "customKokiriSheath2", NULL, NULL},
                        {gLinkChildSwordAndSheathNearDL, gCustomBreakableLongswordInSheathDL, "customKokiriSwordSheath1",
                         "customKokiriSwordSheath2", NULL, NULL},
                        {gLinkChildDekuShieldSwordAndSheathNearDL, gCustomBreakableLongswordSheathDL,
                         "customDekuShieldSword1", "customDekuShieldSword2", "customDekuShieldSword3",
                         gCustomDekuShieldOnBackDL},
                        {gLinkChildDekuShieldWithMatrixDL, gCustomBreakableLongswordSheathDL, "customDekuShieldBack1",
                         "customDekuShieldBack2", "customDekuShieldBack2", gCustomDekuShieldOnBackDL},
                        {gLinkChildDekuShieldWithMatrixDL, gCustomBreakableLongswordInSheathDL, "customDekuShieldBack1",
                         "customDekuShieldBack2", "customDekuShieldBack2", gCustomDekuShieldOnBackDL},
                        {gLinkChildHylianShieldSwordAndSheathNearDL, gCustomBreakableLongswordInSheathDL,
                         "customChildHylianShieldSword1", "customChildHylianShieldSword2",
                         "customChildHylianShieldSword3", gCustomHylianShieldOnChildBackDL},
                        {gLinkAdultSheathNearDL, gCustomBreakableLongswordSheathDL, "customSheath1", "customSheath2",
                         NULL, NULL},
                        {gLinkAdultHylianShieldSwordAndSheathNearDL, gCustomBreakableLongswordInSheathDL,
                         "customHylianShieldSword1", "customHylianShieldSword2", "customHylianShieldSword3",
                         gCustomHylianShieldOnBackDL},
                        {gLinkAdultHylianShieldAndSheathNearDL, gCustomBreakableLongswordSheathDL,
                         "customHylianShieldSheath1", "customHylianShieldSheath2", "customHylianShieldSheath3",
                         gCustomHylianShieldOnBackDL},
                        {gLinkAdultMirrorShieldSwordAndSheathNearDL, gCustomBreakableLongswordInSheathDL,
                         "customMirrorShieldSword1", "customMirrorShieldSword2", "customMirrorShieldSword3",
                         gCustomMirrorShieldOnBackDL},
                        {gLinkAdultMirrorShieldAndSheathNearDL, gCustomBreakableLongswordSheathDL,
                         "customMirrorShieldSheath1", "customMirrorShieldSheath2", "customMirrorShieldSheath3",
                         gCustomMirrorShieldOnBackDL},
                    },
                    useAltAssets);
            }
            break;
        case ITEM_SWORD_KNIFE:
            ApplyPatchRequests(
                {
                    {gLinkChildDekuShieldWithMatrixDL, gCustomBrokenLongswordSheathDL, "customDekuShieldBack1",
                     "customDekuShieldBack2", "customDekuShieldBack3", gCustomDekuShieldOnBackDL},
                    {gLinkChildHylianShieldAndSheathNearDL, gCustomBrokenLongswordSheathDL, "customChildHylianShieldSheath1",
                     "customChildHylianShieldSheath2", "customChildHylianShieldSheath3",
                     gCustomHylianShieldOnChildBackDL},
                    {gLinkChildDekuShieldAndSheathNearDL, gCustomBrokenLongswordInSheathDL, "customDekuShieldSheath1",
                     "customDekuShieldSheath2", "customDekuShieldSheath3", gCustomDekuShieldOnBackDL},
                    {gLinkChildSheathNearDL, gCustomBrokenLongswordSheathDL, "customKokiriSheath1",
                     "customKokiriSheath2", NULL, NULL},
                    {gLinkChildSwordAndSheathNearDL, gCustomBrokenLongswordInSheathDL, "customKokiriSwordSheath1",
                     "customKokiriSwordSheath2", NULL, NULL},
                    {gLinkChildDekuShieldSwordAndSheathNearDL, gCustomBrokenLongswordSheathDL, "customDekuShieldSword1",
                     "customDekuShieldSword2", "customDekuShieldSword3", gCustomDekuShieldOnBackDL},
                    {gLinkChildDekuShieldWithMatrixDL, gCustomBrokenLongswordSheathDL, "customDekuShieldBack1",
                     "customDekuShieldBack2", "customDekuShieldBack2", gCustomDekuShieldOnBackDL},
                    {gLinkChildDekuShieldWithMatrixDL, gCustomBrokenLongswordInSheathDL, "customDekuShieldBack1",
                     "customDekuShieldBack2", "customDekuShieldBack2", gCustomDekuShieldOnBackDL},
                    {gLinkChildHylianShieldSwordAndSheathNearDL, gCustomBrokenLongswordInSheathDL,
                     "customChildHylianShieldSword1", "customChildHylianShieldSword2",
                     "customChildHylianShieldSword3", gCustomHylianShieldOnChildBackDL},
                    {gLinkAdultSheathNearDL, gCustomBrokenLongswordSheathDL, "customSheath1", "customSheath2", NULL,
                     NULL},
                    {gLinkAdultHylianShieldSwordAndSheathNearDL, gCustomBrokenLongswordInSheathDL,
                     "customHylianShieldSword1", "customHylianShieldSword2", "customHylianShieldSword3",
                     gCustomHylianShieldOnBackDL},
                    {gLinkAdultHylianShieldAndSheathNearDL, gCustomBrokenLongswordSheathDL, "customHylianShieldSheath1",
                     "customHylianShieldSheath2", "customHylianShieldSheath3", gCustomHylianShieldOnBackDL},
                    {gLinkAdultMirrorShieldSwordAndSheathNearDL, gCustomBrokenLongswordInSheathDL,
                     "customMirrorShieldSword1", "customMirrorShieldSword2", "customMirrorShieldSword3",
                     gCustomMirrorShieldOnBackDL},
                    {gLinkAdultMirrorShieldAndSheathNearDL, gCustomBrokenLongswordSheathDL, "customMirrorShieldSheath1",
                     "customMirrorShieldSheath2", "customMirrorShieldSheath3", gCustomMirrorShieldOnBackDL},
                },
                useAltAssets);
            break;
        default:
            break;
    }

    ApplyPatchRequests(
        {
            {gLinkAdultLeftHandHoldingMasterSwordNearDL, gCustomMasterSwordDL, "customMasterSword1", "customMasterSword2",
             "customMasterSword3", gLinkAdultLeftHandClosedNearDL},
            {gLinkAdultRightHandHoldingHylianShieldNearDL, gCustomHylianShieldDL, "customHylianShield1",
             "customHylianShield2", "customHylianShield3", gLinkAdultRightHandClosedNearDL},
            {gLinkAdultRightHandHoldingMirrorShieldNearDL, gCustomMirrorShieldDL, "customMirrorShield1",
             "customMirrorShield2", "customMirrorShield3", gLinkAdultRightHandClosedNearDL},
            {gLinkAdultHandHoldingBrokenGiantsKnifeDL, gCustomBrokenLongswordDL, "customBrokenBGS1",
             "customBrokenBGS2", "customBrokenBGS3", gLinkAdultLeftHandClosedNearDL},
            {gLinkChildLeftFistAndKokiriSwordNearDL, gCustomKokiriSwordDL, "customKokiriSword1", "customKokiriSword2",
             "customKokiriSword3", gLinkChildLeftFistNearDL},
            {gLinkChildRightFistAndDekuShieldNearDL, gCustomDekuShieldDL, "customDekuShield1", "customDekuShield2",
             "customDekuShield3", gLinkChildRightHandClosedNearDL},
            {gLinkAdultHookshotTipDL, gCustomHookshotTipDL, "customHookshotTip1", "customHookshotTip2", NULL, NULL},
            {gLinkAdultHookshotChainDL, gCustomHookshotChainDL, "customHookshotChain1", "customHookshotChain2", NULL,
             NULL},
            {gLinkChildRightHandHoldingFairyOcarinaNearDL, gCustomFairyOcarinaDL, "customFairyOcarina1",
             "customFairyOcarina2", "customFairyOcarina3", gLinkChildRightHandNearDL},
            {gLinkChildRightHandAndOotNearDL, gCustomOcarinaOfTimeDL, "customChildOcarina1", "customChildOcarina2",
             "customChildOcarina3", gLinkChildRightHandNearDL},
            {gLinkAdultRightHandHoldingBowNearDL, gCustomBowDL, "customBow1", "customBow2", "customBow3",
             gLinkAdultRightHandClosedNearDL},
            {gLinkAdultRightHandHoldingBowFirstPersonDL, gCustomBowDL, "customBowFPS1", "customBowFPS2",
             "customBowFPS3", gCustomAdultFPSHandDL},
            {gLinkAdultLeftHandHoldingHammerNearDL, gCustomHammerDL, "customHammer1", "customHammer2", "customHammer3",
             gLinkAdultLeftHandClosedNearDL},
            {gLinkChildLeftFistAndBoomerangNearDL, gCustomBoomerangDL, "customBoomerang1", "customBoomerang2",
             "customBoomerang3", gLinkChildLeftFistNearDL},
            {gLinkChildRightHandHoldingSlingshotNearDL, gCustomSlingshotDL, "customSlingshot1", "customSlingshot2",
             "customSlingshot3", gLinkChildRightHandClosedNearDL},
            {gLinkChildRightArmStretchedSlingshotDL, gCustomSlingshotDL, "customSlingshotFPS1", "customSlingshotFPS2",
             "customSlingshotFPS3", gCustomChildFPSHandDL},
        },
        useAltAssets);

    if (INV_CONTENT(ITEM_HOOKSHOT) == ITEM_HOOKSHOT) {
        ApplyPatchRequests(
            {
                {gLinkAdultRightHandHoldingHookshotNearDL, gCustomHookshotDL, "customHookshot1", "customHookshot2",
                 "customHookshot3", gLinkAdultRightHandClosedNearDL},
                {gLinkAdultRightHandHoldingHookshotFarDL, gCustomHookshotDL, "customHookshotFPS1", "customHookshotFPS2",
                 "customHookshotFPS3", gCustomAdultFPSHandDL},
            },
            useAltAssets);
    }

    if (INV_CONTENT(ITEM_LONGSHOT) == ITEM_LONGSHOT) {
        ApplyPatchRequests(
            {
                {gLinkAdultRightHandHoldingHookshotNearDL, gCustomLongshotDL, "customHookshot1", "customHookshot2",
                 "customHookshot3", gLinkAdultRightHandClosedNearDL},
                {gLinkAdultRightHandHoldingHookshotFarDL, gCustomLongshotDL, "customHookshotFPS1", "customHookshotFPS2",
                 "customHookshotFPS3", gCustomAdultFPSHandDL},
            },
            useAltAssets);
    }

    if (INV_CONTENT(ITEM_OCARINA_FAIRY) == ITEM_OCARINA_FAIRY) {
        ApplyPatchRequests(
            {
                {gLinkAdultRightHandHoldingOotNearDL, gCustomFairyOcarinaAdultDL, "customOcarina1", "customOcarina2",
                 "customOcarina3", gLinkAdultRightHandNearDL},
            },
            useAltAssets);
    }

    if (INV_CONTENT(ITEM_OCARINA_TIME) == ITEM_OCARINA_TIME) {
        ApplyPatchRequests(
            {
                {gLinkAdultRightHandHoldingOotNearDL, gCustomOcarinaOfTimeAdultDL, "customOcarina1", "customOcarina2",
                 "customOcarina3", gLinkAdultRightHandNearDL},
            },
            useAltAssets);
    }

    if (LINK_IS_CHILD && equipmentAlwaysVisible) {
        ApplyPatchRequests(
            {
                {gCustomAdultFPSHandDL, gCustomChildFPSHandDL, "patchChildFPSHand1", "patchChildFPSHand2", NULL, NULL},
                {gLinkAdultRightHandClosedNearDL, gLinkChildRightHandClosedNearDL, "customChildRightHand1",
                 "customChildRightHand2", NULL, NULL},
                {gLinkAdultLeftHandClosedNearDL, gLinkChildLeftFistNearDL, "customChildLeftHand1",
                 "customChildLeftHand2", NULL, NULL},
            },
            useAltAssets);
    }

    if (LINK_IS_ADULT && equipmentAlwaysVisible) {
        ApplyPatchRequests(
            {
                {gCustomChildFPSHandDL, gCustomAdultFPSHandDL, "patchAdultFPSHand1", "patchAdultFPSHand2", NULL, NULL},
                {gLinkChildRightHandClosedNearDL, gLinkAdultRightHandClosedNearDL, "customAdultRightHand1",
                 "customAdultRightHand2", NULL, NULL},
                {gLinkChildLeftFistNearDL, gLinkAdultLeftHandClosedNearDL, "customAdultLeftHand1", "customAdultLeftHand2",
                 NULL, NULL},
            },
            useAltAssets);
    }
}
