//
// Created by Fryderyk Niedzwiecki on 11/01/2026.
//

#ifndef GAME_ENGINE_TECH_H
#define GAME_ENGINE_TECH_H
#include <string>
#include <vector>


class Tech {
public:
    bool hasPrevious() const;
    const Tech* getPrevious() const;
    int getTier() const;
    int getPrice(int numOfCities) const;
    std::string getName() const;
    Tech() = default;
    static const std::vector<const Tech*>& getAllTechs();
    static const Tech* findByName(const std::string& techName);



protected:
    int tier = 0;

    const Tech *previousTech = nullptr;
    Tech(int tier, const Tech* previous);


    std::string name="Base";
};

#endif // GAME_ENGINE_TECH_H