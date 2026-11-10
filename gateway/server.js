import http from 'http';
import path from 'path';
import { fileURLToPath } from 'url';
import express from 'express';
import cors from 'cors';
import dotenv from 'dotenv';
import { CoreClient } from './src/coreClient.js';
import { StateStore, MARKET_METADATA } from './src/stateStore.js';
import { PersistenceWorker } from './src/persistenceWorker.js';
import { Broadcaster } from './src/broadcaster.js';

dotenv.config();

const __filename = fileURLToPath(import.meta.url);
const __dirname = path.dirname(__filename);
const distPath = path.resolve(__dirname, '../dashboard/dist');

const PORT = Number(process.env.PORT || 4000);
const CORE_PORT = Number(process.env.CORE_TCP_PORT || 9001);
const CORE_HOST = process.env.CORE_TCP_HOST || '127.0.0.1';

const app = express();
app.use(cors());
app.use(express.json());
app.use(express.static(distPath));

const server = http.createServer(app);

// Initialize subsystem components
const stateStore = new StateStore();
const persistenceWorker = new PersistenceWorker();
const broadcaster = new Broadcaster(server, stateStore, { throttleIntervalMs: 50 });
const coreClient = new CoreClient({ host: CORE_HOST, port: CORE_PORT });

// Wire Core Engine events into Gateway pipelines
coreClient.on('connection', (status) => {
  broadcaster.broadcastEngineStatus(status);
});

coreClient.on('book_update', async (msg) => {
  await stateStore.updateBookState(msg);
  broadcaster.broadcastBookUpdate(msg);

  // Periodically persist snapshot records
  if (msg.event === 'snapshot') {
    await persistenceWorker.recordSnapshot(msg);
  }
});

coreClient.on('trade', async (tradeMsg) => {
  await persistenceWorker.recordTrade(tradeMsg);
  broadcaster.broadcastTrade(tradeMsg);
});

coreClient.on('heartbeat', (hbMsg) => {
  broadcaster.broadcastHeartbeat(hbMsg);
});

// REST API Endpoints

// 1. Health & Status
app.get('/api/health', (req, res) => {
  res.json({
    gateway: {
      uptime_seconds: Math.floor(process.uptime()),
      connected_clients: broadcaster.getClientCount(),
      port: PORT
    },
    core_engine: coreClient.getStatus(),
    state_store: stateStore.getHealth(),
    persistence: persistenceWorker.getHealth()
  });
});

// 2. Market List Overview
app.get('/api/markets', async (req, res) => {
  try {
    const markets = await stateStore.getAllMarkets();
    res.json(markets);
  } catch (err) {
    res.status(500).json({ error: err.message });
  }
});

// 3. Specific Orderbook State & Depth
app.get('/api/state/:asset_id', async (req, res) => {
  const { asset_id } = req.params;
  try {
    const state = await stateStore.getBookState(asset_id);
    if (!state) {
      return res.status(404).json({ error: 'Asset not found or no feed data received yet' });
    }
    res.json(state);
  } catch (err) {
    res.status(500).json({ error: err.message });
  }
});

// 4. Trade History
app.get('/api/trades/:asset_id?', async (req, res) => {
  const { asset_id } = req.params;
  const limit = Math.min(Number(req.query.limit || 50), 200);
  try {
    const trades = await persistenceWorker.getRecentTrades(asset_id, limit);
    res.json(trades);
  } catch (err) {
    res.status(500).json({ error: err.message });
  }
});

// 5. Fault Simulation Endpoint for UI Demonstration
app.post('/api/simulate/fault', async (req, res) => {
  const { asset_id, type } = req.body;
  if (!asset_id) {
    return res.status(400).json({ error: 'asset_id required' });
  }

  // Inject visual STALE state into broadcast stream
  const currentState = await stateStore.getBookState(asset_id) || {};
  const faultedState = {
    ...currentState,
    asset_id,
    state: 'STALE',
    stale_reason: type === 'cross' ? 'Injected crossed book invariant breach (bid > ask)' : 'Feed heartbeat timeout / packet drop'
  };

  await stateStore.updateBookState(faultedState);
  broadcaster.broadcastBookUpdate(faultedState);

  // Schedule auto-recovery after 2.5 seconds to showcase self-healing
  setTimeout(async () => {
    const recoveredState = {
      ...faultedState,
      state: 'REBUILDING',
      stale_reason: 'Requesting fresh REST snapshot from Polymarket CLOB...'
    };
    await stateStore.updateBookState(recoveredState);
    broadcaster.broadcastBookUpdate(recoveredState);

    setTimeout(async () => {
      const liveState = {
        ...recoveredState,
        state: 'LIVE',
        stale_reason: ''
      };
      await stateStore.updateBookState(liveState);
      broadcaster.broadcastBookUpdate(liveState);
    }, 1500);
  }, 2500);

  res.json({ success: true, message: `Fault '${type || 'stale'}' injected; recovery scheduled.` });
});

// SPA fallback for Dashboard frontend
app.get('*', (req, res, next) => {
  if (req.path.startsWith('/api') || req.path.startsWith('/socket.io')) {
    return next();
  }
  res.sendFile(path.join(distPath, 'index.html'));
});

// Bootstrap server
async function startServer() {
  await stateStore.init();
  await persistenceWorker.init(stateStore.redisOnline);

  server.listen(PORT, () => {
    console.log(`[Gateway] HTTP & Socket.IO server running on http://127.0.0.1:${PORT}`);
    coreClient.connect();
  });
}

startServer().catch((err) => {
  console.error('[Gateway] Failed to start server:', err);
  process.exit(1);
});
