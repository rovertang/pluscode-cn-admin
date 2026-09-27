import fs from 'fs';
import zlib from 'zlib';
import readline from 'readline';
import { Redis } from 'ioredis';

const REDIS_URL = process.env.REDIS_URL || 'redis://127.0.0.1:6379/0';
const KV_FILE = '../../data/processed/v2/pluscode_admin_v2.kv.jsonl.gz';

async function importToRedis() {
  console.log('Connecting to Redis...');
  const redis = new Redis(REDIS_URL);

  console.log(`Reading from ${KV_FILE}...`);
  const fileStream = fs.createReadStream(KV_FILE);
  const gzipStream = fileStream.pipe(zlib.createGunzip());
  const rl = readline.createInterface({
    input: gzipStream,
    crlfDelay: Infinity
  });

  let count = 0;
  let pipeline = redis.pipeline();
  const BATCH_SIZE = 5000;

  for await (const line of rl) {
    if (!line.trim()) continue;

    const obj = JSON.parse(line);
    const key = obj.key;

    let buffer;
    if (key.startsWith('tile:')) {
      buffer = Buffer.from(obj.value_base64, 'base64');
    } else if (key.startsWith('root:')) {
      buffer = Buffer.from(JSON.stringify({owner: obj.owner, value_base64: obj.value_base64}), 'utf8');
    } else {
      buffer = Buffer.from(JSON.stringify(obj.value), 'utf8');
    }

    pipeline.set(key, buffer);
    count++;

    if (count % BATCH_SIZE === 0) {
      await pipeline.exec();
      pipeline = redis.pipeline();
      console.log(`Imported ${count} records...`);
    }
  }

  // Flush remaining
  if (pipeline.length > 0) {
    await pipeline.exec();
  }

  console.log(`\nImport complete! Total records: ${count}`);
  await redis.quit();
}

importToRedis().catch(err => {
  console.error("Error importing to Redis:", err);
  process.exit(1);
});
