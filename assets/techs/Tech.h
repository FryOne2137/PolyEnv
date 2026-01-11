//
// Created by Fryderyk Niedzwiecki on 11/01/2026.
//

#ifndef GAME_ENGINE_TECH_H
#define GAME_ENGINE_TECH_H
#include <string>


class Tech {
public:
    bool hasPrevious() const;
    const Tech* getPrevious() const;
    int getTier() const;
    int getPrice(int numOfCities);
    std::string getName() const;
    Tech() = default;


protected:
    int tier = 0;

    const Tech *previousTech = nullptr;
    Tech(int tier, const Tech* previous);


    std::string name="Base";
};

#endif // GAME_ENGINE_TECH_H