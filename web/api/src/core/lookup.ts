import OLC from 'open-location-code';
const olc = new OLC.OpenLocationCode();
import { BRANCH, BOUNDARY, FORMAT, LABEL_MASK, OVERLAP, child_index, summarize, open_tree, TreeView, unpack_directory } from './codec';

export interface KVStorage {
    get(key: string): Promise<Uint8Array | null>;
}

export class LruCache<T> {
    private entries = new Map<string, { value: T; bytes: number }>();
    private totalBytes = 0;

    constructor(private readonly maxEntries: number, private readonly maxBytes: number) {}

    get(key: string): T | undefined {
        const entry = this.entries.get(key);
        if (!entry) return undefined;
        this.entries.delete(key);
        this.entries.set(key, entry);
        return entry.value;
    }

    set(key: string, value: T, bytes: number): void {
        const previous = this.entries.get(key);
        if (previous) {
            this.totalBytes -= previous.bytes;
            this.entries.delete(key);
        }
        if (bytes > this.maxBytes) return;
        this.entries.set(key, { value, bytes });
        this.totalBytes += bytes;
        while (this.entries.size > this.maxEntries || this.totalBytes > this.maxBytes) {
            const oldest = this.entries.keys().next().value;
            if (oldest === undefined) break;
            this.totalBytes -= this.entries.get(oldest)!.bytes;
            this.entries.delete(oldest);
        }
    }
}

export class KVLookup {
    storage: KVStorage;
    roots = new Map<string, number | Uint16Array>();
    admins: Record<number, any> = {};
    private tileCache = new LruCache<number | TreeView>(64, 8 * 1024 * 1024);

    constructor(storage: KVStorage) {
        this.storage = storage;
    }

    async init() {
        const metaBuf = await this.storage.get('meta');
        if (!metaBuf) throw new Error("Missing meta key");
        const meta = JSON.parse(new TextDecoder().decode(metaBuf));
        if (meta.format !== FORMAT || meta.complete !== true) {
            throw new Error("Incomplete or incompatible v2 index");
        }
    }

    async _getTile(key: string): Promise<number | TreeView> {
        const cached = this.tileCache.get(key);
        if (cached !== undefined) return cached;
        const blob = await this.storage.get(`tile:${key}`);
        if (!blob) throw new Error(`Index is corrupt: required tile ${key} is absent`);
        const tree = await open_tree(blob);
        this.tileCache.set(key, tree, typeof tree === 'number' ? 2 : tree.raw.byteLength);
        return tree;
    }

    async _getRoot(prefix: string): Promise<number | Uint16Array> {
        const cached = this.roots.get(prefix);
        if (cached !== undefined) return cached;
        const blob = await this.storage.get(`root:${prefix}`);
        if (!blob) return 0; // default root is 0

        let rootData: number | Uint16Array;
        try {
            const obj: unknown = JSON.parse(new TextDecoder().decode(blob));
            if (!obj || typeof obj !== 'object' || !('owner' in obj)) {
                throw new Error('Invalid root record');
            }
            if (typeof obj.owner === 'number' && Number.isInteger(obj.owner) && obj.owner >= 0 && obj.owner < BRANCH) {
                rootData = obj.owner;
            } else if (obj.owner === null && 'value_base64' in obj && typeof obj.value_base64 === 'string') {
                rootData = unpack_directory(Buffer.from(obj.value_base64, 'base64'));
            } else {
                throw new Error('Invalid root owner or directory');
            }
        } catch (error) {
            throw new Error(`Index is corrupt: root ${prefix} is invalid`, { cause: error });
        }

        this.roots.set(prefix, rootData);
        return rootData;
    }

    async _getAdmin(id: number): Promise<any> {
        if (this.admins[id]) return this.admins[id];
        const blob = await this.storage.get(`admin:${id}`);
        if (blob) {
            const data = JSON.parse(new TextDecoder().decode(blob));
            this.admins[id] = data;
            return data;
        }
        throw new Error(`Index is corrupt: admin ${id} is absent`);
    }

    async lookup(code: string): Promise<any> {
        code = code.trim().toUpperCase();
        if (!olc.isFull(code)) {
            throw new Error("A valid full Plus Code including '+' is required");
        }
        const raw = code.replace("+", "").replace(/0+$/, "");
        const length = raw.length;
        if (length < 8) {
            throw new Error("At least 8 significant characters are required");
        }

        const rootPrefix = raw.substring(0, 4);
        let node: number | Uint16Array | TreeView = await this._getRoot(rootPrefix);
        let level = 4;

        if (typeof node !== 'number') {
            const child = node[child_index(raw, 4)];
            if (child === undefined) throw new Error('Index is corrupt: root child is absent');
            node = child;
            level = 6;
            if (node === BRANCH) {
                node = await this._getTile(raw.substring(0, 6));
            }
        }

        while (typeof node !== 'number' && level < Math.min(length, 11)) {
            if (!(node instanceof TreeView)) throw new Error('Index is corrupt: unexpected directory level');
            const i = child_index(raw, level);
            const tree: TreeView = node;
            const value: number | undefined = tree.values[i];
            if (value === undefined) throw new Error('Index is corrupt: tree child is absent');
            node = value === BRANCH ? tree.getChild(i) : value;
            level = level === 6 ? 8 : (level === 8 ? 10 : 11);
        }

        if (typeof node !== 'number') {
            if (!(node instanceof TreeView)) throw new Error('Index is corrupt: unexpected directory level');
            const summary = summarize(node);
            const labels = summary.labels;
            const flags = summary.flags;

            const adminPromises = Array.from(labels).filter(l => l !== 0).sort((a,b) => a-b).map(l => this._getAdmin(l));
            const admins = await Promise.all(adminPromises);

            return {
                pluscode: code,
                precision: Math.min(length, 11),
                max_precision: 11,
                status: "ambiguous",
                admin: null,
                matched_length: null,
                boundary_cell: true,
                source_overlap: !!(flags & OVERLAP),
                sampled_candidates: admins,
                includes_uncovered_samples: labels.has(0),
                candidate_semantics: "adaptive certified leaves and 11-digit centers; not exhaustive polygon intersections"
            };
        }

        const label = node & LABEL_MASK;
        const leaf = raw.substring(0, level);
        const leaf_code = level >= 8 ? leaf.substring(0, 8) + "+" + leaf.substring(8) : leaf.padEnd(8, "0") + "+";

        return {
            pluscode: code,
            precision: Math.min(length, 11),
            max_precision: 11,
            status: label ? "matched" : "outside_coverage",
            admin: label ? await this._getAdmin(label) : null,
            matched_length: level,
            matched_pluscode: leaf_code,
            boundary_cell: !!(node & BOUNDARY),
            source_overlap: !!(node & OVERLAP),
            assignment: level < 11 ? "certified_full_cell" : "11_digit_center"
        };
    }

    async lookup_latlng(latitude: number, longitude: number): Promise<any> {
        if (!(latitude >= -90 && latitude <= 90 && longitude >= -180 && longitude <= 180)) {
            throw new Error("Finite WGS84 latitude/longitude required");
        }
        return this.lookup(olc.encode(latitude, longitude, 11));
    }
}
