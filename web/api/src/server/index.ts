import './polyfill';
import express from 'express';
import cors from 'cors';
import { Redis } from 'ioredis';
import { KVLookup, type KVStorage } from '../core/lookup';
import path from 'path';

const app = express();
app.use(cors());
app.use(express.static(path.join(process.cwd(), 'public')));

const port = Number(process.env.PORT || 3000);
const redisUrl = process.env.REDIS_URL || 'redis://127.0.0.1:6379/0';

const redis = new Redis(redisUrl, { lazyConnect: true, maxRetriesPerRequest: 1 });

class RedisStorage implements KVStorage {
    async get(key: string): Promise<Uint8Array | null> {
        // Roots and meta are JSON strings, tiles are Buffers
        const buffer = await redis.getBuffer(key);
        if (!buffer) return null;
        return new Uint8Array(buffer);
    }
}

const lookup = new KVLookup(new RedisStorage());

app.get('/api/query', async (req, res) => {
    try {
        const { code, lat, lng } = req.query;
        let result;
        const start = Date.now();

        if (code) {
            result = await lookup.lookup(String(code));
        } else if (lat !== undefined && lng !== undefined) {
            result = await lookup.lookup_latlng(parseFloat(String(lat)), parseFloat(String(lng)));
        } else {
            return res.status(400).json({ error: "Provide either 'code' or 'lat' and 'lng' parameters" });
        }

        const elapsed = Date.now() - start;
        res.json({
            status: 'success',
            data: result,
            debug: { latency_ms: elapsed }
        });
    } catch (err) {
        const message = err instanceof Error ? err.message : 'Unexpected query failure';
        const invalidInput = /^(A valid full Plus Code|At least 8 significant characters|Finite WGS84)/.test(message);
        res.status(invalidInput ? 400 : 500).json({
            status: 'error',
            message: invalidInput ? message : 'Index lookup failed'
        });
    }
});

try {
    await redis.connect();
    await lookup.init();
    app.listen(port, '127.0.0.1', () => {
        console.log(`Server is running at http://localhost:${port}`);
    });
} catch (error) {
    console.error('Cannot start API: Redis is unavailable or its V2 data is invalid', error);
    redis.disconnect();
    process.exitCode = 1;
}
