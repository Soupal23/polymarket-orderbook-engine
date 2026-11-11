import { Server } from 'socket.io';

export class Broadcaster {
  constructor(httpServer, stateStore, options = {}) {
    this.io = new Server(httpServer, {
      cors: {
        origin: '*',
        methods: ['GET', 'POST']
      }
    });

    this.stateStore = stateStore;
    this.throttleIntervalMs = options.throttleIntervalMs || 50; // Max 20 updates/sec per token
    this.pendingUpdates = new Map(); // asset_id -> latest message
    this.lastEmitTimes = new Map(); // asset_id -> timestamp
    this.throttleTimers = new Map(); // asset_id -> timeout handle
    this.connectedSockets = 0;
    this.coreConnected = false;

    this.setupSocketHandlers();
  }

  setupSocketHandlers() {
    this.io.on('connection', async (socket) => {
      this.connectedSockets++;
      console.log(`[Broadcaster] Client connected: ${socket.id} (total: ${this.connectedSockets})`);

      // Immediately send current market overview and engine status to newly connected client
      try {
        const allMarkets = await this.stateStore.getAllMarkets();
        socket.emit('initial_state', { markets: allMarkets });
        socket.emit('engine_status', { connected: this.coreConnected });
      } catch (err) {
        console.error(`[Broadcaster] Failed sending initial state:`, err.message);
      }

      socket.on('subscribe', async (assetId) => {
        if (assetId) {
          socket.join(assetId);
          try {
            const current = await this.stateStore.getBookState(assetId);
            if (current) {
              socket.emit('book_update', current);
            }
          } catch (err) {
            console.error(`[Broadcaster] Failed sending initial book state:`, err.message);
          }
        }
      });

      socket.on('unsubscribe', (assetId) => {
        if (assetId) {
          socket.leave(assetId);
        }
      });

      socket.on('disconnect', () => {
        this.connectedSockets--;
        console.log(`[Broadcaster] Client disconnected: ${socket.id} (remaining: ${this.connectedSockets})`);
      });
    });
  }

  // Coalesced / throttled broadcast for orderbook updates
  broadcastBookUpdate(updateMsg) {
    const assetId = updateMsg.asset_id;
    if (!assetId) {
      this.io.emit('book_update', updateMsg);
      return;
    }

    this.pendingUpdates.set(assetId, updateMsg);

    const now = Date.now();
    const lastEmit = this.lastEmitTimes.get(assetId) || 0;
    const timeSinceLastEmit = now - lastEmit;

    if (timeSinceLastEmit >= this.throttleIntervalMs) {
      this.flushUpdate(assetId);
    } else {
      if (!this.throttleTimers.has(assetId)) {
        const delay = this.throttleIntervalMs - timeSinceLastEmit;
        const timer = setTimeout(() => {
          this.throttleTimers.delete(assetId);
          this.flushUpdate(assetId);
        }, delay);
        this.throttleTimers.set(assetId, timer);
      }
    }
  }

  flushUpdate(assetId) {
    const msg = this.pendingUpdates.get(assetId);
    if (!msg) return;

    this.lastEmitTimes.set(assetId, Date.now());
    this.pendingUpdates.delete(assetId);

    // Emit to subscribed room and to general feed
    this.io.to(assetId).emit('book_update', msg);
    this.io.emit('market_bbo_update', {
      asset_id: msg.asset_id,
      state: msg.state,
      best_bid: msg.best_bid,
      best_ask: msg.best_ask,
      spread: msg.spread,
      mid_price: msg.mid_price,
      micro_price: msg.micro_price,
      imbalance: msg.imbalance,
      timestamp_ms: msg.timestamp_ms
    });
  }

  // Trades are high-value and infrequent: emit immediately
  broadcastTrade(tradeMsg) {
    this.io.emit('trade', tradeMsg);
    if (tradeMsg.asset_id) {
      this.io.to(tradeMsg.asset_id).emit('trade', tradeMsg);
    }
  }

  broadcastHeartbeat(hbMsg) {
    this.io.emit('heartbeat', hbMsg);
  }

  broadcastEngineStatus(status) {
    this.coreConnected = Boolean(status?.connected);
    this.io.emit('engine_status', status);
  }

  getClientCount() {
    return this.connectedSockets;
  }
}
