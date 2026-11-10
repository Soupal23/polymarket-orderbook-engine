import Redis from 'ioredis';

// Market metadata map for the 8 outcome tokens across 4 markets
export const MARKET_METADATA = {
  "95574512423985392683838234327660995166649195744297839352932371900160867086053": {
    marketName: "Democratic Nominee 2028",
    candidate: "Gavin Newsom",
    outcome: "Yes",
    slug: "democratic-nominee-2028-newsom"
  },
  "57529458925585098319698544835003509172087541244301597554900898517616149175404": {
    marketName: "Democratic Nominee 2028",
    candidate: "Gavin Newsom",
    outcome: "No",
    slug: "democratic-nominee-2028-newsom"
  },
  "91104593444265738604715201416805886361899173827299102435552391038562777174624": {
    marketName: "Xi Jinping General Secretary in 2027",
    candidate: "Xi Jinping",
    outcome: "Yes",
    slug: "xi-jinping-leader-2027"
  },
  "50831631527376041071060938450117454911019058693895995079860655297394947932617": {
    marketName: "Xi Jinping General Secretary in 2027",
    candidate: "Xi Jinping",
    outcome: "No",
    slug: "xi-jinping-leader-2027"
  },
  "85802166699665551322055621404177196025287739546059003588998188151242817290547": {
    marketName: "Democratic Nominee 2028",
    candidate: "Pete Buttigieg",
    outcome: "Yes",
    slug: "democratic-nominee-2028-buttigieg"
  },
  "20111267499119642008544577884841961448831934988755018654854593121557997970724": {
    marketName: "Democratic Nominee 2028",
    candidate: "Pete Buttigieg",
    outcome: "No",
    slug: "democratic-nominee-2028-buttigieg"
  },
  "21742633143463906290569050155826241533067272736897614950488156847949938836455": {
    marketName: "Democratic Nominee 2028",
    candidate: "Alexandria Ocasio-Cortez",
    outcome: "Yes",
    slug: "democratic-nominee-2028-aoc"
  },
  "108845704514578847849479901460395349472946252924163953531498114498305096181156": {
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
