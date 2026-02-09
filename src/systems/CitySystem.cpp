//
// Created by Fryderyk Niedzwiecki on 06/02/2026.
//

#include "CitySystem.h"

#include "../game/Game.h"
#include "City.h"
#include "Map.h"

#include "systems/MonumentSystem.h"
#include "systems/UnitSystem.h"
#include "systems/PlayerSystem.h"
#include "systems/CitiesConnectionSystem.h"
#include "systems/VisionSystem.h"

#include "world/Tile.h"
#include "terrain/SettlementTypeEnum.h"

#include <algorithm>
#include <array>


bool CitySystem::cityExists(const Game& game, CityId cityId) {
    if (cityId == kNoCity) return false;
    return game.getCity(cityId) != nullptr;
}

bool CitySystem::setCityOwner(Game& game, CityId cityId, uint8_t ownerId) {
    if (cityId == kNoCity) return false;
    City* c = game.getCity(cityId);
    if (!c) return false;
    c->setOwnerId(ownerId);
    return true;
}

bool CitySystem::setCityName(Game& game, CityId cityId, const std::string& name) {
    if (cityId == kNoCity) return false;
    City* c = game.getCity(cityId);
    if (!c) return false;
    c->setName(name);
    return true;
}

bool CitySystem::setCityCapital(Game& game, CityId cityId, bool isCapital) {
    if (cityId == kNoCity) return false;
    City* c = game.getCity(cityId);
    if (!c) return false;
    c->setCapital(isCapital);
    return true;
}

bool CitySystem::setCityLevel(Game& game, CityId cityId, uint8_t level) {
    if (cityId == kNoCity) return false;
    City* c = game.getCity(cityId);
    if (!c) return false;
    c->setLevel(level);
    return true;
}

bool CitySystem::setCityPopulation(Game& game, CityId cityId, int16_t population) {
    if (cityId == kNoCity) return false;
    City* c = game.getCity(cityId);
    if (!c) return false;
    c->setPopulation(population);
    return true;
}

bool CitySystem::setCityStarsPerRound(Game& game, CityId cityId, uint8_t starsPerRound) {
    if (cityId == kNoCity) return false;
    City* c = game.getCity(cityId);
    if (!c) return false;
    c->setStarsPerRound(starsPerRound);
    return true;
}

bool CitySystem::addPopulation(Game& game, CityId cityId, uint16_t amount) {
    if (cityId == kNoCity) return false;
    City* c = game.getCity(cityId);
    if (!c) return false;

    const uint8_t oldLevel = c->getLevel();

    const bool ok = c->addPopulation(amount);
    if (!ok) return false;

    const uint8_t newLevel = c->getLevel();

    // Monument unlock tylko przy przejściu na 5+
    if (oldLevel < 5 && newLevel >= 5) {
        MonumentSystem::onCityReachedLevel5(game, c->getPos());
    }

    // If the city reached a new level threshold, force reward selection via the pending queue.
    if (newLevel > oldLevel) {
        const PlayerId owner = static_cast<PlayerId>(c->getOwnerId());
        game.enqueuePendingCityUpgrade(owner, cityId);
    }

    return true;
}

uint8_t CitySystem::getCityLevel(const Game& game, CityId cityId) {
    if (cityId == kNoCity) return 0;
    const City* c = game.getCity(cityId);
    if (!c) return 0;
    return c->getLevel();
}

int16_t CitySystem::getCityPopulation(const Game& game, CityId cityId) {
    if (cityId == kNoCity) return 0;
    const City* c = game.getCity(cityId);
    if (!c) return 0;
    return c->getPopulation();
}

uint8_t CitySystem::getCityOwner(const Game& game, CityId cityId) {
    if (cityId == kNoCity) return 0;
    const City* c = game.getCity(cityId);
    if (!c) return 0;
    return c->getOwnerId();
}

uint8_t CitySystem::getCityStarsPerRound(const Game& game, CityId cityId) {
    if (cityId == kNoCity) return 0;
    const City* c = game.getCity(cityId);
    if (!c) return 0;
    return c->getStarsPerRound();
}

uint8_t CitySystem::getCityUnitsCount(const Game& game, CityId cityId) {
    if (cityId == kNoCity) return 0;
    const City* c = game.getCity(cityId);
    if (!c) return 0;
    return c->getUnitsCount();
}

uint8_t CitySystem::getCityMaxUnitCapacity(const Game& game, CityId cityId) {
    if (cityId == kNoCity) return 0;
    const City* c = game.getCity(cityId);
    if (!c) return 0;
    return c->maxUnitCapacity();
}

Pos CitySystem::getCityPos(const Game& game, CityId cityId) {
    if (cityId == kNoCity) return Pos{-9999, -9999};
    const City* c = game.getCity(cityId);
    if (!c) return Pos{-9999, -9999};
    return c->getPos();
}

const std::string CitySystem::getCityName(const Game& game, CityId cityId) {
    static const std::string kEmpty;
    if (cityId == kNoCity) return kEmpty;
    const City* c = game.getCity(cityId);
    if (!c) return kEmpty;
    return c->getName();
}

CityId CitySystem::getCityId(const Game& game, CityId cityId) {
    if (cityId == kNoCity) return kNoCity;
    const City* c = game.getCity(cityId);
    if (!c) return kNoCity;
    return c->getCityId();
}

bool CitySystem::setCityPos(Game& game, CityId cityId, Pos pos) {
    if (cityId == kNoCity) return false;
    City* c = game.getCity(cityId);
    if (!c) return false;
    c->setPos(pos);
    return true;
}

bool CitySystem::setCityId(Game& game, CityId cityId, CityId newId) {
    if (cityId == kNoCity) return false;
    City* c = game.getCity(cityId);
    if (!c) return false;
    c->setCityId(newId);
    return true;
}

bool CitySystem::addUnitToCity(Game& game, UnitId unitId, CityId cityId,bool checkCapacity) {
    if (unitId == Map::kNoUnit) return false;
    if (cityId == kNoCity) return false;

    if (!UnitSystem::unitExists(game, unitId)) return false;

    City* city = game.getCity(cityId);
    if (!city) return false;

    if (checkCapacity) {
        if (city->getUnitsCount() >= city->maxUnitCapacity()) return false;
    }

    city->addUnit(unitId);
    return true;
}

bool CitySystem::removeUnitFromCity(Game& game, UnitId unitId, CityId cityId) {
    if (unitId == Map::kNoUnit) return false;
    if (cityId == kNoCity) return false;

    City* city = game.getCity(cityId);
    if (!city) return false;

    city->removeUnit(unitId);
    return true;
}

bool CitySystem::transferUnitBetweenCities(Game& game, UnitId unitId, CityId fromCityId, CityId toCityId) {
    if (unitId == Map::kNoUnit) return false;
    if (fromCityId == kNoCity || toCityId == kNoCity) return false;
    if (fromCityId == toCityId) return true;

    City* from = game.getCity(fromCityId);
    City* to   = game.getCity(toCityId);
    if (!from || !to) return false;

    from->removeUnit(unitId);

    if (to->getUnitsCount() >= to->maxUnitCapacity()) {
        from->addUnit(unitId);
        return false;
    }

    to->addUnit(unitId);
    return true;
}

// ===== Internal helpers (moved from Game.cpp) =====

void CitySystem::reassignUnitToCity(Game& game, UnitId uid, CityId newCityId, bool checkCapacity) {
    if (uid == kNoUnit) return;

    const size_t cityCount = game.getCities().size();

    // Always remove from any previous city lists.
    for (CityId cid = 0; cid < static_cast<CityId>(cityCount); ++cid) {
        (void)CitySystem::removeUnitFromCity(game, uid, cid);
    }

    // If no target city, stop here (unit remains unassigned to any city).
    if (newCityId == kNoCity) return;

    (void)CitySystem::addUnitToCity(game, uid, newCityId, checkCapacity);
}


CityId CitySystem::pickCityForConvertedUnit(const Game& game, PlayerId owner) {
    if (owner == kNoPlayer) return kNoCity;
    if (!PlayerSystem::playerExists(game, owner)) return kNoCity;

    const CityId capital = PlayerSystem::getCapitalId(game, owner);

    // Rule: if we have a capital, assign ONLY to the capital (ignore capacity).
    if (capital != kNoCity) {
        return capital;
    }

    auto hasCapacity = [&](CityId cid) -> bool {
        if (cid == kNoCity) return false;
        if (!CitySystem::cityExists(game, cid)) return false;
        if (static_cast<PlayerId>(CitySystem::getCityOwner(game, cid)) != owner) return false;
        return CitySystem::getCityUnitsCount(game, cid) < CitySystem::getCityMaxUnitCapacity(game, cid);
    };

    // Exception: if there is NO capital, pick any owned city with capacity from SOUTH to NORTH.
    const auto& owned = PlayerSystem::getCities(game, owner);
    if (owned.empty()) return kNoCity;

    std::vector<CityId> candidates;
    candidates.reserve(owned.size());
    for (CityId cid : owned) {
        if (!hasCapacity(cid)) continue;
        candidates.push_back(cid);
    }

    if (candidates.empty()) return kNoCity;

    // SOUTH first: larger y, tie-break x ascending.
    std::sort(candidates.begin(), candidates.end(), [&](CityId a, CityId b) {
        const Pos pa = CitySystem::getCityPos(game, a);
        const Pos pb = CitySystem::getCityPos(game, b);
        if (pa.y != pb.y) return pa.y > pb.y;
        return pa.x < pb.x;
    });

    return candidates.front();
}


void CitySystem::claimFreeTerritoryRadius(Game& game, CityId cid, SettlementId citySid, Pos center, int radius) {
    if (cid == kNoCity) return;

    Map& map = game.getMap();

    auto tryClaim = [&](Pos p) {
        if (!map.inBounds(p)) return;

        Tile& tt = map.at(p);

        if (tt.getTerritoryCityId() != kNoCity) return;

        // Safety: don't claim another city's center tile if its settlement id differs.
        if (tt.getSettlementType() == SettlementTypeEnum::City) {
            const SettlementId otherSid = tt.getSettlementId();
            if (otherSid != kNoSettlement && otherSid != citySid) return;
        }

        tt.setTerritoryCityId(cid);
        const PlayerId owner = static_cast<PlayerId>(CitySystem::getCityOwner(game, cid));
        if (owner != kNoPlayer) {
            VisionSystem::revealTile(game, owner, p, RevealSource::Initial);
        }
    };

    // Ensure center itself is assigned (same safety rule as before)
    if (map.inBounds(center)) {
        Tile& ct = map.at(center);

        if (ct.getSettlementType() == SettlementTypeEnum::City) {
            const SettlementId otherSid = ct.getSettlementId();
            if (otherSid != kNoSettlement && otherSid != citySid) {
                return;
            }
        }

        ct.setTerritoryCityId(cid);
        const PlayerId owner = static_cast<PlayerId>(CitySystem::getCityOwner(game, cid));
        if (owner != kNoPlayer) {
            VisionSystem::revealTile(game, owner, center, RevealSource::Initial);
        }
    }

    // Clamp radius and claim a square around center (including diagonals)
    if (radius < 0) radius = 0;

    for (int dy = -radius; dy <= radius; ++dy) {
        for (int dx = -radius; dx <= radius; ++dx) {
            if (dx == 0 && dy == 0) continue;
            tryClaim(Pos{
                static_cast<int16_t>(center.x + dx),
                static_cast<int16_t>(center.y + dy)
            });
        }
    }
}

void CitySystem::claimFreeTerritoryRadius1(Game& game, CityId cid, SettlementId citySid, Pos center) {
    claimFreeTerritoryRadius(game, cid, citySid, center, 1);
}


// ===== NEW: initCapital =====

bool CitySystem::initCapital(Game& game, PlayerId owner, CityId cid, Pos capPos) {
    Map& map = game.getMap();
    if (cid == kNoCity) return false;
    if (!map.inBounds(capPos)) return false;
    if (!CitySystem::cityExists(game, cid)) return false;

    (void)CitySystem::setCityOwner(game, cid, static_cast<uint8_t>(owner));
    (void)CitySystem::setCityPos(game, cid, capPos);
    (void)CitySystem::setCityCapital(game, cid, true);

    const SettlementId sid = static_cast<SettlementId>(cid);

    Tile& t = map.at(capPos);
    t.setSettlement(SettlementTypeEnum::City, sid);

    CitySystem::claimFreeTerritoryRadius1(game, cid, sid, capPos);

    // ensure player has this city + capital set
    {
        const auto& cs = PlayerSystem::getCities(game, owner);
        if (std::find(cs.begin(), cs.end(), cid) == cs.end()) {
            PlayerSystem::addCity(game, owner, cid);
        }
        if (PlayerSystem::getCapitalId(game, owner) == kNoCity) {
            PlayerSystem::setCapitalId(game, owner, cid);
        }
    }

    return true;
}

// ===== High-level city actions =====

bool CitySystem::canFoundCityFromVillage(const Game& game, PlayerId owner, Pos pos) {
    (void)owner;
    const Map& map = game.getMap();
    if (!map.inBounds(pos)) return false;

    const Tile& tile = map.at(pos);
    const SettlementTypeEnum st = tile.getSettlementType();
    if (st != SettlementTypeEnum::Village && st != SettlementTypeEnum::City) return false;

    const SettlementId sid = tile.getSettlementId();
    if (sid != kNoSettlement) {
        const CityId cid = static_cast<CityId>(sid);
        if (static_cast<size_t>(cid) < game.getCities().size()) {
            const City* c = game.getCity(cid);
            if (c && static_cast<PlayerId>(c->getOwnerId()) != owner) return false;
        }
    }
    return true;
}

bool CitySystem::foundCityFromVillage(Game& game, PlayerId owner, Pos pos) {
    if (!canFoundCityFromVillage(game, owner, pos)) return false;

    Map& map = game.getMap();

    Tile& tile = map.at(pos);

    SettlementId sid = tile.getSettlementId();
    CityId cid = kNoCity;

    if (sid != kNoSettlement) {
        cid = static_cast<CityId>(sid);
        if (static_cast<size_t>(cid) >= game.getCities().size()) {
            game.getCities().resize(static_cast<size_t>(cid) + 1);
        }
    } else {
        cid = static_cast<CityId>(game.getCities().size());
        game.getCities().resize(game.getCities().size() + 1);
        sid = static_cast<SettlementId>(cid);
    }

    (void)CitySystem::setCityId(game, cid, cid);
    (void)CitySystem::setCityOwner(game, cid, static_cast<uint8_t>(owner));
    (void)CitySystem::setCityLevel(game, cid, 1);
    (void)CitySystem::setCityPopulation(game, cid, 0);
    (void)CitySystem::setCityStarsPerRound(game, cid, 1);
    (void)CitySystem::setCityCapital(game, cid, false);

    if (CitySystem::getCityName(game, cid).empty()) {
        (void)CitySystem::setCityName(game, cid, std::string("City ") + std::to_string(static_cast<int>(cid)));
    }

    (void)CitySystem::setCityPos(game, cid, pos);

    tile.setSettlement(SettlementTypeEnum::City, sid);

    CitySystem::claimFreeTerritoryRadius1(game, cid, sid, pos);

    {
        const auto& cs = PlayerSystem::getCities(game, owner);
        if (std::find(cs.begin(), cs.end(), cid) == cs.end()) {
            PlayerSystem::addCity(game, owner, cid);
        }
        if (PlayerSystem::getCapitalId(game, owner) == kNoCity) {
            PlayerSystem::setCapitalId(game, owner, cid);
        }
    }

    {
        const UnitId uOn = map.unitOn(pos);
        if (uOn != Map::kNoUnit) {
            CitySystem::reassignUnitToCity(game, uOn, cid);
        }
    }

    CitiesConnectionSystem::update(game);
    return true;
}

bool CitySystem::canCaptureCityAt(const Game& game, PlayerId newOwner, Pos pos) {
    const Map& map = game.getMap();
    if (!map.inBounds(pos)) return false;

    const Tile& tile = map.at(pos);
    if (tile.getSettlementType() != SettlementTypeEnum::City) return false;

    const CityId cid = static_cast<CityId>(tile.getSettlementId());
    if (!CitySystem::cityExists(game, cid)) return false;

    const PlayerId oldOwner = static_cast<PlayerId>(CitySystem::getCityOwner(game, cid));
    if (oldOwner == newOwner) return false;
    return true;
}

bool CitySystem::captureCityAt(Game& game, PlayerId newOwner, Pos pos) {
    if (!canCaptureCityAt(game, newOwner, pos)) return false;

    Map& map = game.getMap();

    Tile& tile = map.at(pos);
    const CityId cid = static_cast<CityId>(tile.getSettlementId());
    const PlayerId oldOwner = static_cast<PlayerId>(CitySystem::getCityOwner(game, cid));

    (void)CitySystem::setCityOwner(game, cid, static_cast<uint8_t>(newOwner));
    (void)CitySystem::setCityPos(game, cid, pos);

    PlayerSystem::removeCity(game, oldOwner, cid);
    PlayerSystem::addCity(game, newOwner, cid);

    {
        const UnitId uOn = map.unitOn(pos);
        if (uOn != Map::kNoUnit) {
            // Keep default behavior: respect capacity when capturing.
            CitySystem::reassignUnitToCity(game, uOn, cid, true);
        }
    }

    tile.setSettlement(SettlementTypeEnum::City, tile.getSettlementId());

    CitySystem::claimFreeTerritoryRadius1(game, cid, tile.getSettlementId(), pos);

    CitiesConnectionSystem::update(game);
    return true;
}

bool CitySystem::isCityUnderSiege(const Game& game, CityId cityId) {
    if (cityId == kNoCity) return false;
    const City* c = game.getCity(cityId);
    if (!c) return false;

    const Pos p = c->getPos();
    const Map& map = game.getMap();
    if (!map.inBounds(p)) return false;

    const UnitId u = map.unitOn(p);
    if (u == Map::kNoUnit) return false;
    if (!UnitSystem::unitExists(game, u)) return false;

    const PlayerId cityOwner = static_cast<PlayerId>(c->getOwnerId());
    const PlayerId unitOwner = UnitSystem::getOwnerId(game, u);
    return (unitOwner != kNoPlayer && cityOwner != kNoPlayer && unitOwner != cityOwner);
}


bool CitySystem::getCityIsInfiltrated(const Game& game, CityId cityId) {
    if (cityId == kNoCity) return false;
    const City* c = game.getCity(cityId);
    if (!c) return false;
    return c->getIsInfiltrated();
}

bool CitySystem::setCityIsInfiltrated(Game& game, CityId cityId, bool v) {
    if (cityId == kNoCity) return false;
    City* c = game.getCity(cityId);
    if (!c) return false;
    c->setIsInfiltrated(v);
    return true;
}

void CitySystem::blockCityIncomeNextOwnerTurn(Game& game, CityId cityId) {
    if (!CitySystem::cityExists(game, cityId)) return;

    // ustaw flagę infiltracji → income zostanie zablokowany przy następnym owner turn
    CitySystem::setCityIsInfiltrated(game, cityId, true);
}

int CitySystem::consumeAndGetCityIncomeForOwnerTurn(Game& game, PlayerId owner, CityId cityId) {
    if (!CitySystem::cityExists(game, cityId)) return 0;
    if (owner == kNoPlayer) return 0;

    const PlayerId realOwner =
        static_cast<PlayerId>(CitySystem::getCityOwner(game, cityId));

    // tylko owner może pobrać income
    if (realOwner != owner) return 0;

    // jeżeli miasto było infiltrowane → blokuj income tylko raz
    if (CitySystem::getCityIsInfiltrated(game, cityId)) {
        CitySystem::setCityIsInfiltrated(game, cityId, false); // consume flag
        return 0;
    }

    return static_cast<int>(
        CitySystem::getCityStarsPerRound(game, cityId)
    );
}

void CitySystem::removeUnitFromAnyCity(Game& game, UnitId unitId) {
    // removeUnitFromCity powinno być bezpieczne jako no-op, jeśli unit nie należy do danego miasta.
    for (auto& city : game.getCities()) {
        CitySystem::removeUnitFromCity(game, unitId, city.getCityId());
    }
}

uint8_t CitySystem::getCityParkCount(const Game& game, CityId cityId) {
    if (cityId == kNoCity) return 0;
    const City* c = game.getCity(cityId);
    if (!c) return 0;
    return c->getParkCount();
}

bool CitySystem::setCityParkCount(Game& game, CityId cityId, uint8_t v) {
    if (cityId == kNoCity) return false;
    City* c = game.getCity(cityId);
    if (!c) return false;
    c->setParkCount(v);
    return true;
}

bool CitySystem::addCityParkCount(Game& game, CityId cityId, uint8_t delta) {
    if (delta == 0) return true;
    if (cityId == kNoCity) return false;
    City* c = game.getCity(cityId);
    if (!c) return false;

    // Saturating add (avoid uint8 overflow)
    const int cur = static_cast<int>(c->getParkCount());
    const int nxt = std::clamp(cur + static_cast<int>(delta), 0, 255);
    c->setParkCount(static_cast<uint8_t>(nxt));
    return true;
}

bool CitySystem::setCityWallEnabled(Game& game, CityId cityId, bool enabled) {
    if (cityId == kNoCity) return false;
    City* c = game.getCity(cityId);
    if (!c) return false;
    c->setCityWallEnabled(enabled);
    return true;
}

bool CitySystem::setCityWorkshopEnabled(Game& game, CityId cityId, bool enabled) {
    if (cityId == kNoCity) return false;
    City* c = game.getCity(cityId);
    if (!c) return false;
    c->setWorkshopEnabled(enabled);
    return true;
}

City* CitySystem::getCityBySettlementId(Game& game, SettlementId sid) {
    if (sid == kNoSettlement) return nullptr;

    const CityId cid = static_cast<CityId>(sid);
    if (static_cast<size_t>(cid) >= game.getCities().size()) return nullptr;

    return game.getCity(cid);
}

const City* CitySystem::getCityBySettlementId(const Game& game, SettlementId sid) {
    if (sid == kNoSettlement) return nullptr;

    const CityId cid = static_cast<CityId>(sid);
    if (static_cast<size_t>(cid) >= game.getCities().size()) return nullptr;

    return game.getCity(cid);
}
