const express = require('express');
const http = require('http');
const { WebSocketServer } = require('ws');
const { SerialPort } = require('serialport');
const path = require('path');

const PORT = process.env.UI_PORT || 3000;
const DEFAULT_BAUD = parseInt(process.env.SERIAL_BAUD || '19200', 10);

const app = express();
app.use(express.static(path.join(__dirname, 'public')));
app.use(express.json());

const server = http.createServer(app);
const wss = new WebSocketServer({ server });

let serial = null;
let latestData = {
  position: 0, target: 0, error: 0, velocity: 0,
  fault: false, homed: false, moving: false,
  connected: false, port: null, baud: DEFAULT_BAUD
};

function broadcast(data) {
  const msg = JSON.stringify(data);
  wss.clients.forEach(c => { if (c.readyState === 1) c.send(msg); });
}

app.get('/api/ports', async (_req, res) => {
  try {
    const ports = await SerialPort.list();
    res.json(ports.filter(p => p.path).map(p => ({
      path: p.path,
      manufacturer: p.manufacturer || '',
      productId: p.productId || '',
      serialNumber: p.serialNumber || '',
    })));
  } catch (err) { res.status(500).json({ error: err.message }); }
});

app.post('/api/connect', (req, res) => {
  const { path: portPath, baud } = req.body;
  if (!portPath) return res.status(400).json({ error: 'Missing path' });
  if (serial && serial.isOpen) serial.close();

  const baudRate = parseInt(baud, 10) || DEFAULT_BAUD;
  serial = new SerialPort({ path: portPath, baudRate, autoOpen: true });
  let buffer = '';

  serial.on('open', () => {
    console.log(`Connected: ${portPath} @ ${baudRate} baud`);
    latestData.connected = true; latestData.port = portPath; latestData.baud = baudRate;
    broadcast(latestData); res.json({ ok: true });
  });

  serial.on('data', chunk => {
    buffer += chunk.toString('utf-8');
    const lines = buffer.split('\n');
    buffer = lines.pop();
    for (const line of lines) {
      const parsed = parseStatusLine(line.trim());
      if (parsed) {
        Object.assign(latestData, parsed);
        latestData.connected = true;
        broadcast(latestData);
      }
    }
  });

  serial.on('error', err => {
    console.error('Serial error:', err.message);
    if (!res.headersSent) res.status(500).json({ error: err.message });
    disconnectSerial();
  });

  serial.on('close', () => disconnectSerial());

  setTimeout(() => {
    if (!res.headersSent) res.status(500).json({ error: 'Connection timed out' });
  }, 5000);
});

app.post('/api/disconnect', (_req, res) => { disconnectSerial(); res.json({ ok: true }); });

function disconnectSerial() {
  if (serial) { try { serial.close(); } catch (_) {} serial = null; }
  latestData.connected = false; broadcast(latestData);
}

function parseStatusLine(line) {
  if (!line || line.startsWith('=') || line.startsWith('OK') || line.startsWith('ERR') || line.startsWith('T=')) return null;
  const fields = ['P', 'E', 'V', 'T', 'F', 'H', 'M'];
  const result = {};
  for (const f of fields) {
    const re = new RegExp(`${f}:(-?\\d+)`);
    const m = re.exec(line);
    if (m) {
      const val = parseInt(m[1], 10);
      switch (f) {
        case 'P': result.position = val; break;
        case 'E': result.error = val; break;
        case 'V': result.velocity = val; break;
        case 'T': result.target = val; break;
        case 'F': result.fault = val !== 0; break;
        case 'H': result.homed = val !== 0; break;
        case 'M': result.moving = val !== 0; break;
      }
    }
  }
  return Object.keys(result).length ? result : null;
}

wss.on('connection', (ws) => {
  ws.send(JSON.stringify(latestData));
  ws.on('message', (data) => {
    if (!serial || !serial.isOpen) return;
    try {
      const msg = JSON.parse(data.toString());
      if (msg.command) serial.write(msg.command + '\n');
    } catch (_) {}
  });
});

server.listen(PORT, () => console.log(`Dashboard → http://localhost:${PORT}`));
