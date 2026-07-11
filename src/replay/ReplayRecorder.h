#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

struct ReplayMetadata {
    int formatVersion = 2;
    uint32_t seed = 1;
    int mapSize = 11;
    std::vector<int> tribes;
    float initialLand = 0.5f;
    int smoothing = 3;
    int relief = 4;
    std::string ruleset;
};

class ReplayRecorder {
public:
    void clear() { actionIds_.clear(); }
    void record(size_t actionId) { actionIds_.push_back(actionId); }
    void replace(std::vector<size_t> actionIds) { actionIds_ = std::move(actionIds); }
    const std::vector<size_t>& actionIds() const { return actionIds_; }

    static bool save(
        const std::string& path,
        const ReplayMetadata& metadata,
        const std::vector<size_t>& actionIds,
        std::string& error
    );
    static bool load(
        const std::string& path,
        ReplayMetadata& metadata,
        std::vector<size_t>& actionIds,
        std::string& error
    );

private:
    std::vector<size_t> actionIds_;
};
