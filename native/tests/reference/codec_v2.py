"""Version 2 adaptive OLC tree codec. Standard library only."""

import struct
import zlib
import lzma

FORMAT = "olc-adaptive-4-6-8-10-11-v2"
ALPHABET = "23456789CFGHJMPQRVWX"
DIGITS = {c: i for i, c in enumerate(ALPHABET)}
BRANCH = 65535
LABEL_MASK = 0x3FFF
BOUNDARY = 0x8000
OVERLAP = 0x4000


def encode_vector(values):
    values = list(map(int, values))
    runs = []
    for i, value in enumerate(values):
        if i == len(values) - 1 or value != values[i + 1]:
            runs.extend((i + 1, value))
    rle = b"\x00" + struct.pack("<H", len(runs) // 2) + struct.pack("<" + "H" * len(runs), *runs)
    frequencies = {}
    for value in values:
        frequencies[value] = frequencies.get(value, 0) + 1
    default = min(frequencies, key=lambda val: (-frequencies[val], val))
    exceptions = [(i, v) for i, v in enumerate(values) if v != default]
    sparse = b"\x01" + struct.pack("<HH", default, len(exceptions)) + b"".join(struct.pack("<HH", i, v) for i, v in exceptions)
    return rle if len(rle) <= len(sparse) else sparse


def decode_vector(data, position, size):
    mode = data[position]
    position += 1
    if mode == 0:
        count = struct.unpack_from("<H", data, position)[0]
        position += 2
        values, last = [], 0
        for _ in range(count):
            end, value = struct.unpack_from("<HH", data, position)
            position += 4
            if not last < end <= size:
                raise ValueError("Invalid run")
            values.extend([value] * (end - last))
            last = end
        if last != size:
            raise ValueError("Incomplete vector")
    elif mode == 1:
        default, count = struct.unpack_from("<HH", data, position)
        position += 4
        values, last = [default] * size, -1
        for _ in range(count):
            index, value = struct.unpack_from("<HH", data, position)
            position += 4
            if not last < index < size:
                raise ValueError("Invalid sparse index")
            values[index] = value
            last = index
    else:
        raise ValueError("Unknown vector codec")
    return values, position


def encode_tree(node, level):
    if isinstance(node, int):
        return b"\x00" + struct.pack("<H", node)
    values, children = node
    expected = 20 if level == 10 else 400
    if len(values) != expected:
        raise ValueError("Incorrect child count")
    result = bytearray(b"\x01" + encode_vector(values))
    next_level = {4: 6, 6: 8, 8: 10}.get(level)
    for i, value in enumerate(values):
        if value == BRANCH:
            if next_level is None:
                raise ValueError("Cannot refine past 11 digits")
            child = encode_tree(children[i], next_level)
            result.extend(struct.pack("<I", len(child)))
            result.extend(child)
    return bytes(result)


def decode_tree(data, position, level):
    tag = data[position]
    position += 1
    if tag == 0:
        return struct.unpack_from("<H", data, position)[0], position + 2
    if tag != 1:
        raise ValueError("Unknown node type")
    values, position = decode_vector(data, position, 20 if level == 10 else 400)
    children = {}
    next_level = {4: 6, 6: 8, 8: 10}.get(level)
    for i, value in enumerate(values):
        if value == BRANCH:
            if next_level is None:
                raise ValueError("Unexpected branch at maximum precision")
            size = struct.unpack_from("<I", data, position)[0]
            position += 4
            children[i], end = decode_tree(data, position, next_level)
            if end != position + size:
                raise ValueError("Incorrect child byte length")
            position = end
    return (values, children), position


def pack_tree(node, level=6):
    return lzma.compress(encode_tree(node, level), preset=6)


def unpack_tree(blob, level=6):
    raw = lzma.decompress(blob)
    tree, end = decode_tree(raw, 0, level)
    if end != len(raw):
        raise ValueError("Trailing tree data")
    return tree


class TreeView:
    """Decode just the nodes on a requested path; child lengths permit skipping."""

    def __init__(self, raw, position=0, level=6):
        if raw[position] != 1:
            raise ValueError("Expected branch node")
        self.raw, self.level = raw, level
        self.values, position = decode_vector(raw, position + 1, 20 if level == 10 else 400)
        self.locations, self.cache = {}, {}
        for i, value in enumerate(self.values):
            if value == BRANCH:
                size = struct.unpack_from("<I", raw, position)[0]
                position += 4
                self.locations[i] = position
                position += size
        self.end = position

    def __iter__(self):
        return iter((self.values, self))

    def __getitem__(self, i):
        if i not in self.cache:
            position = self.locations[i]
            if self.raw[position] == 0:
                self.cache[i] = struct.unpack_from("<H", self.raw, position + 1)[0]
            else:
                self.cache[i] = TreeView(self.raw, position, {6: 8, 8: 10}[self.level])
        return self.cache[i]


def open_tree(blob):
    raw = lzma.decompress(blob)
    if raw[0] == 0:
        return struct.unpack_from("<H", raw, 1)[0]
    view = TreeView(raw)
    if view.end != len(raw):
        raise ValueError("Invalid root size")
    return view


def pack_directory(values):
    return zlib.compress(encode_vector(values), 6)


def unpack_directory(blob):
    raw = zlib.decompress(blob)
    values, end = decode_vector(raw, 0, 400)
    if end != len(raw):
        raise ValueError("Trailing directory data")
    return values


def child_index(raw, level):
    if level == 10:
        return DIGITS[raw[10]]
    return DIGITS[raw[level]] * 20 + DIGITS[raw[level + 1]]


def summarize(node):
    if isinstance(node, int):
        return {node & LABEL_MASK}, node & (BOUNDARY | OVERLAP)
    labels, flags = set(), 0
    values, children = node
    for i, value in enumerate(values):
        sublabels, subflags = summarize(children[i] if value == BRANCH else value)
        labels.update(sublabels)
        flags |= subflags
    return labels, flags
