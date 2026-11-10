import net from 'net';
import { EventEmitter } from 'events';

export class CoreClient extends EventEmitter {
  constructor(options = {}) {
    super();
    this.host = options.host || process.env.CORE_TCP_HOST || '127.0.0.1';
    this.port = Number(options.port || process.env.CORE_TCP_PORT || 9001);
    this.reconnectIntervalMs = options.reconnectIntervalMs || 2000;
    this.socket = null;
    this.connected = false;
    this.buffer = '';
    this.shouldReconnect = true;
    this.reconnectTimer = null;
    this.messageCount = 0;
  }

  connect() {
    if (this.socket) {
      this.socket.destroy();
    }

    console.log(`[CoreClient] Connecting to C++ Core Engine at ${this.host}:${this.port}...`);
    this.socket = new net.Socket();
    this.socket.setNoDelay(true);

    this.socket.connect(this.port, this.host, () => {
      this.connected = true;
      console.log(`[CoreClient] Connected to C++ Core Engine successfully.`);
      this.emit('connection', { connected: true, host: this.host, port: this.port });
    });

    this.socket.on('data', (chunk) => {
      this.buffer += chunk.toString('utf8');
      let lineEndIndex;
      while ((lineEndIndex = this.buffer.indexOf('\n')) !== -1) {
        const line = this.buffer.slice(0, lineEndIndex).trim();
        this.buffer = this.buffer.slice(lineEndIndex + 1);

        if (line.length > 0) {
          this.handleMessage(line);
        }
      }
    });

    this.socket.on('close', () => {
      if (this.connected) {
        console.warn(`[CoreClient] Disconnected from C++ Core Engine.`);
      }
      this.connected = false;
      this.emit('connection', { connected: false });
      this.scheduleReconnect();
    });

    this.socket.on('error', (err) => {
      // Avoid crashing unhandled on connection refused
      this.emit('error', err);
    });
  }

  handleMessage(rawLine) {
    try {
      const msg = JSON.parse(rawLine);
      this.messageCount++;

      if (msg.event === 'snapshot' || msg.event === 'book_update') {
        this.emit('book_update', msg);
      } else if (msg.event === 'trade') {
        this.emit('trade', msg);
      } else if (msg.event === 'heartbeat') {
        this.emit('heartbeat', msg);
      } else {
        this.emit('message', msg);
      }
    } catch (err) {
      console.error(`[CoreClient] Failed to parse message line:`, err.message);
    }
  }

  scheduleReconnect() {
    if (!this.shouldReconnect || this.reconnectTimer) return;
    this.reconnectTimer = setTimeout(() => {
      this.reconnectTimer = null;
      if (!this.connected) {
        this.connect();
      }
    }, this.reconnectIntervalMs);
  }

  disconnect() {
    this.shouldReconnect = false;
    if (this.reconnectTimer) {
      clearTimeout(this.reconnectTimer);
      this.reconnectTimer = null;
    }
    if (this.socket) {
      this.socket.destroy();
      this.socket = null;
    }
    this.connected = false;
  }

  getStatus() {
    return {
      connected: this.connected,
      host: this.host,
      port: this.port,
      messagesProcessed: this.messageCount,
    };
  }
}
