import { Redis } from 'ioredis';

const REDIS_URL = process.env.REDIS_URL || 'redis://127.0.0.1:6379/0';

async function verify() {
  console.log('Verifying Redis data...');
  const redis = new Redis(REDIS_URL);

  try {
    const dbsize = await redis.dbsize();
    console.log(`DBSIZE (Total keys in DB): ${dbsize}`);

    // Check 'meta'
    const meta = await redis.getBuffer('meta');
    if (meta) {
      console.log(`Key 'meta' exists, length: ${meta.length} bytes`);
      console.log(`Preview: ${meta.toString('utf8').substring(0, 100)}...`);
    } else {
      console.log(`Key 'meta' missing!`);
    }

    // Check Shanghai center tile
    const tileKey = 'tile:8Q336F';
    const tile = await redis.getBuffer(tileKey);
    if (tile) {
      console.log(`Key '${tileKey}' exists, length: ${tile.length} bytes`);
      // Since it's XZ compressed, it should start with FD 37 7A 58 5A 00
      const header = tile.subarray(0, 6).toString('hex').toUpperCase();
      console.log(`Tile header (hex): ${header} (Expect FD377A585A00 for XZ)`);
    } else {
      console.log(`Key '${tileKey}' missing!`);
    }
  } catch (err) {
    console.error('Redis verification failed:', err);
    process.exitCode = 1;
  } finally {
    await redis.quit();
  }
}

verify();
