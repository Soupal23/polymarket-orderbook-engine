import { spawn, execSync } from 'child_process';
import path from 'path';
import { fileURLToPath } from 'url';
import assert from 'assert';

const __filename = fileURLToPath(import.meta.url);
const __dirname = path.dirname(__filename);
const rootDir = path.resolve(__dirname, '../..');

const engineExe = path.join(rootDir, 'build/live_engine.exe');
const goldenData = path.join(rootDir, 'data/golden_sample.jsonl');

console.log('[E2E Test] Starting C++ Core Engine (live_engine.exe)...');
const engineProc = spawn(engineExe, [
  '--replay', goldenData,
  '--speed', '0',
  '--port', '9001'
], {
  cwd: rootDir,
  stdio: ['ignore', 'pipe', 'pipe']
});

engineProc.stdout.on('data', (d) => console.log(`[Engine] ${d.toString().trim()}`));
engineProc.stderr.on('data', (d) => console.error(`[Engine Error] ${d.toString().trim()}`));

// Give engine a moment to start TCP listening
setTimeout(async () => {
  console.log('[E2E Test] Starting Node.js Gateway...');
  const gatewayProc = spawn('node', ['server.js'], {
    cwd: path.join(rootDir, 'gateway'),
    stdio: ['ignore', 'pipe', 'pipe'],
    env: { ...process.env, PORT: '4000', CORE_TCP_PORT: '9001' }
  });

  gatewayProc.stdout.on('data', async (d) => {
    const text = d.toString();
    console.log(`[Gateway] ${text.trim()}`);
    if (text.includes('HTTP & Socket.IO server running') && !started) {
      started = true;
      runTests();
    }
  });
  gatewayProc.stderr.on('data', (d) => console.error(`[Gateway Error] ${d.toString().trim()}`));

  let started = false;
  async function runTests() {
    // Wait an additional 300ms for coreClient socket handshake
    await new Promise((r) => setTimeout(r, 300));
    try {
      console.log('\n[E2E Test] Probing Gateway endpoints...');

      // 1. Check Health
      const healthRes = await fetch('http://127.0.0.1:4000/api/health');
      assert.strictEqual(healthRes.status, 200, 'Health endpoint should return 200');
      const health = await healthRes.json();
      console.log('✓ Gateway Health:', JSON.stringify(health.core_engine));
      assert.strictEqual(health.core_engine.connected, true, 'Engine should be connected');

      // 2. Check Markets
      const marketsRes = await fetch('http://127.0.0.1:4000/api/markets');
      assert.strictEqual(marketsRes.status, 200, 'Markets endpoint should return 200');
      const markets = await marketsRes.json();
      console.log(`✓ Markets count: ${markets.length}`);
      assert.ok(markets.length >= 8, 'Expected 8 tracked outcome tokens');

      // 3. Check populated orderbook state for golden_asset_yes
      const stateRes = await fetch('http://127.0.0.1:4000/api/state/golden_asset_yes');
      assert.strictEqual(stateRes.status, 200, 'State endpoint should return 200 for replayed asset');
      const state = await stateRes.json();
      assert.strictEqual(state.state, 'LIVE', 'Orderbook state should be LIVE');
      assert.ok(state.bids.length > 0 && state.asks.length > 0, 'Orderbook should have populated bids and asks');
      console.log(`✓ Book State LIVE: best_bid=${state.best_bid}, best_ask=${state.best_ask}, bids=${state.bids.length}, asks=${state.asks.length}`);

      // 4. Check Dashboard Static Serving
      const pageRes = await fetch('http://127.0.0.1:4000/');
      assert.strictEqual(pageRes.status, 200, 'Frontend dashboard index.html should return 200');
      const htmlText = await pageRes.text();
      assert.ok(htmlText.includes('Polymarket CLOB'), 'Dashboard HTML should contain title');
      console.log('✓ React Dashboard served successfully over HTTP port 4000');

      console.log('\n=============================================');
      console.log('>>> FULL-STACK E2E INTEGRATION TEST PASSED! <<<');
      console.log('=============================================\n');

      // Cleanly terminate child processes
      if (process.platform === 'win32') {
        try { execSync(`taskkill /F /T /PID ${gatewayProc.pid}`, { stdio: 'ignore' }); } catch {}
        try { execSync(`taskkill /F /T /PID ${engineProc.pid}`, { stdio: 'ignore' }); } catch {}
      } else {
        gatewayProc.kill();
        engineProc.kill();
      }
      process.exit(0);
    } catch (err) {
      console.error('\nE2E Test Failed:', err);
      if (process.platform === 'win32') {
        try { execSync(`taskkill /F /T /PID ${gatewayProc.pid}`, { stdio: 'ignore' }); } catch {}
        try { execSync(`taskkill /F /T /PID ${engineProc.pid}`, { stdio: 'ignore' }); } catch {}
      } else {
        gatewayProc.kill();
        engineProc.kill();
      }
      process.exit(1);
    }
  }
}, 500);
