"""Classic DOS level parsing and canonical Original Lemmings ordering."""

from __future__ import annotations

from dataclasses import dataclass, replace
from pathlib import Path
from typing import Iterable

from .crunch import FormatError, sections


LEVEL_WIDTH = 1600
LEVEL_HEIGHT = 160
RATINGS = ("Fun", "Tricky", "Taxing", "Mayhem")
ORDER = (
    (91,95,96,92,93,94,97,-6,-12,-32,-42,-7,16,-17,-22,-24,-27,-43,-51,-63,-84,13,-41,-57,-60,-71,-46,-61,-65,-82),
    (0,-16,-21,-30,-31,-33,-34,-47,-62,-73,-77,-80,-83,2,-91,-93,-94,-95,-97,3,5,6,7,10,11,12,14,15,20,17),
    (22,23,24,25,26,27,30,31,32,33,34,35,36,37,1,40,41,42,43,44,45,46,47,50,51,52,53,54,21,67),
    (55,56,57,60,61,62,63,64,65,66,-67,70,71,72,73,74,75,76,77,-92,80,4,81,82,83,84,85,86,87,90),
)

CANONICAL_NAMES = (
    (
        "Just dig!", "Only floaters can survive this", "Tailor-made for blockers",
        "Now use miners and climbers", "You need bashers this time",
        "A task for blockers and bombers", "Builders will help you here",
        "Not as complicated as it looks", "As long as you try your best",
        "Smile if you love lemmings", "Keep your hair on Mr. Lemming", "Patience",
        "We all fall down", "Origins and Lemmings", "Don't let your eyes deceive you",
        "Don't do anything too hasty", "Easy when you know how", "Let's block and blow",
        "Take good care of my Lemmings", "We are now at LEMCON ONE", "You Live and Lem",
        "A Beast of a level", "I've lost that Lemming feeling", "Konbanwa Lemming san",
        "Lemmings Lemmings everywhere", "Nightmare on Lem street",
        "Let's be careful out there", "If only they could fly", "worra lorra lemmings",
        "Lock up your Lemmings",
    ),
    (
        "This should be a doddle!", "We all fall down", "A ladder would be handy",
        "Here's one I prepared earlier", "Careless clicking costs lives", "Lemmingology",
        "Been there, seen it, done it", "Lemming sanctuary in sight",
        "They just keep on coming", "There's a lot of them about", "Lemmings in the attic",
        "Bitter Lemming", "Lemming Drops", "MENACING !!", "Ozone friendly Lemmings",
        "Luvly Jubly", "Diet Lemmingaid", "It's Lemmingentry Watson",
        "Postcard from Lemmingland", "One way digging to freedom", "All the 6`s ........",
        "Turn around young lemmings!", "From The Boundary Line", "Tightrope City", "Cascade",
        "I have a cunning plan", "The Island of the Wicker people", "Lost something?",
        "Rainbow Island", "The Crankshaft",
    ),
    (
        "If at first you don`t succeed..", "Watch out, there`s traps about",
        "Heaven can wait (we hope!!!!)", "Lend a helping hand....", "The Prison!",
        "Compression Method 1", "Every Lemming for himself!!!", "The Art Gallery",
        "Perseverance", "Izzie Wizzie lemmings get busy", "The ascending pillar scenario",
        "Livin` On The Edge", "Upsidedown World", "Hunt the Nessy....",
        "What an AWESOME level", "Mary Poppins` land", "X marks the spot",
        "Tribute to M.C.Escher", "Bomboozal", "Walk the web rope", "Feel the heat!",
        "Come on over to my place", "King of the castle", "Take a running jump.....",
        "Follow the leader...", "Triple Trouble", "Call in the bomb squad",
        "POOR WEE CREATURES!", "How do I dig up the way?", "We all fall down",
    ),
    (
        "Steel Works", "The Boiler Room", "It`s hero time!", "The Crossroads",
        "Down, along, up. In that order", "One way or another", "Poles Apart",
        "Last one out is a rotten egg!", "Curse of the Pharaohs", "Pillars of Hercules",
        "We all fall down", "The Far Side", "The Great Lemming Caper", "Pea Soup",
        "The Fast Food Kitchen...", "Just a Minute...", "Stepping Stones",
        "And then there were four....", "Time to get up!", "No added colours or Lemmings",
        "With a twist of lemming please", "A BeastII of a level", "Going up.......",
        "All or Nothing", "Have a nice day!", "The Steel Mines of Kessel",
        "Just a Minute (Part Two)", "Mind the step.....", "Save Me",
        "Rendezvous at the Mountain",
    ),
)


def word(data: bytes, offset: int, signed: bool = False) -> int:
    return int.from_bytes(data[offset : offset + 2], "big", signed=signed)


@dataclass(frozen=True)
class ObjectPlacement:
    x: int
    y: int
    object_id: int
    upside_down: bool
    no_overwrite: bool
    only_overwrite: bool


@dataclass(frozen=True)
class TerrainPlacement:
    x: int
    y: int
    terrain_id: int
    upside_down: bool
    no_overwrite: bool
    erase: bool


@dataclass(frozen=True)
class SteelArea:
    x: int
    y: int
    width: int
    height: int


@dataclass(frozen=True)
class Level:
    name: str
    release_rate: int
    lemming_count: int
    rescue_count: int
    time_minutes: int
    skills: tuple[int, ...]
    camera_x: int
    style: int
    special: int
    objects: tuple[ObjectPlacement, ...]
    terrain: tuple[TerrainPlacement, ...]
    steel: tuple[SteelArea, ...]
    source_id: int = 0
    odd_record: int = -1
    rating: int = 0
    number: int = 0


def parse_level(data: bytes, source_id: int = 0) -> Level:
    if len(data) != 2048:
        raise FormatError(f"level must be 2048 bytes, got {len(data)}")
    objects = []
    for offset in range(0x20, 0x120, 8):
        flags = word(data, offset + 6)
        if flags == 0:
            continue
        objects.append(ObjectPlacement(
            word(data, offset) - 16,
            word(data, offset + 2, signed=True),
            word(data, offset + 4),
            bool(flags & 0x0080), bool(flags & 0x8000), bool(flags & 0x4000),
        ))
    terrain = []
    for offset in range(0x120, 0x760, 4):
        raw = int.from_bytes(data[offset : offset + 4], "big")
        if raw == 0xFFFFFFFF:
            continue
        y = (raw >> 7) & 0x1FF
        terrain.append(TerrainPlacement(
            ((raw >> 16) & 0xFFF) - 16,
            y - (516 if y > 256 else 4),
            raw & 0x3F,
            bool(raw & (1 << 30)), bool(raw & (1 << 31)), bool(raw & (1 << 29)),
        ))
    steel = []
    for offset in range(0x760, 0x7E0, 4):
        pos = word(data, offset)
        size = data[offset + 2]
        if pos == 0 and size == 0:
            continue
        steel.append(SteelArea(
            (pos & 0x1FF) * 4 - 16,
            ((pos >> 9) & 0x7F) * 4,
            (size & 0x0F) * 4 + 4,
            ((size >> 4) & 0x0F) * 4 + 4,
        ))
    name = data[0x7E0:0x800].decode("ascii", "replace").strip(" \0")
    return Level(
        name=name,
        release_rate=word(data, 0), lemming_count=word(data, 2),
        rescue_count=word(data, 4), time_minutes=word(data, 6),
        skills=tuple(word(data, 8 + index * 2) for index in range(8)),
        camera_x=word(data, 0x18) & ~7, style=word(data, 0x1A),
        special=word(data, 0x1C), objects=tuple(objects), terrain=tuple(terrain),
        steel=tuple(steel), source_id=source_id,
    )


def load_unique(directory: Path) -> list[Level]:
    result: list[Level | None] = [None] * 100
    for path in sorted(directory.glob("level[0-9][0-9][0-9].dat")):
        file_id = int(path.stem[-3:])
        for part, section in enumerate(sections(path.read_bytes())):
            source_id = file_id * 10 + part
            result[source_id] = parse_level(section.payload, source_id)
    return [level for level in result if level is not None]


def load_oddtable(path: Path) -> list[tuple[str, tuple[int, ...]]]:
    data = path.read_bytes()
    if len(data) != 80 * 56:
        raise FormatError("ODDTABLE.DAT must contain 80 56-byte records")
    records = []
    for offset in range(0, len(data), 56):
        values = tuple(word(data, offset + index * 2) for index in range(12))
        name = data[offset + 24 : offset + 56].decode("ascii", "replace").strip(" \0")
        records.append((name, values))
    return records


def canonical_levels(unique: Iterable[Level], oddtable: list[tuple[str, tuple[int, ...]]]) -> list[Level]:
    by_id = {level.source_id: level for level in unique}
    odd_by_name: dict[str, list[tuple[int, tuple[int, ...]]]] = {}
    for index, (name, values) in enumerate(oddtable):
        if name != "This is a non-used duplicate":
            odd_by_name.setdefault(name.casefold(), []).append((index, values))
    odd_use: dict[str, int] = {}
    result = []
    for rating, entries in enumerate(ORDER):
        for number, encoded in enumerate(entries):
            source_id = abs(encoded)
            if source_id not in by_id:
                raise FormatError(f"missing source level {source_id}")
            level = by_id[source_id]
            odd_record = -1
            if encoded < 0:
                target = CANONICAL_NAMES[rating][number]
                matches = odd_by_name.get(target.casefold(), [])
                used = odd_use.get(target.casefold(), 0)
                if used >= len(matches):
                    raise FormatError(f"missing odd-table properties for {target}")
                odd_record, values = matches[used]
                odd_use[target.casefold()] = used + 1
                level = replace(level, name=target, release_rate=values[0],
                    lemming_count=values[1], rescue_count=values[2],
                    time_minutes=values[3], skills=values[4:12])
            else:
                expected = CANONICAL_NAMES[rating][number]
                if level.name.casefold() != expected.casefold():
                    raise FormatError(
                        f"source level {source_id} is {level.name!r}, expected {expected!r}"
                    )
            result.append(replace(level, rating=rating, number=number,
                                  odd_record=odd_record))
    return result
