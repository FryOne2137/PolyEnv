from __future__ import annotations

from enum import IntEnum


class Tribe(IntEnum):
    XinXi = 1
    Imperius = 2
    Bardur = 3
    Oumaji = 4
    Kickoo = 5
    Hoodrick = 6
    Luxidoor = 7
    Vengir = 8
    Zebasi = 9
    AiMo = 10
    Quetzali = 11
    Yadakk = 12
    # Reserved/internal ids. Special tribes are not part of the supported
    # public ruleset for the current release.
    Aquarion = 13
    Elyrion = 14
    Polaris = 15
    Cymanti = 16


# Convenience names for: players=[Bardur, Kickoo, ...]
XinXi = Tribe.XinXi
Imperius = Tribe.Imperius
Bardur = Tribe.Bardur
Oumaji = Tribe.Oumaji
Kickoo = Tribe.Kickoo
Hoodrick = Tribe.Hoodrick
Luxidoor = Tribe.Luxidoor
Vengir = Tribe.Vengir
Zebasi = Tribe.Zebasi
AiMo = Tribe.AiMo
Quetzali = Tribe.Quetzali
Yadakk = Tribe.Yadakk
SUPPORTED_TRIBES = {
    Tribe.XinXi,
    Tribe.Imperius,
    Tribe.Bardur,
    Tribe.Oumaji,
    Tribe.Kickoo,
    Tribe.Hoodrick,
    Tribe.Luxidoor,
    Tribe.Vengir,
    Tribe.Zebasi,
    Tribe.AiMo,
    Tribe.Quetzali,
    Tribe.Yadakk,
}


NAME_TO_TRIBE = {
    "xinxi": Tribe.XinXi,
    "xin-xi": Tribe.XinXi,
    "xin_xi": Tribe.XinXi,
    "imperius": Tribe.Imperius,
    "bardur": Tribe.Bardur,
    "oumaji": Tribe.Oumaji,
    "kickoo": Tribe.Kickoo,
    "hoodrick": Tribe.Hoodrick,
    "luxidoor": Tribe.Luxidoor,
    "vengir": Tribe.Vengir,
    "zebasi": Tribe.Zebasi,
    "aimo": Tribe.AiMo,
    "ai-mo": Tribe.AiMo,
    "ai_mo": Tribe.AiMo,
    "quetzali": Tribe.Quetzali,
    "yadakk": Tribe.Yadakk,
}


__all__ = [
    "Tribe",
    "XinXi",
    "Imperius",
    "Bardur",
    "Oumaji",
    "Kickoo",
    "Hoodrick",
    "Luxidoor",
    "Vengir",
    "Zebasi",
    "AiMo",
    "Quetzali",
    "Yadakk",
    "SUPPORTED_TRIBES",
    "NAME_TO_TRIBE",
]
