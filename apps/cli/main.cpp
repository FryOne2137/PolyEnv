#include <iostream>

#include "techs/ArcheryTech.h"
#include "techs/ClimbingTech.h"
#include "techs/NavigationTech.h"
#include "techs/Tech.h"
#include "tribes/Bardur.h"
#include "../../src/content/tribes/Tribe.h"

// TIP To <b>Run</b> code, press <shortcut actionId="Run"/> or click the <icon src="AllIcons.Actions.Execute"/> icon in the gutter.
int main() {
    Tribe *tribe = new Bardur();
    std::cout<<tribe->getName()<<std::endl;
    std::cout<<tribe->getId()<<std::endl;
    std::cout<<tribe->getStartStars()<<std::endl;
    std::cout<<tribe->getStartTech()<<std::endl;

}