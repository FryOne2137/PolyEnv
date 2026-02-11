#ifndef GAME_ENGINE_SETTLEMENT_H
#define GAME_ENGINE_SETTLEMENT_H

#include "Pos.h"
#include "terrain/SettlementId.h"

class Settlement {
public:
    SettlementId getId() const ;
    Pos getPos() const ;

    void setId(SettlementId v) { id = v; }
    void setPos(Pos p) { pos = p; }

protected:
    Pos pos{};
    SettlementId id = kNoSettlement;
};

#endif