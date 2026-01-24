//
// Created by Fryderyk Niedzwiecki on 15/01/2026.
//

#include "InteractionSystem.h"

#include "Game.h"
#include "World/Map.h"
#include "World/Tile.h"
#include "Player/Player.h"

#include "terrain/ResourcesEnum.h"
#include "terrain/SettlementTypeEnum.h"

static constexpr int kStarfishReward = 8;

void InteractionSystem::onUnitEnteredTile(Game& game, UnitId unitId, Pos pos) {
    Unit* unit = game.getUnit(unitId);
    if (!unit) return;

    // Bezpieczeństwo: Interaction odpalamy po udanym ruchu, ale sprawdźmy bounds
    if (!game.getMap().inBounds(pos)) return;

    // Kolejność nieprzypadkowa:
    // 1) rzeczy “zbieralne” (starfish)
    // 2) rzeczy “statusowe” (ruin)
    // 3) rzeczy zmieniające kontrolę / struktury (village->city)
    handleStarfish(game, unitId, pos);
    handleRuin(game, unitId, pos);
    handleVillage(game, unitId, pos);
}

void InteractionSystem::handleStarfish(Game& game, UnitId unitId, Pos pos) {
    Unit* unit = game.getUnit(unitId);
    if (!unit) return;

    Tile& tile = game.getMap().at(pos);

    if (tile.getSettlementType() != SettlementTypeEnum::Starfish) return;
    // Nagroda
    game.getPlayer(unit->getOwnerId()).addStars(kStarfishReward);

    // Usuń starfish z mapy, żeby nie dało się zebrać drugi raz
    tile.setResource(ResourcesEnum::None);
}

void InteractionSystem::handleRuin(Game& game, UnitId unitId, Pos pos) {
    // U Ciebie w generatorze ruiny mogą być jako SettlementTypeEnum::Ruin
    // albo osobny “above layer”. W obecnej architekturze najlepiej:
    // - trzymać ruin jako settlementType
    // - a logikę nagrody robić tutaj, ale finalnie wywołać helper w Game
    //
    // Żeby to było czyste i nie mieszać zależności, polecam w Game dodać:
    //   bool Game::tryResolveRuin(UnitId unitId, Pos pos);
    // i tu tylko:
    //
    // if (game.tryResolveRuin(unitId, pos)) { ... }
    //
    // Na razie zostawiam jako TODO, bo Twoje enumy/Tile API mogą się jeszcze różnić.
    (void)game;
    (void)unitId;
    (void)pos;
}

void InteractionSystem::handleVillage(Game& game, UnitId unitId, Pos pos) {
    Unit* unit = game.getUnit(unitId);
    if (!unit) return;

    Tile& tile = game.getMap().at(pos);

    if (tile.getSettlementType() != SettlementTypeEnum::Village) return;

    // Convert Village -> City on this tile
    const PlayerId owner = unit->getOwnerId();

    // 1) Change tile settlement type (id is assigned in foundCityFromVillage)
    tile.setSettlement(SettlementTypeEnum::City, kNoSettlement);

    // 2) Create/register City for this tile
    game.foundCityFromVillage(owner, pos);

    // 3) Consumes the whole turn: unit cannot move anymore this round
    unit->setMovedThisTurn(true);
}