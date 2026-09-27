import * as pako from 'pako';
(globalThis as any).self = globalThis;
import { createRequire } from 'module';
const require = createRequire(import.meta.url);
const { XzReadableStream } = require('xzwasm');

export const BRANCH = 65535;
export const LABEL_MASK = 0x3FFF;
export const BOUNDARY = 0x8000;
export const OVERLAP = 0x4000;
export const FORMAT = "olc-adaptive-4-6-8-10-11-v2";

const ALPHABET = "23456789CFGHJMPQRVWX";
const DIGITS: Record<string, number> = {};
for (let i = 0; i < ALPHABET.length; i++) {
    DIGITS[ALPHABET.charAt(i)] = i;
}

function digit(raw: string, index: number): number {
    const character = raw[index];
    const value = character === undefined ? undefined : DIGITS[character];
    if (value === undefined) throw new Error('Invalid Plus Code character');
    return value;
}

export function child_index(raw: string, level: number): number {
    if (level === 10) {
        return digit(raw, 10);
    }
    return digit(raw, level) * 20 + digit(raw, level + 1);
}

export function decode_vector(data: Uint8Array, position: number, size: number): { values: Uint16Array, position: number } {
    const view = new DataView(data.buffer, data.byteOffset, data.byteLength);
    const mode = data[position];
    position += 1;

    let values = new Uint16Array(size);

    if (mode === 0) {
        const count = view.getUint16(position, true);
        position += 2;
        let last = 0;
        let ptr = 0;
        for (let i = 0; i < count; i++) {
            const end = view.getUint16(position, true);
            const value = view.getUint16(position + 2, true);
            position += 4;
            if (!(last < end && end <= size)) {
                throw new Error("Invalid run");
            }
            for (let j = 0; j < end - last; j++) {
                values[ptr++] = value;
            }
            last = end;
        }
        if (last !== size) {
            throw new Error("Incomplete vector");
        }
    } else if (mode === 1) {
        const defaultVal = view.getUint16(position, true);
        const count = view.getUint16(position + 2, true);
        position += 4;

        for (let i = 0; i < size; i++) {
            values[i] = defaultVal;
        }

        let last = -1;
        for (let i = 0; i < count; i++) {
            const index = view.getUint16(position, true);
            const value = view.getUint16(position + 2, true);
            position += 4;
            if (!(last < index && index < size)) {
                throw new Error("Invalid sparse index");
            }
            values[index] = value;
            last = index;
        }
    } else {
        throw new Error("Unknown vector mode");
    }

    return { values, position };
}

export class TreeView {
    raw: Uint8Array;
    view: DataView;
    level: number;
    values: Uint16Array;
    locations: Record<number, number>;
    cache: Record<number, number | TreeView>;
    end: number;

    constructor(raw: Uint8Array, position: number = 0, level: number = 6) {
        if (raw[position] !== 1) {
            throw new Error("Expected branch node");
        }
        this.raw = raw;
        this.view = new DataView(raw.buffer, raw.byteOffset, raw.byteLength);
        this.level = level;

        const decoded = decode_vector(raw, position + 1, level === 10 ? 20 : 400);
        this.values = decoded.values;
        position = decoded.position;

        this.locations = {};
        this.cache = {};

        for (let i = 0; i < this.values.length; i++) {
            if (this.values[i] === BRANCH) {
                const size = this.view.getUint32(position, true);
                position += 4;
                this.locations[i] = position;
                position += size;
            }
        }
        this.end = position;
    }

    getChild(i: number): number | TreeView {
        if (!(i in this.cache)) {
            const pos = this.locations[i];
            if (pos === undefined) throw new Error('Missing branch child');
            if (this.raw[pos] === 0) {
                this.cache[i] = this.view.getUint16(pos + 1, true);
            } else {
                const nextLevel = this.level === 6 ? 8 : (this.level === 8 ? 10 : -1);
                this.cache[i] = new TreeView(this.raw, pos, nextLevel);
            }
        }
        const child = this.cache[i];
        if (child === undefined) throw new Error('Missing branch child');
        return child;
    }
}

export async function open_tree(blob: Uint8Array): Promise<number | TreeView> {
    // Decompress fully
    const compressedStream = new Response(new Uint8Array(blob)).body;
    if (!compressedStream) throw new Error("Stream error");
    const stream = new XzReadableStream(compressedStream);
    const arrayBuffer = await new Response(stream).arrayBuffer();
    const raw = new Uint8Array(arrayBuffer);
    if (raw[0] === 0) {
        const view = new DataView(raw.buffer, raw.byteOffset, raw.byteLength);
        return view.getUint16(1, true);
    }
    const treeView = new TreeView(raw);
    if (treeView.end !== raw.length) {
        throw new Error("Invalid root size");
    }
    return treeView;
}

export function unpack_directory(blob: Uint8Array): Uint16Array {
    const raw = pako.inflate(blob);
    const decoded = decode_vector(raw, 0, 400);
    if (decoded.position !== raw.length) {
        throw new Error("Trailing directory data");
    }
    return decoded.values;
}

export function summarize(node: number | TreeView): { labels: Set<number>, flags: number } {
    if (typeof node === 'number') {
        return { labels: new Set([node & LABEL_MASK]), flags: node & (BOUNDARY | OVERLAP) };
    }
    const labels = new Set<number>();
    let flags = 0;
    for (let i = 0; i < node.values.length; i++) {
        const value = node.values[i];
        if (value === undefined) throw new Error('Missing tree value');
        const child = value === BRANCH ? node.getChild(i) : value;
        const res = summarize(child);
        for (const l of res.labels) {
            labels.add(l);
        }
        flags |= res.flags;
    }
    return { labels, flags };
}
