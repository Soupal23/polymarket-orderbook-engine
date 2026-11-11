import { MongoClient } from 'mongodb';
import { Queue, Worker } from 'bullmq';

export class PersistenceWorker {
  constructor(options = {}) {
    this.mongoUri = options.mongoUri || process.env.MONGO_URI || 'mongodb://127.0.0.1:27017';
    this.dbName = options.dbName || 'polymarket_orderbook';
    this.redisUrl = options.redisUrl || process.env.REDIS_URL || 'redis://127.0.0.1:6379';
    this.isRedisOnline = options.isRedisOnline || false;

    this.mongoClient = null;
    this.db = null;
    this.mongoOnline = false;

    this.queue = null;
    this.worker = null;

    // Resilient fallback storage: capped ring buffers
    this.inMemoryTrades = [];
    this.inMemorySnapshots = [];
    this.maxMemoryItems = 1000;
  }

  async init(redisOnline = false) {
    this.isRedisOnline = redisOnline;

    if (process.env.NODE_ENV === 'production' && !process.env.MONGO_URI) {
      this.mongoOnline = false;
      console.log(`[PersistenceWorker] No MONGO_URI configured in production; using fast in-memory ring-buffer storage.`);
    } else {
      // 1. Attempt MongoDB connection
      try {
        this.mongoClient = new MongoClient(this.mongoUri, {
          serverSelectionTimeoutMS: 1000,
          connectTimeoutMS: 1000,
        });
        await this.mongoClient.connect();
        this.db = this.mongoClient.db(this.dbName);
        this.mongoOnline = true;
        console.log(`[PersistenceWorker] Connected to MongoDB at ${this.mongoUri}/${this.dbName}`);

        // Ensure TTL indexes (expire after 24h = 86400s)
        const snapshotsCol = this.db.collection('snapshots');
        await snapshotsCol.createIndex({ created_at: 1 }, { expireAfterSeconds: 86400 });
        await snapshotsCol.createIndex({ asset_id: 1, created_at: -1 });

        const tradesCol = this.db.collection('trades');
        await tradesCol.createIndex({ created_at: 1 }, { expireAfterSeconds: 86400 });
        await tradesCol.createIndex({ asset_id: 1, timestamp_ms: -1 });
      } catch {
        this.mongoOnline = false;
        if (this.mongoClient) {
          try { await this.mongoClient.close(); } catch {}
          this.mongoClient = null;
        }
        console.log(`[PersistenceWorker] MongoDB offline (${this.mongoUri}); using fast in-memory ring-buffer storage.`);
      }
    }

    // 2. Setup BullMQ Queue if Redis is available
    if (this.isRedisOnline) {
      try {
        const parsedUrl = new URL(this.redisUrl);
        const connection = {
          host: parsedUrl.hostname || '127.0.0.1',
          port: Number(parsedUrl.port || 6379),
        };

        this.queue = new Queue('persistence_queue', { connection });
        this.worker = new Worker(
          'persistence_queue',
          async (job) => {
            await this.processJob(job.data);
          },
          { connection, concurrency: 5 }
        );

        this.worker.on('failed', (job, err) => {
          console.warn(`[BullMQ Worker] Job ${job?.id} failed:`, err.message);
        });

        console.log(`[PersistenceWorker] BullMQ queue and worker initialized on Redis.`);
      } catch (err) {
        console.warn(`[PersistenceWorker] BullMQ initialization skipped: ${err.message}`);
      }
    }
  }

  async recordSnapshot(snapshotData) {
    const record = {
      ...snapshotData,
      created_at: new Date()
    };

    // Keep in in-memory ring buffer
    this.inMemorySnapshots.push(record);
    if (this.inMemorySnapshots.length > this.maxMemoryItems) {
      this.inMemorySnapshots.shift();
    }

    if (this.queue) {
      await this.queue.add('snapshot', record, { removeOnComplete: 100, removeOnFail: 50 });
    } else {
      await this.processJob({ type: 'snapshot', data: record });
    }
  }

  async recordTrade(tradeData) {
    const record = {
      ...tradeData,
      created_at: new Date()
    };

    // Keep in in-memory ring buffer
    this.inMemoryTrades.push(record);
    if (this.inMemoryTrades.length > this.maxMemoryItems) {
      this.inMemoryTrades.shift();
    }

    if (this.queue) {
      await this.queue.add('trade', record, { removeOnComplete: 100, removeOnFail: 50 });
    } else {
      await this.processJob({ type: 'trade', data: record });
    }
  }

  async processJob(jobData) {
    if (!this.mongoOnline || !this.db) return;

    try {
      if (jobData.type === 'snapshot' || jobData.event === 'snapshot') {
        const doc = jobData.data || jobData;
        await this.db.collection('snapshots').insertOne(doc);
      } else if (jobData.type === 'trade' || jobData.event === 'trade') {
        const doc = jobData.data || jobData;
        await this.db.collection('trades').insertOne(doc);
      }
    } catch (err) {
      console.warn(`[PersistenceWorker] DB write failed:`, err.message);
    }
  }

  async getRecentTrades(assetId, limit = 50) {
    if (this.mongoOnline && this.db) {
      try {
        const query = assetId ? { asset_id: assetId } : {};
        return await this.db
          .collection('trades')
          .find(query)
          .sort({ timestamp_ms: -1 })
          .limit(limit)
          .toArray();
      } catch {
        // Fall back to memory
      }
    }

    // In-memory filter
    let results = this.inMemoryTrades;
    if (assetId) {
      results = results.filter((t) => t.asset_id === assetId);
    }
    return results.slice(-limit).reverse();
  }

  getHealth() {
    return {
      mongodb: {
        online: this.mongoOnline,
        uri: this.mongoUri,
        database: this.dbName,
      },
      bullmq: {
        active: Boolean(this.queue && this.worker),
      },
      buffered_snapshots_count: this.inMemorySnapshots.length,
      buffered_trades_count: this.inMemoryTrades.length,
    };
  }

  async close() {
    if (this.worker) await this.worker.close();
    if (this.queue) await this.queue.close();
    if (this.mongoClient) await this.mongoClient.close();
  }
}
