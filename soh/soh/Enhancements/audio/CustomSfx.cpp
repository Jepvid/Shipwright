#include <string>
#include <cstring>
#include <memory>
#include <unordered_map>

#include <ship/Context.h>
#include <ship/resource/ResourceManager.h>
#include <spdlog/spdlog.h>

#include "soh/Enhancements/game-interactor/GameInteractor.h"
#include "soh/ShipInit.hpp"
#include "soh/resource/type/AudioSample.h"
#include "soh/ResourceManagerHelpers.h"

extern "C" {
#include "variables.h"
#include "sfx.h"
}

typedef struct CustomSfxName {
    const char* name;
    uint32_t id;
} CustomSfxName;

#include "CustomSfxNames.inc"

// NA_SE_OC_OCARINA cycles through several font instruments (ocarina, Malon, whistle, harp,
// grind organ, flute). An override for it should only replace the actual ocarina.
#define OCARINA_SFX_ID 0x5800
#define OCARINA_FONT_INSTRUMENT 52

struct CustomSfxEntry {
    std::shared_ptr<SOH::AudioSample> resource;
    bool pitched = false;
    // Copies of the vanilla instruments this sfx was seen playing through, with the sample
    // pointers swapped to the custom sample. Built lazily on the audio thread.
    std::unordered_map<Instrument*, Instrument*> clones;
};

// sfx ids carry flag bits (SFX_FLAG 0x800 and the 0xC00 state bits) that call sites may add or
// strip; bank and index are the identity.
static uint16_t NormalizeSfxId(uint32_t sfxId) {
    return (uint16_t)((SFX_BANK(sfxId) << 9) | SFX_INDEX(sfxId));
}

static std::unordered_map<uint16_t, CustomSfxEntry> sOverrides;
// SoundFontSounds belonging to as-recorded clones -> frequency ratio playing them as recorded.
static std::unordered_map<SoundFontSound*, float> sLockedSounds;
static uint16_t sChannelSfx[16] = { 0 };

static void ScanCustomSfx() {
    static std::unordered_map<std::string, uint32_t> nameToId;
    if (nameToId.empty()) {
        for (const auto& entry : sCustomSfxNames) {
            nameToId[entry.name] = entry.id;
        }
    }

    sOverrides.clear();
    sLockedSounds.clear();

    int fileCount = 0;
    char** fileList = ResourceMgr_ListFiles("audio/custom/*", &fileCount);
    for (int i = 0; i < fileCount; i++) {
        std::string path = fileList[i];
        std::string name = path.substr(path.find_last_of('/') + 1);

        bool pitched = false;
        constexpr std::string_view kPitchedSuffix = ".pitched";
        if (name.size() > kPitchedSuffix.size() &&
            name.compare(name.size() - kPitchedSuffix.size(), std::string::npos, kPitchedSuffix) == 0) {
            pitched = true;
            name.resize(name.size() - kPitchedSuffix.size());
        }

        auto idIter = nameToId.find(name);
        if (idIter == nameToId.end()) {
            SPDLOG_WARN("CustomSfx: '{}' does not match any sfx name, ignoring", path);
            free(fileList[i]);
            continue;
        }

        auto resource = std::static_pointer_cast<SOH::AudioSample>(
            Ship::Context::GetRawInstance()->GetResourceManager()->LoadResource(path));
        if (resource == nullptr) {
            SPDLOG_WARN("CustomSfx: failed to load sample '{}'", path);
            free(fileList[i]);
            continue;
        }

        CustomSfxEntry entry;
        entry.resource = resource;
        entry.pitched = pitched;
        sOverrides[NormalizeSfxId(idIter->second)] = std::move(entry);
        SPDLOG_INFO("CustomSfx: registered {} override for {}", pitched ? "pitched" : "pitch-locked", name);
        free(fileList[i]);
    }
    free(fileList);
}

static Instrument* GetOverrideInstrument(SequenceChannel* channel, int32_t instId, Instrument* vanilla) {
    int channelIdx = -1;
    for (int i = 0; i < 16; i++) {
        if (gAudioContext.seqPlayers[SEQ_PLAYER_SFX].channels[i] == channel) {
            channelIdx = i;
            break;
        }
    }
    if (channelIdx < 0) {
        return vanilla;
    }

    uint16_t sfxKey = NormalizeSfxId(sChannelSfx[channelIdx]);
    auto overrideIter = sOverrides.find(sfxKey);
    if (overrideIter == sOverrides.end()) {
        return vanilla;
    }
    if (sfxKey == NormalizeSfxId(OCARINA_SFX_ID) && instId != OCARINA_FONT_INSTRUMENT) {
        return vanilla;
    }

    CustomSfxEntry& entry = overrideIter->second;
    auto cloneIter = entry.clones.find(vanilla);
    if (cloneIter != entry.clones.end()) {
        return cloneIter->second;
    }

    Instrument* clone = new Instrument(*vanilla);
    SoundFontSample* sample = (SoundFontSample*)&entry.resource->sample;
    clone->lowNotesSound.sample = sample;
    clone->normalNotesSound.sample = sample;
    clone->highNotesSound.sample = sample;
    if (!entry.pitched) {
        // Play as recorded: streamed samples carry rate/32000 in the resource tuning, raw
        // binary samples are authored at the engine's 32 kHz.
        float ratio = entry.resource->tuning > 0.0f ? entry.resource->tuning : 1.0f;
        sLockedSounds[&clone->lowNotesSound] = ratio;
        sLockedSounds[&clone->normalNotesSound] = ratio;
        sLockedSounds[&clone->highNotesSound] = ratio;
    }
    entry.clones[vanilla] = clone;
    return clone;
}

static void RegisterCustomSfx() {
    ScanCustomSfx();

    COND_VB_SHOULD(VB_SFX_CHANNEL_START, !sOverrides.empty(), {
        SoundBankEntry* entry = va_arg(args, SoundBankEntry*);
        int32_t channelIdx = va_arg(args, int32_t);
        if (channelIdx >= 0 && channelIdx < 16) {
            sChannelSfx[channelIdx] = entry->sfxId;
        }
    });

    COND_VB_SHOULD(VB_SFX_USE_VANILLA_INSTRUMENT, !sOverrides.empty(), {
        SequenceChannel* channel = va_arg(args, SequenceChannel*);
        int32_t instId = va_arg(args, int32_t);
        Instrument** instOut = va_arg(args, Instrument**);
        if (channel->seqPlayer->playerIdx != SEQ_PLAYER_SFX) {
            return;
        }
        Instrument* replacement = GetOverrideInstrument(channel, instId, *instOut);
        if (replacement != *instOut) {
            *instOut = replacement;
            *should = false;
        }
    });

    COND_VB_SHOULD(VB_SFX_NOTE_USE_VANILLA_PITCH, !sOverrides.empty(), {
        NotePlaybackState* playbackState = va_arg(args, NotePlaybackState*);
        f32* frequency = va_arg(args, f32*);
        SequenceLayer* layer = playbackState->parentLayer;
        if (layer == NO_LAYER || layer == NULL || layer->channel == NULL ||
            layer->channel->seqPlayer->playerIdx != SEQ_PLAYER_SFX || layer->sound == NULL) {
            return;
        }
        auto lockedIter = sLockedSounds.find(layer->sound);
        if (lockedIter != sLockedSounds.end()) {
            // Play as recorded: nullifies the scripted note pitch, portamento and vibrato, but
            // keeps the channel-level frequency effects (per-play random pitch variation and
            // positional scaling) that vanilla applies per play.
            *frequency = lockedIter->second * layer->channel->freqScale;
            *should = false;
        }
    });
}

static RegisterShipInitFunc initFunc(RegisterCustomSfx);
