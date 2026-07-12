#pragma once

#include <nlohmann/json.hpp>
#include "game/Game.h"
#include "ai/Action.h"
#include "ai/GameStateAdapter.h"

// Serializes game state to the JSON format expected by the Python inference server.
namespace GameStateSerializer {

    // Produces a 2D array (tiles × 19 features) matching Python's tokenized_map().
    // Perspective is the player id whose visibility / tech is used.
    nlohmann::json tokenizedMapAsJson(const Game& game, PlayerId perspective);

    // Produces the obs dict (no tokenized_map inside).
    nlohmann::json observationAsJson(const Game& game, PlayerId perspective,
                                     const nlohmann::json& tokenizedMap);

    // Produces the actions array matching Python's legal_param_actions().
    nlohmann::json legalActionsAsJson(const Game& game, GameStateAdapter& adapter);

    // Produces the spec dict.
    nlohmann::json actionSpecAsJson(const Game& game);

    // Builds the full request JSON to send to the inference server.
    nlohmann::json buildRequest(const Game& game, GameStateAdapter& adapter);

} // namespace GameStateSerializer
