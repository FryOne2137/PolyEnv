#include "ModelClient.h"

#include <iostream>
#include <stdexcept>
#include <string>

#include <nlohmann/json.hpp>

#include "GameStateSerializer.h"

using json = nlohmann::json;

// ── ZeroMQ-backed implementation ─────────────────────────────────────────────
#if defined(GAME_ENGINE_HAS_ZMQ) && GAME_ENGINE_HAS_ZMQ

#include <zmq.h>

ModelClient::ModelClient(const std::string& endpoint)
    : endpoint_(endpoint)
{
    zmqCtx_ = zmq_ctx_new();
    if (!zmqCtx_)
        throw BotClientError("Failed to create ZeroMQ context");
}

ModelClient::~ModelClient() {
    if (zmqCtx_) {
        zmq_ctx_destroy(zmqCtx_);
        zmqCtx_ = nullptr;
    }
}

int ModelClient::queryAction(const Game& game, GameStateAdapter& adapter) {
    const json req = GameStateSerializer::buildRequest(game, adapter);
    const std::string reqStr = req.dump();

    constexpr int kMaxAttempts = 3;

    for (int attempt = 1; attempt <= kMaxAttempts; ++attempt) {
        void* sock = zmq_socket(zmqCtx_, ZMQ_REQ);
        if (!sock)
            throw BotClientError("zmq_socket() failed");

        const int linger = 0;
        zmq_setsockopt(sock, ZMQ_LINGER,   &linger,     sizeof(linger));
        zmq_setsockopt(sock, ZMQ_RCVTIMEO, &timeoutMs_, sizeof(timeoutMs_));

        if (zmq_connect(sock, endpoint_.c_str()) != 0) {
            zmq_close(sock);
            throw BotClientError("zmq_connect() failed for " + endpoint_);
        }

        if (zmq_send(sock, reqStr.data(), reqStr.size(), 0) < 0) {
            zmq_close(sock);
            std::cerr << "[bot] zmq_send failed on attempt " << attempt << "\n";
            continue;
        }

        zmq_msg_t reply;
        zmq_msg_init(&reply);
        const int rc = zmq_msg_recv(&reply, sock, 0);
        zmq_close(sock);

        if (rc < 0) {
            zmq_msg_close(&reply);
            std::cerr << "[bot] Timeout/recv error on attempt " << attempt
                      << "/" << kMaxAttempts << "\n";
            continue;
        }

        const std::string replyStr(static_cast<const char*>(zmq_msg_data(&reply)),
                                   zmq_msg_size(&reply));
        zmq_msg_close(&reply);

        json resp;
        try { resp = json::parse(replyStr); }
        catch (const json::exception& e) {
            throw BotClientError(std::string("JSON parse error: ") + e.what());
        }

        if (resp.contains("error"))
            throw BotClientError("Server error: " + resp["error"].get<std::string>());

        // Prefer resp["action"]["action_id"] — the action-space ID sent in the request.
        // (resp["action_index"] is the raw argmax array-index, not the action-space ID.)
        if (resp.contains("action") && resp["action"].is_object()
                && resp["action"].contains("action_id"))
            return resp["action"]["action_id"].get<int>();

        // Legacy fallback: server returned only action_index without the action dict.
        if (!resp.contains("action_index"))
            throw BotClientError("Response missing 'action_index' and 'action'");
        return resp["action_index"].get<int>();
    }

    throw BotClientError("Model server did not respond after " +
                         std::to_string(kMaxAttempts) + " attempts");
}

// ── Stub when ZMQ is not available ───────────────────────────────────────────
#else

ModelClient::ModelClient(const std::string& endpoint)
    : endpoint_(endpoint)
{
    throw BotClientError(
        "Bot client is disabled: ZeroMQ was not found at compile time. "
        "Install libzmq (brew install zeromq) and rebuild.");
}

ModelClient::~ModelClient() {}

int ModelClient::queryAction(const Game& /*game*/, GameStateAdapter& /*adapter*/) {
    throw BotClientError("Bot client not available (compiled without ZeroMQ).");
}

#endif
