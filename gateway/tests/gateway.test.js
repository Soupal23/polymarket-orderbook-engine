import net from 'net';
import http from 'http';
import assert from 'assert';

const TEST_CORE_PORT = 9088;
const TEST_GATEWAY_PORT = 4088;

process.env.PORT = String(TEST_GATEWAY_PORT);
process.env.CORE_TCP_PORT = String(TEST_CORE_PORT);

// Start mock C++ engine TCP server
let clientSocket = null;
const mockEngine = net.createServer((socket) => {
  clientSocket = socket;
  console.log('[MockEngine] Gateway client connected.');
  // Send test snapshot immediately
  const sampleMsg = {
    event: 'snapshot',
    asset_id: '54533043819946592547517511176940999955633860128497669742211153063842200957669',
    market_id: 'test_market',
    state: 'LIVE',
    stale_reason: '',
    timestamp_ms: 1720000000000,
    best_bid: 0.48,
    best_ask: 0.52,
    spread: 0.04,
    mid_price: 0.50,
    micro_price: 0.495,
    imbalance: 0.2,
    total_bid_qty: 5000,
    total_ask_qty: 4000,
    bids: [[0.48, 2500], [0.47, 2500]],
    asks: [[0.52, 2000], [0.53, 2000]]
  };

  socket.write(JSON.stringify(sampleMsg) + '\n');
});

mockEngine.listen(TEST_CORE_PORT, async () => {
  console.log(`[MockEngine] Listening on ${TEST_CORE_PORT}`);

  // Dynamically import server to bind to test port
  await import('../server.js');

  // Allow gateway to connect and receive initial snapshot
  setTimeout(async () => {
    try {
      // 1. Test /api/health
      const healthRes = await fetch(`http://127.0.0.1:${TEST_GATEWAY_PORT}/api/health`);
      const healthData = await healthRes.json();
      assert.strictEqual(healthRes.status, 200);
      assert.strictEqual(healthData.core_engine.connected, true);
      console.log('✓ /api/health test passed');

      // 2. Test /api/markets
      const marketsRes = await fetch(`http://127.0.0.1:${TEST_GATEWAY_PORT}/api/markets`);
      const marketsData = await marketsRes.json();
      assert.strictEqual(marketsRes.status, 200);
      assert.ok(marketsData.length >= 8);
      console.log('✓ /api/markets test passed');

      // 3. Test /api/state/:asset_id
      const assetId = '54533043819946592547517511176940999955633860128497669742211153063842200957669';
      const stateRes = await fetch(`http://127.0.0.1:${TEST_GATEWAY_PORT}/api/state/${assetId}`);
      const stateData = await stateRes.json();
      assert.strictEqual(stateRes.status, 200);
      assert.strictEqual(stateData.state, 'LIVE');
      assert.strictEqual(stateData.best_bid, 0.48);
      assert.strictEqual(stateData.best_ask, 0.52);
      console.log('✓ /api/state/:asset_id test passed');

      // 4. Test /api/simulate/fault
      const faultRes = await fetch(`http://127.0.0.1:${TEST_GATEWAY_PORT}/api/simulate/fault`, {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ asset_id: assetId, type: 'cross' })
      });
      const faultData = await faultRes.json();
      assert.strictEqual(faultRes.status, 200);
      assert.strictEqual(faultData.success, true);
      console.log('✓ /api/simulate/fault test passed');

      console.log('\nAll Gateway automated integration tests PASSED successfully!');
      if (clientSocket) clientSocket.destroy();
      mockEngine.close();
      setTimeout(() => process.exit(0), 100);
    } catch (err) {
      console.error('Integration test failed:', err);
      if (clientSocket) clientSocket.destroy();
      mockEngine.close();
      setTimeout(() => process.exit(1), 100);
    }
  }, 1500);
});
