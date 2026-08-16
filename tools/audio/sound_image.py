"""Clean implementation of the DOS Lemmings v1 AdLib command interpreter.

The interpreter emits register/value pairs and contains no FM synthesis code.
All offsets are facts from the supplied VGA sound image and executable study.
"""

from __future__ import annotations

from dataclasses import dataclass


@dataclass(frozen=True)
class AudioConfig:
    channel_config: int = 1452
    level_table: int = 2215
    frequencies: int = 2343
    octaves: int = 2727
    frequency_indices: int = 2823
    instructions: int = 2926
    sound_indices: int = 21989
    sound_instruments: int = 21731
    music_count: int = 21


class Image:
    def __init__(self, data: bytes):
        if len(data) != 22125:
            raise ValueError(f"DOS v1 sound image must be 22125 bytes, got {len(data)}")
        self.data = data

    def byte(self, offset: int) -> int:
        if not 0 <= offset < len(self.data):
            raise ValueError(f"sound image offset outside data: {offset}")
        return self.data[offset]

    def be16(self, offset: int) -> int:
        # The driver is an x86 memory image, so all words are little-endian.
        # The research implementation called this operation ``readWordBE``;
        # retain the internal name while implementing the actual on-disk order.
        return self.byte(offset) | self.byte(offset + 1) << 8


class Channel:
    NONE, MUSIC, SOUND = range(3)

    def __init__(self, image: Image, config: AudioConfig, index: int):
        self.image, self.config = image, config
        p = config.channel_config + index * 20
        self.note = image.byte(p); self.wait = image.byte(p + 1)
        self.instrument_record = image.be16(p + 2)
        self.instrument = image.byte(p + 4)
        self.operator_a = image.byte(p + 5); self.operator_b = image.byte(p + 6)
        self.channel = image.byte(p + 7)
        self.key_value = image.byte(p + 8); self.key_register = image.byte(p + 9)
        self.program = image.be16(p + 10); self.position = image.be16(p + 12)
        self.level_delta = image.byte(p + 15)
        encoded_state = image.byte(p + 16)
        self.state = self.MUSIC if encoded_state == 1 else self.SOUND if encoded_state == 2 else self.NONE
        self.wait_sum = image.byte(p + 17)
        self.transpose = image.byte(p + 18); self.slide = image.byte(p + 19)
        self.instrument_base = 0

    def init_music(self, program: int, instrument_base: int) -> None:
        self.program = program
        self.position = self.image.be16(self.program) + self.config.instructions
        self.program += 2
        self.instrument_base = instrument_base
        self.wait = 1; self.state = self.MUSIC

    def init_sound(self, position: int) -> None:
        self.position = position; self.wait = 1; self.slide = 0; self.state = self.SOUND

    def _frequency(self, output: list[tuple[int, int]]) -> None:
        index = (self.note + self.transpose) & 0xFF
        table_index = index + 4
        octave = self.image.byte(self.config.octaves + table_index)
        frequency_index = self.image.byte(self.config.frequency_indices + table_index)
        frequency = self.image.be16(self.config.frequencies + frequency_index * 32)
        if not frequency & 0x8000:
            octave = (octave - 1) & 0xFF
        if octave & 0x80:
            octave = (octave + 1) & 0xFF
            frequency = (frequency << 1) & 0xFFFF
        output.append((self.channel + 0xA0, frequency & 0xFF))
        self.key_value = ((frequency >> 8) & 3) | ((octave << 2) & 0xFF)
        self.key_register = self.channel + 0xB0
        output.append((self.key_register, self.key_value | 0x20))

    def _level(self, position: int, output: list[tuple[int, int]]) -> None:
        source = self.image.byte(position)
        value = self.image.byte(self.config.level_table + (source & 0x7F))
        value |= (self.image.byte(self.instrument_record + 12) << 2) & 0xC0
        output.append((self.operator_a + 0x40, value))
        source = (self.level_delta + self.image.byte(self.instrument_record + 10)) & 0x7F
        value = self.image.byte(self.config.level_table + source)
        value |= (self.image.byte(self.instrument_record + 12) >> 2) & 0xC0
        output.append((self.operator_b + 0x40, value))

    def _envelope(self, number: int, output: list[tuple[int, int]]) -> None:
        self.instrument = number
        base = self.config.sound_instruments if self.state == self.SOUND else self.instrument_base
        p = base + ((number - 1) << 4)
        for register, offset in ((self.operator_a + 0x60, 0), (self.operator_b + 0x60, 1),
                                 (self.operator_a + 0x80, 2), (self.operator_b + 0x80, 3),
                                 (self.operator_a + 0xE0, 6), (self.operator_b + 0xE0, 7),
                                 (self.channel + 0xC0, 9), (self.operator_a + 0x20, 4),
                                 (self.operator_b + 0x20, 5)):
            output.append((register, self.image.byte(p + offset)))
        self.transpose = self.image.byte(p + 8)
        self.level_delta = self.image.byte(p + 11)
        self.instrument_record = p
        self._level(p + 10, output)

    def _control(self, command: int, position: int,
                 output: list[tuple[int, int]]) -> int | None:
        operation = command & 15
        if operation == 0:
            target = self.image.be16(self.program); self.program += 2
            if target == 0:
                table = self.image.be16(self.program) + self.config.instructions
                position = self.image.be16(table) + self.config.instructions
                self.program = table + 2
            else:
                position = target + self.config.instructions
            self.position = position
        elif operation == 1:
            output.append((self.key_register, self.key_value)); self.slide = 0
            self.position = position; self.wait = self.wait_sum; return None
        elif operation == 2:
            self.position = position; self.wait = self.wait_sum; return None
        elif operation == 3:
            return None
        elif operation == 4:
            self.transpose = self.image.byte(position); position += 1
        elif operation == 5:
            output.append((self.key_register, self.key_value)); self.state = self.NONE; return None
        elif operation == 6:
            self.slide = 1
        elif operation == 7:
            self.slide = 0xFF
        elif operation == 8:
            self._level(position, output); position += 1
        return position

    def advance(self, output: list[tuple[int, int]]) -> None:
        if self.state == self.NONE:
            return
        self.wait -= 1
        saved_position = self.position
        if self.wait <= 0:
            position = self.position
            while True:
                command = self.image.byte(position); position += 1
                if command < 0x80:
                    self.note = command
                    output.append((self.key_register, self.key_value))
                    self._frequency(output)
                    self.wait = self.wait_sum; self.position = position
                    return
                if command >= 0xE0:
                    self.wait_sum = command - 0xDF
                elif command >= 0xC0:
                    self._envelope(command - 0xC0, output)
                elif command <= 0xB0:
                    next_position = self._control(command, position, output)
                    if next_position is None:
                        return
                    position = next_position
                else:
                    self._level(position, output); position += 1
        else:
            if self.slide:
                self.note = (self.note + self.slide) & 0xFF
                self._frequency(output)
            if self.image.byte(saved_position) != 0x82 and \
                    self.image.byte(self.instrument_record + 14) == self.wait:
                output.append((self.key_register, self.key_value)); self.slide = 0


class SoundImagePlayer:
    def __init__(self, data: bytes, config: AudioConfig = AudioConfig()):
        self.image, self.config = Image(data), config
        self.channels: list[Channel] = []
        self.wait_cycles = 0; self.cycle = 0; self.sample_rate_factor = 0x4300
        self.initialized = False

    def music(self, index: int) -> "SoundImagePlayer":
        index %= self.config.music_count
        # Song-header pointers are absolute within the decompressed sound image;
        # pointers stored inside a song are relative to the instruction area.
        header = self.image.be16(self.config.instructions + index * 2)
        self.sample_rate_factor = self.image.be16(header)
        instrument_base = self.image.be16(header + 2) + self.config.instructions
        self.wait_cycles = self.image.byte(header + 4)
        count = self.image.byte(header + 5)
        self.channels = []
        for channel_index in range(count):
            channel = Channel(self.image, self.config, channel_index)
            channel.init_music(self.image.be16(header + 6 + channel_index * 2) +
                               self.config.instructions, instrument_base)
            self.channels.append(channel)
        return self

    def sound(self, index: int) -> "SoundImagePlayer":
        if not 0 <= index < 18:
            raise ValueError("sound index must be 0..17")
        channel = Channel(self.image, self.config, 8)
        channel.init_sound(self.image.be16(self.config.sound_indices + index * 2))
        self.channels = [channel]; self.wait_cycles = 0; self.sample_rate_factor = 0x4300
        return self

    def step(self) -> list[tuple[int, int]]:
        output: list[tuple[int, int]] = []
        if self.cycle > 0:
            self.cycle -= 1; return output
        self.cycle = self.wait_cycles
        if not self.initialized:
            self.initialized = True
            output.extend(((4, 0x60), (4, 0x80), (2, 0xFF), (4, 0x21),
                           (4, 0x60), (4, 0x80)))
            output.extend((channel.key_register, channel.key_value) for channel in self.channels)
            output.extend(((1, 0x20), (0xBD, 0xC0), (8, 0), (4, 0x21)))
        for channel in self.channels:
            channel.advance(output)
        return output


def music_loop_steps(data: bytes, index: int,
                     max_steps: int = 200_000) -> tuple[int, int]:
    """Return the first exactly repeating command-interpreter state."""
    player = SoundImagePlayer(data).music(index)
    seen: dict[tuple, int] = {}
    for step in range(max_steps):
        state = (player.initialized, player.cycle, tuple(
            (channel.note, channel.wait, channel.instrument_record,
             channel.instrument, channel.key_value, channel.key_register,
             channel.program, channel.position, channel.level_delta,
             channel.state, channel.wait_sum, channel.transpose,
             channel.slide, channel.instrument_base)
            for channel in player.channels
        ))
        if state in seen:
            return seen[state], step
        seen[state] = step
        player.step()
    raise ValueError(f"music {index} has no loop within {max_steps} steps")
