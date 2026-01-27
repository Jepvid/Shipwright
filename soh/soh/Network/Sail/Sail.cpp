#include "Sail.h"
#include <libultraship/bridge.h>
#include <libultraship/libultraship.h>
#include <nlohmann/json.hpp>
#include "global.h"
#include "soh/OTRGlobals.h"
#include "soh/Enhancements/randomizer/SeedContext.h"
#include "soh/Enhancements/randomizer/entrance.h"
#include "soh/Enhancements/randomizer/randomizer_entrance_tracker.h"
#include "soh/Enhancements/randomizer/randomizerTypes.h"
#include "soh/util.h"

template <class DstType, class SrcType> bool IsType(const SrcType* src) {
    return dynamic_cast<const DstType*>(src) != nullptr;
}

static bool sPendingInitialSync = false;

static void Sail_GetEntranceSceneRoomSpawn(const EntranceData* data, int32_t* scene, int32_t* room, int32_t* spawn) {
    if (scene) {
        *scene = -1;
    }
    if (room) {
        *room = -1;
    }
    if (spawn) {
        *spawn = -1;
    }

    if (data == nullptr || data->scenes.empty()) {
        return;
    }

    const auto& info = data->scenes.front();
    if (scene) {
        *scene = info.scene;
    }
    if (spawn) {
        *spawn = info.spawn;
    }
    if (room) {
        *room = (info.scene == SCENE_THIEVES_HIDEOUT && info.spawn >= 0) ? info.spawn : -1;
    }
}

static bool Sail_ShouldSkipEntrance(const EntranceData* original, const EntranceData* overrideData, s16 index,
                                    bool hideReverse, bool discoveredOnly) {
    if (original == nullptr || overrideData == nullptr) {
        return true;
    }

    if (original->metaTag.ends_with("bw") || overrideData->metaTag.ends_with("bw")) {
        return true;
    }

    const bool decoupled =
        OTRGlobals::Instance->gRandomizer->GetRandoSettingValue(RSK_DECOUPLED_ENTRANCES) == RO_GENERIC_ON;

    if ((original->type == ENTRANCE_TYPE_DUNGEON || original->type == ENTRANCE_TYPE_GROTTO ||
         original->type == ENTRANCE_TYPE_INTERIOR) &&
        (original->oneExit != 1 && !decoupled && hideReverse)) {
        return true;
    }

    if (discoveredOnly && !EntranceTracker_IsEntranceDiscovered(index)) {
        return true;
    }

    return false;
}

static void Sail_SendSeedInfo(Sail* sail) {
    if (sail == nullptr || !sail->isConnected || !GameInteractor::IsSaveLoaded()) {
        return;
    }

    bool entranceRando = false;
    bool decoupledEntrances = false;
    if (IS_RANDO) {
        entranceRando =
            OTRGlobals::Instance->gRandomizer->GetRandoSettingValue(RSK_SHUFFLE_ENTRANCES) == RO_GENERIC_ON;
        decoupledEntrances =
            OTRGlobals::Instance->gRandomizer->GetRandoSettingValue(RSK_DECOUPLED_ENTRANCES) == RO_GENERIC_ON;
    }

    nlohmann::json seedPayload;
    seedPayload["type"] = "seed_info";
    seedPayload["entranceRando"] = entranceRando;
    seedPayload["decoupledEntrances"] = decoupledEntrances;
    sail->SendJsonToRemote(seedPayload);
}

static void Sail_SendCurrentScene(Sail* sail) {
    if (sail == nullptr || !sail->isConnected || !GameInteractor::IsSaveLoaded()) {
        return;
    }

    const int32_t entranceIndex = static_cast<int32_t>(gSaveContext.entranceIndex);
    const int32_t entranceTableIndex = entranceIndex + static_cast<int32_t>(gSaveContext.sceneSetupIndex);
    const EntranceInfo entranceInfo = gEntranceTable[entranceTableIndex];
    const int32_t roomNum = gPlayState ? static_cast<int32_t>(gPlayState->roomCtx.curRoom.num) : -1;

    std::string sceneName;
    const s16 lastEntranceIndex = GetLastEntranceOverride();
    if (lastEntranceIndex >= 0) {
        const s16 nextEntranceIndex = Entrance_PeekNextIndexOverride(lastEntranceIndex);
        const EntranceData* overrideData = GetEntranceData(nextEntranceIndex);
        if (overrideData != nullptr) {
            sceneName = overrideData->destination;
        }
    }

    nlohmann::json currentScenePayload;
    currentScenePayload["type"] = "current_scene";
    currentScenePayload["sceneNum"] = static_cast<int32_t>(entranceInfo.scene);
    currentScenePayload["spawn"] = static_cast<int32_t>(entranceInfo.spawn);
    currentScenePayload["room"] = roomNum;
    if (!sceneName.empty()) {
        currentScenePayload["sceneName"] = sceneName;
    }

    sail->SendJsonToRemote(currentScenePayload);
}

static void Sail_SendEntranceMap(Sail* sail) {
    if (sail == nullptr || !sail->isConnected || !GameInteractor::IsSaveLoaded()) {
        return;
    }

    if (!IS_RANDO ||
        OTRGlobals::Instance->gRandomizer->GetRandoSettingValue(RSK_SHUFFLE_ENTRANCES) != RO_GENERIC_ON) {
        return;
    }

    auto entranceCtx = OTRGlobals::Instance->gRandoContext->GetEntranceShuffler();
    if (entranceCtx == nullptr) {
        return;
    }

    const bool hideReverse = CVarGetInteger(CVAR_TRACKER_ENTRANCE("HideReverseEntrances"), 1);

    nlohmann::json payload;
    payload["type"] = "entrance_map";
    payload["connections"] = nlohmann::json::array();

    for (size_t i = 0; i < ENTRANCE_OVERRIDES_MAX_COUNT; i++) {
        EntranceOverride entrance = entranceCtx->entranceOverrides[i];
        if (Entrance_EntranceIsNull(&entrance)) {
            break;
        }

        const EntranceData* original = GetEntranceData(entrance.index);
        const EntranceData* overrideData = GetEntranceData(entrance.override);

        if (Sail_ShouldSkipEntrance(original, overrideData, entrance.index, hideReverse, true)) {
            continue;
        }

        int32_t fromScene = -1;
        int32_t fromRoom = -1;
        int32_t toScene = -1;
        int32_t toSpawn = -1;

        Sail_GetEntranceSceneRoomSpawn(original, &fromScene, &fromRoom, nullptr);
        Sail_GetEntranceSceneRoomSpawn(overrideData, &toScene, nullptr, &toSpawn);

        nlohmann::json entry;
        entry["fromEntrance"] = static_cast<int32_t>(entrance.index);
        entry["toEntrance"] = static_cast<int32_t>(entrance.override);
        entry["fromScene"] = fromScene;
        entry["fromRoom"] = fromRoom;
        entry["toScene"] = toScene;
        entry["spawn"] = toSpawn;
        entry["fromName"] = original->source;
        entry["toName"] = overrideData->destination;

        payload["connections"].push_back(entry);
    }

    sail->SendJsonToRemote(payload);
}

void Sail::Enable() {
    Network::Enable(CVarGetString(CVAR_REMOTE_SAIL("Host"), "127.0.0.1"),
                    CVarGetInteger(CVAR_REMOTE_SAIL("Port"), 43384));
}

void Sail::OnConnected() {
    RegisterHooks();
    if (GameInteractor::IsSaveLoaded()) {
        Sail_SendSeedInfo(this);
        Sail_SendCurrentScene(this);
        Sail_SendEntranceMap(this);
        sPendingInitialSync = false;
    } else {
        sPendingInitialSync = true;
    }
}

void Sail::OnDisconnected() {
    RegisterHooks();
}

void Sail::OnIncomingJson(nlohmann::json payload) {
    SPDLOG_INFO("[Sail] Received payload: \n{}", payload.dump());

    nlohmann::json responsePayload;
    responsePayload["type"] = "result";
    responsePayload["status"] = "failure";

    try {
        if (!payload.contains("id")) {
            SPDLOG_ERROR("[Sail] Received payload without ID");
            SendJsonToRemote(responsePayload);
            return;
        }

        responsePayload["id"] = payload["id"];

        if (!payload.contains("type")) {
            SPDLOG_ERROR("[Sail] Received payload without type");
            SendJsonToRemote(responsePayload);
            return;
        }

        std::string payloadType = payload["type"].get<std::string>();

        if (payloadType == "command") {
            if (!payload.contains("command")) {
                SPDLOG_ERROR("[Sail] Received command payload without command");
                SendJsonToRemote(responsePayload);
                return;
            }

            std::string command = payload["command"].get<std::string>();
            std::reinterpret_pointer_cast<Ship::ConsoleWindow>(
                Ship::Context::GetInstance()->GetWindow()->GetGui()->GetGuiWindow("Console"))
                ->Dispatch(command);
            responsePayload["status"] = "success";
            SendJsonToRemote(responsePayload);
            return;
        } else if (payloadType == "effect") {
            if (!payload.contains("effect") || !payload["effect"].contains("type")) {
                SPDLOG_ERROR("[Sail] Received effect payload without effect type");
                SendJsonToRemote(responsePayload);
                return;
            }

            std::string effectType = payload["effect"]["type"].get<std::string>();

            // Special case for "command" effect, so we can also run commands from the `simple_twitch_sail` script
            if (effectType == "command") {
                if (!payload["effect"].contains("command")) {
                    SPDLOG_ERROR("[Sail] Received command effect payload without command");
                    SendJsonToRemote(responsePayload);
                    return;
                }

                std::string command = payload["effect"]["command"].get<std::string>();
                std::reinterpret_pointer_cast<Ship::ConsoleWindow>(
                    Ship::Context::GetInstance()->GetWindow()->GetGui()->GetGuiWindow("Console"))
                    ->Dispatch(command);
                responsePayload["status"] = "success";
                SendJsonToRemote(responsePayload);
                return;
            }

            if (effectType != "apply" && effectType != "remove") {
                SPDLOG_ERROR("[Sail] Received effect payload with unknown effect type: {}", effectType);
                SendJsonToRemote(responsePayload);
                return;
            }

            if (!GameInteractor::IsSaveLoaded()) {
                responsePayload["status"] = "try_again";
                SendJsonToRemote(responsePayload);
                return;
            }

            GameInteractionEffectBase* giEffect = EffectFromJson(payload["effect"]);
            if (giEffect) {
                GameInteractionEffectQueryResult result;
                if (effectType == "remove") {
                    if (IsType<RemovableGameInteractionEffect>(giEffect)) {
                        result = dynamic_cast<RemovableGameInteractionEffect*>(giEffect)->Remove();
                    } else {
                        result = GameInteractionEffectQueryResult::NotPossible;
                    }
                } else {
                    result = giEffect->Apply();
                }

                if (result == GameInteractionEffectQueryResult::Possible) {
                    responsePayload["status"] = "success";
                } else if (result == GameInteractionEffectQueryResult::TemporarilyNotPossible) {
                    responsePayload["status"] = "try_again";
                }
                SendJsonToRemote(responsePayload);
                return;
            }
        } else {
            SPDLOG_ERROR("[Sail] Unknown payload type: {}", payloadType);
            SendJsonToRemote(responsePayload);
            return;
        }

        // If we get here, something went wrong, send the failure response
        SPDLOG_ERROR("[Sail] Failed to handle remote JSON, sending failure response");
        SendJsonToRemote(responsePayload);
    } catch (const std::exception& e) {
        SPDLOG_ERROR("[Sail] Exception handling remote JSON: {}", e.what());
    } catch (...) { SPDLOG_ERROR("[Sail] Unknown exception handling remote JSON"); }
}

GameInteractionEffectBase* Sail::EffectFromJson(nlohmann::json payload) {
    if (!payload.contains("name")) {
        return nullptr;
    }

    std::string name = payload["name"].get<std::string>();

    if (name == "SetSceneFlag") {
        auto effect = new GameInteractionEffect::SetSceneFlag();
        if (payload.contains("parameters")) {
            effect->parameters[0] = payload["parameters"][0].get<int32_t>();
            effect->parameters[1] = payload["parameters"][1].get<int32_t>();
            effect->parameters[2] = payload["parameters"][2].get<int32_t>();
        }
        return effect;
    } else if (name == "UnsetSceneFlag") {
        auto effect = new GameInteractionEffect::UnsetSceneFlag();
        if (payload.contains("parameters")) {
            effect->parameters[0] = payload["parameters"][0].get<int32_t>();
            effect->parameters[1] = payload["parameters"][1].get<int32_t>();
            effect->parameters[2] = payload["parameters"][2].get<int32_t>();
        }
        return effect;
    } else if (name == "SetFlag") {
        auto effect = new GameInteractionEffect::SetFlag();
        if (payload.contains("parameters")) {
            effect->parameters[0] = payload["parameters"][0].get<int32_t>();
            effect->parameters[1] = payload["parameters"][1].get<int32_t>();
        }
        return effect;
    } else if (name == "UnsetFlag") {
        auto effect = new GameInteractionEffect::UnsetFlag();
        if (payload.contains("parameters")) {
            effect->parameters[0] = payload["parameters"][0].get<int32_t>();
            effect->parameters[1] = payload["parameters"][1].get<int32_t>();
        }
        return effect;
    } else if (name == "ModifyHeartContainers") {
        auto effect = new GameInteractionEffect::ModifyHeartContainers();
        if (payload.contains("parameters")) {
            effect->parameters[0] = payload["parameters"][0].get<int32_t>();
        }
        return effect;
    } else if (name == "FillMagic") {
        return new GameInteractionEffect::FillMagic();
    } else if (name == "EmptyMagic") {
        return new GameInteractionEffect::EmptyMagic();
    } else if (name == "ModifyRupees") {
        auto effect = new GameInteractionEffect::ModifyRupees();
        if (payload.contains("parameters")) {
            effect->parameters[0] = payload["parameters"][0].get<int32_t>();
        }
        return effect;
    } else if (name == "NoUI") {
        return new GameInteractionEffect::NoUI();
    } else if (name == "ModifyGravity") {
        auto effect = new GameInteractionEffect::ModifyGravity();
        if (payload.contains("parameters")) {
            effect->parameters[0] = payload["parameters"][0].get<int32_t>();
        }
        return effect;
    } else if (name == "ModifyHealth") {
        auto effect = new GameInteractionEffect::ModifyHealth();
        if (payload.contains("parameters")) {
            effect->parameters[0] = payload["parameters"][0].get<int32_t>();
        }
        return effect;
    } else if (name == "SetPlayerHealth") {
        auto effect = new GameInteractionEffect::SetPlayerHealth();
        if (payload.contains("parameters")) {
            effect->parameters[0] = payload["parameters"][0].get<int32_t>();
        }
        return effect;
    } else if (name == "FreezePlayer") {
        return new GameInteractionEffect::FreezePlayer();
    } else if (name == "BurnPlayer") {
        return new GameInteractionEffect::BurnPlayer();
    } else if (name == "ElectrocutePlayer") {
        return new GameInteractionEffect::ElectrocutePlayer();
    } else if (name == "KnockbackPlayer") {
        auto effect = new GameInteractionEffect::KnockbackPlayer();
        if (payload.contains("parameters")) {
            effect->parameters[0] = payload["parameters"][0].get<int32_t>();
        }
        return effect;
    } else if (name == "ModifyLinkSize") {
        auto effect = new GameInteractionEffect::ModifyLinkSize();
        if (payload.contains("parameters")) {
            effect->parameters[0] = payload["parameters"][0].get<int32_t>();
        }
        return effect;
    } else if (name == "InvisibleLink") {
        return new GameInteractionEffect::InvisibleLink();
    } else if (name == "PacifistMode") {
        return new GameInteractionEffect::PacifistMode();
    } else if (name == "DisableZTargeting") {
        return new GameInteractionEffect::DisableZTargeting();
    } else if (name == "WeatherRainstorm") {
        return new GameInteractionEffect::WeatherRainstorm();
    } else if (name == "ReverseControls") {
        return new GameInteractionEffect::ReverseControls();
    } else if (name == "ForceEquipBoots") {
        auto effect = new GameInteractionEffect::ForceEquipBoots();
        if (payload.contains("parameters")) {
            effect->parameters[0] = payload["parameters"][0].get<int32_t>();
        }
        return effect;
    } else if (name == "ModifyMovementSpeedMultiplier") {
        auto effect = new GameInteractionEffect::ModifyMovementSpeedMultiplier();
        if (payload.contains("parameters")) {
            effect->parameters[0] = payload["parameters"][0].get<int32_t>();
        }
        return effect;
    } else if (name == "OneHitKO") {
        return new GameInteractionEffect::OneHitKO();
    } else if (name == "ModifyDefenseModifier") {
        auto effect = new GameInteractionEffect::ModifyDefenseModifier();
        if (payload.contains("parameters")) {
            effect->parameters[0] = payload["parameters"][0].get<int32_t>();
        }
        return effect;
    } else if (name == "GiveOrTakeShield") {
        auto effect = new GameInteractionEffect::GiveOrTakeShield();
        if (payload.contains("parameters")) {
            effect->parameters[0] = payload["parameters"][0].get<int32_t>();
        }
        return effect;
    } else if (name == "TeleportPlayer") {
        auto effect = new GameInteractionEffect::TeleportPlayer();
        if (payload.contains("parameters")) {
            effect->parameters[0] = payload["parameters"][0].get<int32_t>();
        }
        return effect;
    } else if (name == "ClearAssignedButtons") {
        auto effect = new GameInteractionEffect::ClearAssignedButtons();
        if (payload.contains("parameters")) {
            effect->parameters[0] = payload["parameters"][0].get<int32_t>();
        }
        return effect;
    } else if (name == "SetTimeOfDay") {
        auto effect = new GameInteractionEffect::SetTimeOfDay();
        if (payload.contains("parameters")) {
            effect->parameters[0] = payload["parameters"][0].get<int32_t>();
        }
        return effect;
    } else if (name == "SetCollisionViewer") {
        return new GameInteractionEffect::SetCollisionViewer();
    } else if (name == "RandomizeCosmetics") {
        return new GameInteractionEffect::RandomizeCosmetics();
    } else if (name == "PressButton") {
        auto effect = new GameInteractionEffect::PressButton();
        if (payload.contains("parameters")) {
            effect->parameters[0] = payload["parameters"][0].get<int32_t>();
        }
        return effect;
    } else if (name == "PressRandomButton") {
        auto effect = new GameInteractionEffect::PressRandomButton();
        if (payload.contains("parameters")) {
            effect->parameters[0] = payload["parameters"][0].get<int32_t>();
        }
        return effect;
    } else if (name == "AddOrTakeAmmo") {
        auto effect = new GameInteractionEffect::AddOrTakeAmmo();
        if (payload.contains("parameters")) {
            effect->parameters[0] = payload["parameters"][0].get<int32_t>();
            effect->parameters[1] = payload["parameters"][1].get<int32_t>();
        }
        return effect;
    } else if (name == "RandomBombFuseTimer") {
        return new GameInteractionEffect::RandomBombFuseTimer();
    } else if (name == "DisableLedgeGrabs") {
        return new GameInteractionEffect::DisableLedgeGrabs();
    } else if (name == "RandomWind") {
        return new GameInteractionEffect::RandomWind();
    } else if (name == "RandomBonks") {
        return new GameInteractionEffect::RandomBonks();
    } else if (name == "PlayerInvincibility") {
        return new GameInteractionEffect::PlayerInvincibility();
    } else if (name == "SlipperyFloor") {
        return new GameInteractionEffect::SlipperyFloor();
    } else if (name == "SpawnEnemyWithOffset") {
        auto effect = new GameInteractionEffect::SpawnEnemyWithOffset();
        if (payload.contains("parameters")) {
            effect->parameters[0] = payload["parameters"][0].get<int32_t>();
            effect->parameters[1] = payload["parameters"][1].get<int32_t>();
        }
        return effect;
    } else if (name == "SpawnActor") {
        auto effect = new GameInteractionEffect::SpawnActor();
        if (payload.contains("parameters")) {
            effect->parameters[0] = payload["parameters"][0].get<int32_t>();
            effect->parameters[1] = payload["parameters"][1].get<int32_t>();
        }
        return effect;
    } else {
        SPDLOG_INFO("[Sail] Unknown effect name: {}", name);
        return nullptr;
    }
}

void Sail::RegisterHooks() {
    COND_HOOK(OnTransitionEnd, isConnected, [&](int32_t sceneNum) {
        if (!isConnected || !GameInteractor::IsSaveLoaded())
            return;

        static_cast<void>(sceneNum);

        // Treat transition end as the sync point: send the tracker state the website needs.
        Sail_SendSeedInfo(this);
        Sail_SendCurrentScene(this);
        Sail_SendEntranceMap(this);
        sPendingInitialSync = false;
    });

    COND_HOOK(OnLoadGame, isConnected, [&](int32_t fileNum) {
        if (!isConnected || !GameInteractor::IsSaveLoaded())
            return;

        Sail_SendSeedInfo(this);
        Sail_SendCurrentScene(this);
        Sail_SendEntranceMap(this);
        sPendingInitialSync = false;

        nlohmann::json payload;
        payload["id"] = std::rand();
        payload["type"] = "hook";
        payload["hook"]["type"] = "OnLoadGame";
        payload["hook"]["fileNum"] = fileNum;

        SendJsonToRemote(payload);
    });

    COND_HOOK(OnExitGame, isConnected, [&](int32_t fileNum) {
        if (!isConnected || !GameInteractor::IsSaveLoaded())
            return;

        nlohmann::json payload;
        payload["id"] = std::rand();
        payload["type"] = "hook";
        payload["hook"]["type"] = "OnExitGame";
        payload["hook"]["fileNum"] = fileNum;

        SendJsonToRemote(payload);
    });

    COND_HOOK(OnItemReceive, isConnected, [&](GetItemEntry itemEntry) {
        if (!isConnected || !GameInteractor::IsSaveLoaded())
            return;
        nlohmann::json payload;
        payload["id"] = std::rand();
        payload["type"] = "hook";
        payload["hook"]["type"] = "OnItemReceive";
        payload["hook"]["tableId"] = itemEntry.tableId;
        payload["hook"]["getItemId"] = itemEntry.getItemId;

        SendJsonToRemote(payload);
    });

    COND_HOOK(OnEnemyDefeat, isConnected, [&](void* refActor) {
        if (!isConnected || !GameInteractor::IsSaveLoaded())
            return;

        Actor* actor = (Actor*)refActor;
        nlohmann::json payload;
        payload["id"] = std::rand();
        payload["type"] = "hook";
        payload["hook"]["type"] = "OnEnemyDefeat";
        payload["hook"]["actorId"] = actor->id;
        payload["hook"]["params"] = actor->params;

        SendJsonToRemote(payload);
    });

    COND_HOOK(OnActorInit, isConnected, [&](void* refActor) {
        if (!isConnected || !GameInteractor::IsSaveLoaded())
            return;

        Actor* actor = (Actor*)refActor;
        nlohmann::json payload;
        payload["id"] = std::rand();
        payload["type"] = "hook";
        payload["hook"]["type"] = "OnActorInit";
        payload["hook"]["actorId"] = actor->id;
        payload["hook"]["params"] = actor->params;

        SendJsonToRemote(payload);
    });

    COND_HOOK(OnFlagSet, isConnected, [&](int16_t flagType, int16_t flag) {
        if (!isConnected || !GameInteractor::IsSaveLoaded())
            return;
        nlohmann::json payload;
        payload["id"] = std::rand();
        payload["type"] = "hook";
        payload["hook"]["type"] = "OnFlagSet";
        payload["hook"]["flagType"] = flagType;
        payload["hook"]["flag"] = flag;

        SendJsonToRemote(payload);
    });

    COND_HOOK(OnFlagUnset, isConnected, [&](int16_t flagType, int16_t flag) {
        if (!isConnected || !GameInteractor::IsSaveLoaded())
            return;
        nlohmann::json payload;
        payload["id"] = std::rand();
        payload["type"] = "hook";
        payload["hook"]["type"] = "OnFlagUnset";
        payload["hook"]["flagType"] = flagType;
        payload["hook"]["flag"] = flag;

        SendJsonToRemote(payload);
    });

    COND_HOOK(OnSceneFlagSet, isConnected, [&](int16_t sceneNum, int16_t flagType, int16_t flag) {
        if (!isConnected || !GameInteractor::IsSaveLoaded())
            return;
        nlohmann::json payload;
        payload["id"] = std::rand();
        payload["type"] = "hook";
        payload["hook"]["type"] = "OnSceneFlagSet";
        payload["hook"]["flagType"] = flagType;
        payload["hook"]["flag"] = flag;
        payload["hook"]["sceneNum"] = sceneNum;

        SendJsonToRemote(payload);
    });

    COND_HOOK(OnSceneFlagUnset, isConnected, [&](int16_t sceneNum, int16_t flagType, int16_t flag) {
        if (!isConnected || !GameInteractor::IsSaveLoaded())
            return;
        nlohmann::json payload;
        payload["id"] = std::rand();
        payload["type"] = "hook";
        payload["hook"]["type"] = "OnSceneFlagUnset";
        payload["hook"]["flagType"] = flagType;
        payload["hook"]["flag"] = flag;
        payload["hook"]["sceneNum"] = sceneNum;

        SendJsonToRemote(payload);
    });
}
