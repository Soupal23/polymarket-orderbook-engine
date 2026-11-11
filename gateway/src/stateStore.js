import Redis from 'ioredis';

// Market metadata map for the 8 outcome tokens across 4 markets (matching docs/feed-notes.md)
export const MARKET_METADATA = {
  "54533043819946592547517511176940999955633860128497669742211153063842200957669": {
    marketName: "Democratic Nominee 2028",
    candidate: "Gavin Newsom",
    outcome: "Yes",
    slug: "democratic-nominee-2028-newsom"
  },
  "87854174148074652060467921081181402357467303721471806610111179101805869578687": {
    marketName: "Democratic Nominee 2028",
    candidate: "Gavin Newsom",
    outcome: "No",
    slug: "democratic-nominee-2028-newsom"
  },
  "32338220190071351435772801779725302244575775216413325951443816017994629993401": {
    marketName: "Xi Jinping out before 2027?",
    candidate: "Xi Jinping",
    outcome: "Yes",
    slug: "xi-jinping-out-2027"
  },
  "25659310674993675562345759665114759892400026242514633218387667107987341231962": {
    marketName: "Xi Jinping out before 2027?",
    candidate: "Xi Jinping",
    outcome: "No",
    slug: "xi-jinping-out-2027"
  },
  "91031279171981959197361710127213577102576826515163085396576756946418341946256": {
    marketName: "Democratic Nominee 2028",
    candidate: "Pete Buttigieg",
    outcome: "Yes",
    slug: "democratic-nominee-2028-buttigieg"
  },
  "18494065527924073750217237765784380365430319396356251245973222791140100738948": {
    marketName: "Democratic Nominee 2028",
    candidate: "Pete Buttigieg",
    outcome: "No",
    slug: "democratic-nominee-2028-buttigieg"
  },
  "107064985435494333113391038470401719113272800530429703182710416066774068907304": {
    marketName: "Democratic Nominee 2028",
    candidate: "Alexandria Ocasio-Cortez",
    outcome: "Yes",
    slug: "democratic-nominee-2028-aoc"
  },
  "65176072261324737856085688071627118509549293922582857186996392180609764586527": {
    marketName: "Democratic Nominee 2028",
    candidate: "Alexandria Ocasio-Cortez",
    outcome: "No",
    slug: "democratic-nominee-2028-aoc"
  }
};

export class StateStore {
  constructor(options = {}) {
    this.redisUrl = options.redisUrl || process.env.REDIS_URL || 'redis://127.0.0.1:6379';
    this.redisClient = null;
    this.redisOnline = false;
    this.inMemoryCache = new Map();
    this.totalUpdates = 0;
    this.startTime = Date.now();
  }

  async init() {
    try {
      this.redisClient = new Redis(this.redisUrl, {
        maxRetriesPerRequest: 1,
        retryStrategy: () => null, // Do not spam retries if Redis is not running
        enableOfflineQueue: false,
        connectTimeout: 800,
      });

      this.redisClient.on('connect', () => {
        this.redisOnline = true;
        console.log(`[StateStore] Connected to Redis at ${this.redisUrl}`);
      });

      this.redisClient.on('error', (err) => {
        if (this.redisOnline) {
          console.warn(`[StateStore] Redis connection lost: ${err.message}`);
        }
        this.redisOnline = false;
      });

      // Quick test ping
      await Promise.race([
        this.redisClient.ping().then(() => { this.redisOnline = true; }),
        new Promise((_, reject) => setTimeout(() => reject(new Error('timeout')), 800))
      ]);
    } catch {
      this.redisOnline = false;
      if (this.redisClient) {
        try { this.redisClient.disconnect(false); } catch {}
        this.redisClient = null;
      }
      console.log(`[StateStore] Redis unavailable (${this.redisUrl}); running in fast In-Memory cache mode.`);
    }
  }

  async updateBookState(bookMessage) {
    this.totalUpdates++;
    const assetId = bookMessage.asset_id;
    if (!assetId) return;

    const enriched = {
      ...bookMessage,
      metadata: MARKET_METADATA[assetId] || { marketName: 'Unknown Market', outcome: 'Unknown' },
      gatewayReceivedAt: Date.now()
    };

    // 1. In-memory update
    this.inMemoryCache.set(assetId, enriched);

    // 2. Redis update if available
    if (this.redisOnline && this.redisClient) {
      try {
        const key = `orderbook:state:${assetId}`;
        await this.redisClient.set(key, JSON.stringify(enriched), 'EX', 3600);
      } catch (err) {
        this.redisOnline = false;
        console.warn(`[StateStore] Failed writing to Redis: ${err.message}`);
      }
    }
  }

  async getBookState(assetId) {
    if (this.redisOnline && this.redisClient) {
      try {
        const raw = await this.redisClient.get(`orderbook:state:${assetId}`);
        if (raw) return JSON.parse(raw);
      } catch {
        this.redisOnline = false;
      }
    }
    return this.inMemoryCache.get(assetId) || null;
  }

  async getAllMarkets() {
    const results = [];
    for (const [assetId, meta] of Object.entries(MARKET_METADATA)) {
      const state = await this.getBookState(assetId);
      results.push({
        asset_id: assetId,
        metadata: meta,
        state: state?.state || 'AWAITING_FEED',
        best_bid: state?.best_bid ?? null,
        best_ask: state?.best_ask ?? null,
        spread: state?.spread ?? null,
        mid_price: state?.mid_price ?? null,
        micro_price: state?.micro_price ?? null,
        imbalance: state?.imbalance ?? null,
        last_updated: state?.timestamp_ms ?? null
      });
    }
    return results;
  }

  getHealth() {
    return {
      status: 'healthy',
      uptime_seconds: Math.floor((Date.now() - this.startTime) / 1000),
      redis: {
        enabled: this.redisOnline,
        url: this.redisUrl,
      },
      cached_markets_count: this.inMemoryCache.size,
      total_updates_processed: this.totalUpdates
    };
  }
}
