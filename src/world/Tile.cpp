//
// Created by Fryderyk Niedzwiecki on 11/01/2026.
//

#include "Tile.h"

void Tile::removeResourceFlag(ResourcesEnum flag) {
    const uint8_t nr = static_cast<uint8_t>(resource) & ~static_cast<uint8_t>(flag);
    resource = static_cast<ResourcesEnum>(nr);
}

void Tile::addResourceFlag(ResourcesEnum flag) {
    const uint8_t nr = static_cast<uint8_t>(resource) | static_cast<uint8_t>(flag);
    resource = static_cast<ResourcesEnum>(nr);
}

bool Tile::hasResourceFlag(ResourcesEnum flag) const {
    return (static_cast<uint8_t>(resource) & static_cast<uint8_t>(flag)) != 0;
}