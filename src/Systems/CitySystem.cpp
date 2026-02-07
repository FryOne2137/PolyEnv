//
// Created by Fryderyk Niedzwiecki on 06/02/2026.
//

#include "CitySystem.h"

#include "Game.h"
#include "City.h"
#include "Map.h"

#include "Systems/MonumentSystem.h"
#include "Systems/UnitSystem.h"
#include "Systems/PlayerSystem.h"
#include "Systems/CitiesConnectionSystem.h"

#include "World/Tile.h"
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

bool CitySystem::addUnitToCity(Game& game, UnitId unitId, CityId cityId) {
    if (unitId == Map::kNoUnit) return false;
    if (cityId == kNoCity) return false;

    if (!UnitSystem::unitExists(game, unitId)) return false;

    City* city = game.getCity(cityId);
    if (!city) return false;

    if (city->getUnitsCount() >= city->maxUnitCapacity()) return false;

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

void CitySystem::reassignUnitToCity(Game& game, UnitId uid, CityId newCityId) {
    if (uid == kNoUnit || newCityId == kNoCity) return;

    const size_t cityCount = game.getCities().size();
    for (CityId cid = 0; cid < static_cast<CityId>(cityCount); ++cid) {
        (void)CitySystem::removeUnitFromCity(game, uid, cid);
    }
    (void)CitySystem::addUnitToCity(game, uid, newCityId);
}

void CitySystem::claimFreeTerritoryRadius1(Game& game, CityId cid, SettlementId citySid, Pos center) {
    if (cid == kNoCity) return;

    Map& map = game.getMap();

    auto tryClaim = [&](Pos p) {
        if (!map.inBounds(p)) return;

        Tile& tt = map.at(p);

        if (tt.getTerritoryCityId() != kNoCity) return;

        if (tt.getSettlementType() == SettlementTypeEnum::City) {
            const SettlementId otherSid = tt.getSettlementId();
            if (otherSid != kNoSettlement && otherSid != citySid) return;
        }

        tt.setTerritoryCityId(cid);
    };

    if (map.inBounds(center)) {
        Tile& ct = map.at(center);

        if (ct.getSettlementType() == SettlementTypeEnum::City) {
            const SettlementId otherSid = ct.getSettlementId();
            if (otherSid != kNoSettlement && otherSid != citySid) {
                return;
            }
        }

        ct.setTerritoryCityId(cid);
    }

    static const std::array<Pos, 8> kNb = {
        Pos{ 1, 0}, Pos{-1, 0}, Pos{0, 1}, Pos{0,-1},
        Pos{ 1, 1}, Pos{ 1,-1}, Pos{-1, 1}, Pos{-1,-1}
    };

    for (const Pos d : kNb) {
        tryClaim(Pos{center.x + d.x, center.y + d.y});
    }
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

bool CitySystem::foundCityFromVillage(Game& game, PlayerId owner, Pos pos) {
    Map& map = game.getMap();
    if (!map.inBounds(pos)) return false;

    Tile& tile = map.at(pos);

    const SettlementTypeEnum st = tile.getSettlementType();
    if (st != SettlementTypeEnum::Village && st != SettlementTypeEnum::City) return false;

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

bool CitySystem::captureCityAt(Game& game, PlayerId newOwner, Pos pos) {
    Map& map = game.getMap();
    if (!map.inBounds(pos)) return false;

    Tile& tile = map.at(pos);
    if (tile.getSettlementType() != SettlementTypeEnum::City) return false;

    const CityId cid = static_cast<CityId>(tile.getSettlementId());
    if (!CitySystem::cityExists(game, cid)) return false;

    const PlayerId oldOwner = static_cast<PlayerId>(CitySystem::getCityOwner(game, cid));
    if (oldOwner == newOwner) return false;

    (void)CitySystem::setCityOwner(game, cid, static_cast<uint8_t>(newOwner));
    (void)CitySystem::setCityPos(game, cid, pos);

    PlayerSystem::removeCity(game, oldOwner, cid);
    PlayerSystem::addCity(game, newOwner, cid);

    {
        const UnitId uOn = map.unitOn(pos);
        if (uOn != Map::kNoUnit) {
            CitySystem::reassignUnitToCity(game, uOn, cid);
        }
    }

    tile.setSettlement(SettlementTypeEnum::City, tile.getSettlementId());

    CitySystem::claimFreeTerritoryRadius1(game, cid, tile.getSettlementId(), pos);

    CitiesConnectionSystem::update(game);
    return true;
}