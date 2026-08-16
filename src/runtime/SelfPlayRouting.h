#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <vector>

namespace polyenv::selfplay {

inline int32_t agentFor(const std::vector<int32_t>& assignments,
                        size_t envIndex,
                        int player,
                        size_t playerCount) {
    if (player < 0 || static_cast<size_t>(player) >= playerCount) return -1;
    if (playerCount == 0 || assignments.size() % playerCount != 0 ||
        envIndex >= assignments.size() / playerCount) {
        throw std::logic_error("invalid SelfPlayPool agent assignment table");
    }
    return assignments[envIndex * playerCount + static_cast<size_t>(player)];
}

inline void setAssignment(std::vector<int32_t>& assignments,
                          size_t envIndex,
                          const int32_t* agentIds,
                          size_t playerCount) {
    if (playerCount == 0 || assignments.size() % playerCount != 0 ||
        envIndex >= assignments.size() / playerCount) {
        throw std::logic_error("invalid SelfPlayPool agent assignment table");
    }
    if (std::any_of(agentIds, agentIds + playerCount, [](int32_t id) { return id < 0; })) {
        throw std::invalid_argument("agent ids must be non-negative");
    }
    std::copy_n(agentIds, playerCount, assignments.begin() + envIndex * playerCount);
}

}  // namespace polyenv::selfplay
