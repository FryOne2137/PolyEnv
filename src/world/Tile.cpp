//
// Created by Fryderyk Niedzwiecki on 11/01/2026.
//

#include "Tile.h"

void Tile::removeResourceFlag(ResourcesEnum flag) {
    if (resource == flag) {
        resource = ResourcesEnum::None;
    }
}

void Tile::addResourceFlag(ResourcesEnum flag) {
    if (flag != ResourcesEnum::None) {
        resource = flag;
    }
}

bool Tile::hasResourceFlag(ResourcesEnum flag) const {
    return resource == flag;
}
