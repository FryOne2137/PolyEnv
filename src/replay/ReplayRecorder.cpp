#include "ReplayRecorder.h"

#include <fstream>
#include <limits>
#include <nlohmann/json.hpp>

namespace {
constexpr const char* kReplayFormat = "polyenv-game";
constexpr int kReplayFormatVersion = 3;
constexpr const char* kReplayRuleset = "polyenv-2026-07";

bool validTribeId(int tribeId) {
    return tribeId >= 1 && tribeId <= 12;
}

bool validMapType(uint8_t mapType) {
    return mapType <= 1; // 0 = Lakes, 1 = Drylands
}
} // namespace

bool ReplayRecorder::save(
    const std::string& path,
    const ReplayMetadata& metadata,
    const std::vector<size_t>& actionIds,
    std::string& error
) {
    if (path.empty()) {
        error = "replay path is empty";
        return false;
    }
    if (metadata.seed == 0 || metadata.mapSize <= 0 || metadata.tribes.empty() ||
        !validMapType(metadata.mapType)) {
        error = "invalid replay metadata";
        return false;
    }

    try {
        nlohmann::json replay;
        replay["format"] = kReplayFormat;
        replay["format_version"] = kReplayFormatVersion;
        replay["engine_version"] = "0.2.0";
        replay["ruleset"] = kReplayRuleset;
        replay["seed"] = metadata.seed;
        replay["map_size"] = metadata.mapSize;
        replay["tribes"] = metadata.tribes;
        replay["actions"] = actionIds;
        replay["map_generation"] = {
            {"map_type", metadata.mapType},
        };

        std::ofstream out(path);
        if (!out) {
            error = "cannot open replay for writing";
            return false;
        }
        out << replay.dump(2) << '\n';
        return true;
    } catch (const std::exception& exception) {
        error = exception.what();
        return false;
    }
}

bool ReplayRecorder::load(
    const std::string& path,
    ReplayMetadata& metadata,
    std::vector<size_t>& actionIds,
    std::string& error
) {
    try {
        std::ifstream in(path);
        if (!in) {
            error = "cannot open replay";
            return false;
        }
        const nlohmann::json replay = nlohmann::json::parse(in);
        if (replay.value("format", "") != kReplayFormat) {
            error = "unsupported .polygame format";
            return false;
        }

        const int formatVersion = replay.value("format_version", 0);
        if (formatVersion == 1) {
            error = "legacy .polygame v1 is incompatible with the current map generator and action space; "
                    "open it with the engine version that created it or create a new replay with the current engine";
            return false;
        }
        if (formatVersion != kReplayFormatVersion) {
            error = "unsupported .polygame format version";
            return false;
        }

        const std::string ruleset = replay.value("ruleset", "");
        if (ruleset != kReplayRuleset) {
            error = "replay ruleset is incompatible with this engine";
            return false;
        }

        metadata.formatVersion = formatVersion;
        metadata.ruleset = ruleset;
        metadata.seed = replay.at("seed").get<uint32_t>();
        metadata.mapSize = replay.at("map_size").get<int>();
        metadata.tribes = replay.at("tribes").get<std::vector<int>>();
        actionIds = replay.at("actions").get<std::vector<size_t>>();
        if (metadata.seed == 0 || metadata.mapSize <= 0 || metadata.tribes.empty()) {
            error = "invalid replay header";
            return false;
        }
        for (const int tribeId : metadata.tribes) {
            if (!validTribeId(tribeId)) {
                error = "invalid tribe id";
                return false;
            }
        }

        if (replay.contains("map_generation")) {
            const nlohmann::json& generation = replay.at("map_generation");
            const int mapType = generation.value("map_type", static_cast<int>(metadata.mapType));
            if (mapType < 0 || mapType > 1) {
                error = "invalid map type";
                return false;
            }
            metadata.mapType = static_cast<uint8_t>(mapType);
        }
        return true;
    } catch (const std::exception& exception) {
        error = exception.what();
        return false;
    }
}
