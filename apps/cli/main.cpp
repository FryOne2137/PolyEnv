#include <iostream>
#include <vector>

#include "game/Game.h"

int main() {
    Game game;
    Game::NewGameConfig cfg;
    cfg.mapSize = 16;
    cfg.seed = 1;
    cfg.tribes = {TribeType::Bardur, TribeType::Imperius};

    game.newGame(cfg);

    std::cout << "Started game. players=" << game.getPlayers().size()
              << " map=" << game.getMap().getWidth() << "x" << game.getMap().getHeight()
              << " turn=" << game.getTurnNumber() << "\n";

    return 0;
}
