import { spawn } from 'child_process';
import path from 'path';
import { fileURLToPath } from 'url';

const __filename = fileURLToPath(import.meta.url);
const __dirname = path.dirname(__filename);

const engineMode = process.env.ENGINE_MODE || 'live';
const coreTcpPort = process.env.CORE_TCP_PORT || '9001';
const replayFile = process.env.REPLAY_FILE || 'data/golden_sample.jsonl';
const replaySpeed = process.env.REPLAY_SPEED || '1.0';

const enginePath = path.join(__dirname, 'build', process.platform === 'win32' ? 'live_engine.exe' : 'live_engine');
const engineArgs = engineMode === 'replay'
  ? ['--replay', replayFile, '--speed', replaySpeed, '--port', coreTcpPort]
  : ['--live', '--port', coreTcpPort];

console.log(`[Entrypoint] Starting C++ Core Engine: ${enginePath} ${engineArgs.join(' ')}`);

let shuttingDown = false;
let engineProc = null;
let gatewayProc = null;

function startEngine() {
  engineProc = spawn(enginePath, engineArgs, {
    cwd: __dirname,
    stdio: 'inherit'
  });

  engineProc.on('exit', (code, signal) => {
    console.log(`[Entrypoint] live_engine exited (code=${code}, signal=${signal})`);
    if (!shuttingDown) {
      console.log('[Entrypoint] Restarting live_engine in 2 seconds...');
      setTimeout(startEngine, 2000);
    }
  });
}

function startGateway() {
  console.log('[Entrypoint] Starting Node.js Gateway...');
  gatewayProc = spawn('node', ['server.js'], {
    cwd: path.join(__dirname, 'gateway'),
    stdio: 'inherit',
    env: {
      ...process.env,
      CORE_TCP_HOST: '127.0.0.1',
      CORE_TCP_PORT: coreTcpPort
    }
  });

  gatewayProc.on('exit', (code, signal) => {
    console.log(`[Entrypoint] gateway exited (code=${code}, signal=${signal})`);
    if (!shuttingDown) {
      shutdown(code || 1);
    }
  });
}

function shutdown(exitCode = 0) {
  if (shuttingDown) return;
  shuttingDown = true;
  console.log('[Entrypoint] Graceful shutdown initiated...');
  if (gatewayProc && !gatewayProc.killed) gatewayProc.kill('SIGTERM');
  if (engineProc && !engineProc.killed) engineProc.kill('SIGTERM');
  setTimeout(() => process.exit(exitCode), 500);
}

process.on('SIGINT', () => shutdown(0));
process.on('SIGTERM', () => shutdown(0));

startEngine();
setTimeout(startGateway, 300);
