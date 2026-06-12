#include "SmoothSkinning.h"
#include "soh/Enhancements/game-interactor/GameInteractor.h"
#include "soh/Enhancements/game-interactor/vanilla-behavior/GIVanillaBehavior.h"
#include "soh/ShipInit.hpp"

#include <unordered_map>
#include <vector>
#include <cmath>

extern "C" {
#include "functions.h"
#include "variables.h"
}

#include "libultraship/bridge/resourcebridge.h"

static constexpr uint8_t OTR_OP_VTX_FILEPATH = 0x24;
static constexpr uint8_t OP_ENDDL = 0xDF;

static inline void MtxFMultVec3f(const MtxF& m, float ix, float iy, float iz, float& ox, float& oy, float& oz) {
    ox = m.xw + ix * m.xx + iy * m.xy + iz * m.xz;
    oy = m.yw + ix * m.yx + iy * m.yy + iz * m.yz;
    oz = m.zw + ix * m.zx + iy * m.zy + iz * m.zz;
}

static inline void MtxFMultDir3f(const MtxF& m, float ix, float iy, float iz, float& ox, float& oy, float& oz) {
    ox = ix * m.xx + iy * m.xy + iz * m.xz;
    oy = ix * m.yx + iy * m.yy + iz * m.yz;
    oz = ix * m.zx + iy * m.zy + iz * m.zz;
}

struct SmoothVtxEntry {
    Vtx* vtx;
    const SOH::SmoothSkinVertex* ssv;
};

struct LimbSmoothState {
    SOH::SkeletonLimb* skeletonLimb;
    int boneIndex;
    bool resolved;
    std::vector<SmoothVtxEntry> entries;
};

static std::unordered_map<void*, LimbSmoothState> sSmoothRegistry;

void SmoothSkinning_RegisterLimb(void* limbDataPtr, SOH::SkeletonLimb* skelLimb) {
    LimbSmoothState state;
    state.skeletonLimb = skelLimb;
    state.boneIndex = -1;
    state.resolved = false;
    sSmoothRegistry[limbDataPtr] = std::move(state);
}

// Scans an OTR display list and collects all vertex buffer ranges.
static void CollectVtxRanges(const char* dlPath, std::vector<std::pair<Vtx*, int>>& out) {
    if (!dlPath || strncmp(dlPath, "__OTR__", 7) != 0) {
        return;
    }
    Gfx* dl = (Gfx*)ResourceGetDataByName(dlPath);
    if (!dl) {
        return;
    }

    for (Gfx* cmd = dl;; cmd++) {
        uint8_t op = (uint8_t)(cmd->words.w0 >> 24);
        if (op == OP_ENDDL) {
            break;
        }
        if (op == OTR_OP_VTX_FILEPATH) {
            const char* vtxPath = (const char*)cmd->words.w1;
            cmd++;
            int vtxCnt = (int)cmd->words.w0;
            int vtxDataOff = (int)(cmd->words.w1 & 0xFFFF);
            Vtx* base = (Vtx*)ResourceGetDataByName(vtxPath);
            if (base) {
                out.push_back({ base + vtxDataOff, vtxCnt });
            }
        }
    }
}

// Resolves vtx buffer pointers and bone index for a limb on first draw.
static void ResolveLimb(LimbSmoothState& state, void* limbDataPtr, void** skeleton, int limbCount) {
    state.resolved = true;

    state.boneIndex = -1;
    for (int i = 0; i < limbCount; i++) {
        if (skeleton[i] == limbDataPtr) {
            state.boneIndex = i;
            break;
        }
    }
    if (state.boneIndex < 0) {
        return;
    }

    StandardLimb* limb = (StandardLimb*)limbDataPtr;
    const char* dlPath = (limb->dList != nullptr) ? (const char*)limb->dList : nullptr;

    std::vector<std::pair<Vtx*, int>> vtxRanges;
    CollectVtxRanges(dlPath, vtxRanges);

    if (vtxRanges.empty()) {
        return;
    }

    for (const auto& ssv : state.skeletonLimb->smoothSkinVertices) {
        for (auto& [bufStart, count] : vtxRanges) {
            bool found = false;
            for (int i = 0; i < count; i++) {
                if (bufStart[i].v.ob[0] == ssv.domX && bufStart[i].v.ob[1] == ssv.domY &&
                    bufStart[i].v.ob[2] == ssv.domZ) {
                    state.entries.push_back({ &bufStart[i], &ssv });
                    found = true;
                    break;
                }
            }
            if (found) {
                break;
            }
        }
    }
}

// Walks the limb hierarchy and captures each bone's world matrix.
static void BuildBoneMatricesRecursive(void** skeleton, Vec3s* jointTable, int limbIndex, MtxF* boneMatrices) {
    StandardLimb* limb = (StandardLimb*)skeleton[limbIndex];
    Vec3f pos = { (f32)limb->jointPos.x, (f32)limb->jointPos.y, (f32)limb->jointPos.z };
    Vec3s rot = jointTable[limbIndex + 1];

    Matrix_Push();
    Matrix_TranslateRotateZYX(&pos, &rot);
    Matrix_Get(&boneMatrices[limbIndex]);

    if (limb->child != LIMB_DONE) {
        BuildBoneMatricesRecursive(skeleton, jointTable, limb->child, boneMatrices);
    }

    Matrix_Pop();

    if (limb->sibling != LIMB_DONE) {
        BuildBoneMatricesRecursive(skeleton, jointTable, limb->sibling, boneMatrices);
    }
}

static void BuildBoneMatrices(void** skeleton, Vec3s* jointTable, MtxF* boneMatrices) {
    StandardLimb* rootLimb = (StandardLimb*)skeleton[0];
    Vec3f rootPos = { (f32)jointTable[0].x, (f32)jointTable[0].y, (f32)jointTable[0].z };
    Vec3s rootRot = jointTable[1];

    Matrix_Push();
    Matrix_TranslateRotateZYX(&rootPos, &rootRot);
    Matrix_Get(&boneMatrices[0]);

    if (rootLimb->child != LIMB_DONE) {
        BuildBoneMatricesRecursive(skeleton, jointTable, rootLimb->child, boneMatrices);
    }

    Matrix_Pop();
}

// Blends vertex positions and normals using bone weights and writes to the vtx buffer.
static void ApplySmoothBlend(LimbSmoothState& state, const MtxF* boneMatrices) {
    if (state.boneIndex < 0 || state.entries.empty()) {
        return;
    }

    MtxF domMatrix = boneMatrices[state.boneIndex];
    MtxF invDom;
    if (SkinMatrix_Invert(&domMatrix, &invDom) != 0) {
        return; // singular, skip
    }

    for (const auto& entry : state.entries) {
        float wx = 0.0f, wy = 0.0f, wz = 0.0f;
        float wnx = 0.0f, wny = 0.0f, wnz = 0.0f;

        for (const auto& inf : entry.ssv->influences) {
            float w = inf.weight / 255.0f;
            const MtxF& bm = boneMatrices[inf.boneIndex];

            float px, py, pz;
            MtxFMultVec3f(bm, (float)inf.localX, (float)inf.localY, (float)inf.localZ, px, py, pz);
            wx += w * px;
            wy += w * py;
            wz += w * pz;

            float nx = inf.normX / 127.0f;
            float ny = inf.normY / 127.0f;
            float nz = inf.normZ / 127.0f;
            float dnx, dny, dnz;
            MtxFMultDir3f(bm, nx, ny, nz, dnx, dny, dnz);
            wnx += w * dnx;
            wny += w * dny;
            wnz += w * dnz;
        }

        float lx, ly, lz;
        MtxFMultVec3f(invDom, wx, wy, wz, lx, ly, lz);
        entry.vtx->v.ob[0] = (s16)lroundf(lx);
        entry.vtx->v.ob[1] = (s16)lroundf(ly);
        entry.vtx->v.ob[2] = (s16)lroundf(lz);

        float lnx, lny, lnz;
        MtxFMultDir3f(invDom, wnx, wny, wnz, lnx, lny, lnz);
        float len = sqrtf(lnx * lnx + lny * lny + lnz * lnz);
        if (len > 0.0001f) {
            entry.vtx->n.n[0] = (s8)((lnx / len) * 127.0f);
            entry.vtx->n.n[1] = (s8)((lny / len) * 127.0f);
            entry.vtx->n.n[2] = (s8)((lnz / len) * 127.0f);
        }
    }
}

static void RegisterSmoothSkinning() {
    COND_VB_SHOULD(VB_DRAW_SMOOTH_SKIN_OPA, CVarGetInteger(CVAR_SETTING("AltAssets"), 1), {
        if (sSmoothRegistry.empty()) {
            return;
        }

        PlayState* play = va_arg(args, PlayState*);
        SkelAnime* skelAnime = va_arg(args, SkelAnime*);

        void** skeleton = skelAnime->skeleton;
        Vec3s* jointTable = skelAnime->jointTable;
        int limbCount = (int)skelAnime->limbCount;

        if (limbCount <= 0 || limbCount > 100) {
            return;
        }

        bool hasSmoothSkin = false;
        for (int i = 0; i < limbCount; i++) {
            if (sSmoothRegistry.count(skeleton[i])) {
                hasSmoothSkin = true;
                break;
            }
        }
        if (!hasSmoothSkin) {
            return;
        }

        for (int i = 0; i < limbCount; i++) {
            auto it = sSmoothRegistry.find(skeleton[i]);
            if (it != sSmoothRegistry.end() && !it->second.resolved) {
                ResolveLimb(it->second, skeleton[i], skeleton, limbCount);
            }
        }

        MtxF boneMatrices[100];
        BuildBoneMatrices(skeleton, jointTable, boneMatrices);

        for (int i = 0; i < limbCount; i++) {
            auto it = sSmoothRegistry.find(skeleton[i]);
            if (it != sSmoothRegistry.end()) {
                ApplySmoothBlend(it->second, boneMatrices);
            }
        }

        *should = false;
    });
}

static RegisterShipInitFunc sSmoothSkinInit(RegisterSmoothSkinning, { CVAR_SETTING("AltAssets") });
