import assert from 'node:assert/strict';
import test from 'node:test';
import { KVLookup, LruCache, type KVStorage } from './lookup';
import { FORMAT } from './codec';

const encode = (value: unknown) => new TextEncoder().encode(JSON.stringify(value));

class MemoryStorage implements KVStorage {
    constructor(readonly values = new Map<string, Uint8Array>()) {}

    async get(key: string): Promise<Uint8Array | null> {
        return this.values.get(key) ?? null;
    }
}

test('startup rejects missing or incompatible metadata', async () => {
    const storage = new MemoryStorage();
    const lookup = new KVLookup(storage);
    await assert.rejects(lookup.init(), /Missing meta key/);

    storage.values.set('meta', encode({ format: FORMAT, complete: false }));
    await assert.rejects(lookup.init(), /Incomplete or incompatible/);

    storage.values.set('meta', encode({ format: FORMAT, complete: true }));
    await lookup.init();
});

test('corrupt root records fail instead of returning outside coverage', async () => {
    const storage = new MemoryStorage();
    const lookup = new KVLookup(storage);
    assert.equal(await lookup._getRoot('8PFR'), 0);

    storage.values.set('root:8PFR', encode({ owner: null, value_base64: 'invalid' }));
    await assert.rejects(lookup._getRoot('8PFR'), /root 8PFR is invalid/);

    storage.values.set('root:8PFR', encode({ owner: 0, value_base64: null }));
    assert.equal(await lookup._getRoot('8PFR'), 0);
    storage.values.delete('root:8PFR');
    assert.equal(await lookup._getRoot('8PFR'), 0);
});

test('tile cache respects entry and byte limits with LRU eviction', () => {
    const cache = new LruCache<string>(2, 5);
    cache.set('a', 'A', 3);
    cache.set('b', 'B', 2);
    assert.equal(cache.get('a'), 'A');

    cache.set('c', 'C', 2);
    assert.equal(cache.get('b'), undefined);
    assert.equal(cache.get('a'), 'A');
    assert.equal(cache.get('c'), 'C');

    cache.set('oversize', 'X', 6);
    assert.equal(cache.get('oversize'), undefined);
    cache.set('a', 'large', 6);
    assert.equal(cache.get('a'), undefined);
    assert.equal(cache.get('c'), 'C');
});
