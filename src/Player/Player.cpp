//
// Created by Fryderyk Niedzwiecki on 11/01/2026.
//

#include "Player.h"

bool Player::addStars(int starAmount) {
    Player::starBalance+=starAmount;
    return true;
}

bool Player::collectStarsFromCities() {
    int starAmount = 0;

    for (City* city : cities) {
        starAmount+=city->getStarsPerRound();
    }

    starBalance+=starAmount;
    return true;
}

bool Player::hasTech(const Tech* tech) const {
    for (const Tech* t : techs) {
        if (t == tech) return true;
    }
    return false;
}

bool Player::buyTech(const Tech* tech) {

    if (!tech) return false;
    if (hasTech(tech)) return false;

    if (starBalance < tech->getPrice(techs.size())) return false;

    if (tech->hasPrevious()) {
        const Tech* prev = tech->getPrevious();
        if (prev && !hasTech(prev)) {
            return false;
        }
    }

    techs.push_back(const_cast<Tech*>(tech));
    return true;
}