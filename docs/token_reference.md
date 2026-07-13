# Game Data And IDs

This page lists numeric ids used in `map_tokens`, action fields, and model request packets.

`model_request*`, `player_map*`, and default `tokenized_map()` use player-view tokens. Hidden player-view features are `-1`. `full_map*` uses the same feature layout but returns ground-truth values.

## Tile Token Layout

Every row in `map_tokens` describes one map tile and has exactly 23 integer
features. Tile index `i` addresses the row `map_tokens[i]`.

| Index | Field | Meaning | No value / hidden |
| ---: | --- | --- | --- |
| 0 | `visibility` | Whether this tile is known to the selected player: `1` visible/known, `0` hidden. | `0` for a hidden tile. |
| 1 | `is_cloak_around` | `1` when one of the player's units is adjacent to an enemy Cloak that remains hidden; otherwise `0`. | `-1` when the tile itself is hidden. |
| 2 | `unit_hp` | Health of the unit on this tile. | `-1` when no visible unit exists or the value is hidden. |
| 3 | `unit_owner` | Player id of the unit on this tile. | `-1` when no visible unit exists or the value is hidden. |
| 4 | `unit_type` | Type of the visible unit, such as Warrior, Archer, Catapult, or Giant. See [Units](#units). | `-1` when no visible unit exists or the value is hidden. |
| 5 | `own_unit_kills` | Kill count, exposed only for a unit owned by the selected player. | `-1` for other units, no unit, or hidden data. |
| 6 | `territory_city_id` | Runtime id of the city whose territory contains this tile. | `-1` when the tile has no territory or the value is hidden. |
| 7 | `road_bridge` | Road / bridge state. See [Road And Bridge](#road-and-bridge). | `-1` when hidden. |
| 8 | `building` | Building type. See [Buildings](#buildings). | `-1` when hidden. |
| 9 | `is_capital` | `1` for a capital city, `0` for a non-capital city. | `-1` when the tile is not a city or is hidden. |
| 10 | `city_has_workshop` | Whether the city on this tile has the Workshop upgrade: `1` yes, `0` no. | `-1` when the tile is not a visible city. |
| 11 | `city_has_wall` | Whether the city on this tile has the City Wall upgrade: `1` yes, `0` no. | `-1` when the tile is not a visible city. |
| 12 | `city_park_count` | Number of Parks owned by the city on this tile. | `-1` when the tile is not a visible city. |
| 13 | `city_level` | Level of the city on this tile. | `-1` when the tile is not a city or is hidden. |
| 14 | `settlement_type` | None, village, city, starfish, or ruin. See [Settlements](#settlements). | `-1` when hidden. |
| 15 | `settlement_id` | Runtime id of the settlement / city on this tile. | `-1` when no settlement exists or the value is hidden. |
| 16 | `city_owner` | Player id of the city owner. | `-1` when the tile is not a city or is hidden. |
| 17 | `own_city_units_occupied` | Number of the selected player's units occupying their own city. | `-1` for another player's city, a non-city tile, or hidden data. |
| 18 | `resource` | Resource type. See [Resources](#resources). | `-1` when hidden. |
| 19 | `base_terrain` | Base terrain type. See [Base Terrain](#base-terrain). | `-1` when hidden. |
| 20 | `tribe` | Map-generation tribe type. See [Tribes](#tribes). | `-1` when hidden. |
| 21 | `unit_max_hp` | Maximum health of the visible unit, including bonuses such as veteran status. | `-1` when no visible unit exists or the value is hidden. |
| 22 | `unit_origin_city_id` | Runtime id of the city that originally trained or rewarded the visible unit. For an enemy unit, it is exposed only after that city is known to the observer. | `-1` when no visible unit exists, it came from a non-city source (for example a ruin), the origin city is not known, or the value is hidden. |

!!! note
    Runtime ids such as `territory_city_id` and `settlement_id` are useful for
    linking data during one running game. They are not categorical ids for
    model training and must not be interpreted as city classes.

The terrain and object columns most commonly used by models are:

| Feature index | Field | Uses table |
| ---: | --- | --- |
| 7 | `road_bridge` | [Road And Bridge](#road-and-bridge) |
| 8 | `building` | [Buildings](#buildings) |
| 11 | `settlement_type` | [Settlements](#settlements) |
| 15 | `resource` | [Resources](#resources) |
| 16 | `base_terrain` | [Base Terrain](#base-terrain) |
| 17 | `tribe` | [Tribes](#tribes) |

Action fields use additional tables:

| Action field | Uses table |
| --- | --- |
| `type_id` | [Action Types](#action-types) |
| `tech` | [Technologies](#technologies) |
| `building` | [Buildings](#buildings) |
| `spawn_type` | [Units](#units) |
| `upgrade` | [City Upgrade Choices](#city-upgrade-choices) |
| `tile_action` | [Tile Actions](#tile-actions) |
| `unit_upgrade` | [Unit Upgrade Actions](#unit-upgrade-actions) |
| `unit_id` | Runtime unit instance id, not a unit type |

!!! note
    `unit_id` identifies a concrete unit instance in the current game. Unit type ids are listed under [Units](#units) and appear in fields such as `spawn_type`.

## Sentinel Values

| Value | Meaning |
| ---: | --- |
| `-1` | No value, no object, hidden value, or not applicable |
| `0` | Usually `None`, `Unknown`, or a valid enum value depending on the field |

## Base Terrain

Used by `map_tokens[*][19]`.

| Id | Name |
| ---: | --- |
| 0 | Ocean |
| 1 | Water |
| 2 | Land |
| 3 | Mountain |
| 4 | Forest |

## Resources

Used by `map_tokens[*][18]`.

| Id | Name |
| ---: | --- |
| 0 | None |
| 1 | Fish |
| 2 | Whale |
| 3 | Fruit |
| 4 | Crops |
| 5 | Animal |
| 6 | Metal |

## Settlements

Used by `map_tokens[*][14]`.

| Id | Name |
| ---: | --- |
| 0 | None |
| 1 | Village |
| 2 | City |
| 3 | Starfish |
| 4 | Ruin |

## Road And Bridge

Used by `map_tokens[*][7]`.

| Id | Name |
| ---: | --- |
| 0 | None |
| 1 | Road |
| 2 | Bridge |
| 3 | WaterConnection |

## Buildings

Used by `map_tokens[*][8]` and action field `building`.

| Id | Name |
| ---: | --- |
| 0 | None |
| 1 | Farm |
| 2 | Forge |
| 3 | LumberHut |
| 4 | Market |
| 5 | Mine |
| 6 | Port |
| 7 | Sawmill |
| 8 | Windmill |
| 9 | AltarOfPeace |
| 10 | EmperorsTomb |
| 11 | EyeOfGod |
| 12 | GateOfPower |
| 13 | GrandBazaar |
| 14 | ParkOfFortune |
| 15 | TowerOfWisdom |
| 16 | Lighthouse |

## Tribes

Used by `map_tokens[*][20]` and Python tribe constants.

The supported public ruleset uses ids `1..12`. Ids `13..16` are reserved/internal and are not accepted by `GameEnv` in this release.

| Id | Name |
| ---: | --- |
| 0 | Unknown |
| 1 | XinXi |
| 2 | Imperius |
| 3 | Bardur |
| 4 | Oumaji |
| 5 | Kickoo |
| 6 | Hoodrick |
| 7 | Luxidoor |
| 8 | Vengir |
| 9 | Zebasi |
| 10 | AiMo |
| 11 | Quetzali |
| 12 | Yadakk |
| 13 | Aquarion (reserved/internal) |
| 14 | Elyrion (reserved/internal) |
| 15 | Polaris (reserved/internal) |
| 16 | Cymanti (reserved/internal) |

## Technologies

Used by action field `tech`.

| Id | Name | Tier |
| ---: | --- | --- |
| 0 | Climbing | 1 |
| 1 | Fishing | 1 |
| 2 | Hunting | 1 |
| 3 | Organization | 1 |
| 4 | Riding | 1 |
| 5 | Archery | 2 |
| 6 | Ramming | 2 |
| 7 | Farming | 2 |
| 8 | Forestry | 2 |
| 9 | FreeSpirit | 2 |
| 10 | Meditation | 2 |
| 11 | Mining | 2 |
| 12 | Roads | 2 |
| 13 | Sailing | 2 |
| 14 | Strategy | 2 |
| 15 | Aquatism | 3 |
| 16 | Chivalry | 3 |
| 17 | Construction | 3 |
| 18 | Diplomacy | 3 |
| 19 | Mathematics | 3 |
| 20 | Navigation | 3 |
| 21 | Philosophy | 3 |
| 22 | Smithery | 3 |
| 23 | Spiritualism | 3 |
| 24 | Trade | 3 |
| 25 | Count / none sentinel |

## Units

Used by `map_tokens[*][4]` (`unit_type`) and action fields such as
`spawn_type`. Concrete `unit_id` values remain runtime object ids used only in
action payloads and are not listed here.

| Id | Name | Group |
| ---: | --- | --- |
| 0 | Unknown | Sentinel |
| 1 | Warrior | Land |
| 2 | Archer | Land |
| 3 | Defender | Land |
| 4 | Rider | Land |
| 5 | MindBender | Land |
| 6 | Swordsman | Land |
| 7 | Catapult | Land |
| 8 | Cloak | Land |
| 9 | Knight | Land |
| 10 | Dagger | Land |
| 11 | Giant | Land |
| 12 | Bunny | Land |
| 13 | Bunta | Land |
| 20 | Raft | Naval |
| 21 | Scout | Naval |
| 22 | Rammer | Naval |
| 23 | Bomber | Naval |
| 24 | Dinghy | Naval |
| 25 | Pirate | Naval |
| 26 | Juggernaut | Naval |
| 30 | Mermaid | Aquarion |
| 31 | AquaticAmphibian | Aquarion |
| 32 | MermaidArcher | Aquarion |
| 33 | MermaidDefender | Aquarion |
| 34 | Swordsmaid | Aquarion |
| 35 | Scuba | Aquarion |
| 36 | Siren | Aquarion |
| 37 | Shark | Aquarion |
| 38 | YellyBelly | Aquarion |
| 39 | Puffer | Aquarion |
| 40 | TridentionAq | Aquarion |
| 41 | CrabAq | Aquarion |
| 50 | Polytaur | Elyrion |
| 51 | DragonEgg | Elyrion |
| 52 | BabyDragon | Elyrion |
| 53 | FireDragon | Elyrion |
| 60 | IceArcher | Polaris |
| 61 | BattleSled | Polaris |
| 62 | Mooni | Polaris |
| 63 | IceFortress | Polaris |
| 64 | Gaami | Polaris |
| 70 | Hexapod | Cymanti |
| 71 | Kiton | Cymanti |
| 72 | Phychi | Cymanti |
| 73 | Shaman | Cymanti |
| 74 | Raychi | Cymanti |
| 75 | Exida | Cymanti |
| 76 | Doomux | Cymanti |
| 77 | MothC | Cymanti |
| 78 | LarvaC | Cymanti |
| 79 | InsectEgg | Cymanti |
| 80 | Boomchi | Cymanti |
| 81 | LivingIsland | Cymanti |
| 90 | GiantSuper | Super unit |

## Action Types

Used by action field `type_id`.

| Id | Name |
| ---: | --- |
| 0 | Move |
| 1 | Attack |
| 2 | Heal |
| 3 | EndTurn |
| 4 | BuyTech |
| 5 | UpgradeCity |
| 6 | Build |
| 7 | SpawnUnit |
| 8 | TileAction |
| 9 | UnitUpgrade |

## Tile Actions

Used by action field `tile_action`.

| Id | Name |
| ---: | --- |
| 0 | None |
| 1 | Hunt |
| 2 | Organization |
| 3 | Fishing |
| 4 | ClearForest |
| 5 | BurnForest |
| 6 | GrowForest |
| 7 | DestroyTile |
| 8 | BuildRoad |
| 9 | BuildBridge |
| 10 | Explorer |
| 11 | FoundCity |
| 12 | Ruin |
| 13 | Starfish |
| 14 | CaptureCity |

## Unit Upgrade Actions

Used by action field `unit_upgrade`.

| Id | Name |
| ---: | --- |
| 0 | None |
| 1 | RaftToScout |
| 2 | RaftToRammer |
| 3 | RaftToBomber |
| 4 | BecomeVeteran |
| 5 | Disband |

## City Upgrade Choices

Used by action field `upgrade`.

| Id | Name |
| ---: | --- |
| 0 | None |
| 1 | Workshop |
| 2 | Explorer |
| 3 | CityWall |
| 4 | Resources |
| 5 | PopulationGrowth |
| 6 | BorderGrowth |
| 7 | Park |
| 8 | SuperUnit |
