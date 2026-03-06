#pragma once

#include <stdexcept>
#include <string>

#include "game/Game.h"
#include "ai/GameStateAdapter.h"

// Exception thrown when the model server is unreachable or returns an error.
class BotClientError : public std::runtime_error {
public:
    explicit BotClientError(const std::string& msg) : std::runtime_error(msg) {}
};

// ZeroMQ REQ-REP client that forwards game state to the Python inference server.
//
// Protocol:
//   C++ → server:  {"map_tokens": ..., "obs": ..., "actions": [...], "spec": ...}
//   server → C++:  {"action_index": <action_id from the actions list>}
//
// The server receives the full legal action list, picks one (argmax),
// and returns its action_id. The action is always legal by construction.
class ModelClient {
public:
    // endpoint: ZeroMQ address, e.g. "tcp://localhost:5555"
    explicit ModelClient(const std::string& endpoint);
    ~ModelClient();

    // Send current game state, get back the chosen action_id.
    // Throws BotClientError if the server is unreachable or returns an error.
    int queryAction(const Game& game, GameStateAdapter& adapter);

private:
    std::string endpoint_;
    void*       zmqCtx_    = nullptr;
    int         timeoutMs_ = 10000;  // 10 s receive timeout
};
