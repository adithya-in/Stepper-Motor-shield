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
let latestData = { rpm: 0, direction: 'STOPPED', connected: false, port: null, baud: DEFAULT_BAUD };

function broadcast(data) {
  const msg = JSON.stringify(data);
  wss.clients.forEach((client) => {
    if (client.readyState === 1) client.send(msg);
  });
}

// --- Serial port listing ---
app.get('/api/ports', async (_req, res) => {
  try {
    const ports = await SerialPort.list();
    const list = ports
      .filter((p) => p.path && (p.vendorId || p.manufacturer || p.productId || p.serialNumber))
      .map((p) => ({
        path: p.path,
        manufacturer: p.manufacturer || '',
        productId: p.productId || '',
        serialNumber: p.serialNumber || '',
      }));
    res.json(list);
  } catch (err) {
    res.status(500).json({ error: err.message });
  }
});

// --- Connect to serial port ---
app.post('/api/connect', (req, res) => {
  const { path: portPath, baud } = req.body;
  if (!portPath) return res.status(400).json({ error: 'Missing path' });

  if (serial && serial.isOpen) {
    serial.close();
  }

  const baudRate = parseInt(baud, 10) || DEFAULT_BAUD;

  serial = new SerialPort({ path: portPath, baudRate, autoOpen: true });

  let buffer = '';

  serial.on('open', () => {
    console.log(`Connected: ${portPath} @ ${baudRate} baud`);
    latestData.connected = true;
    latestData.port = portPath;
    latestData.baud = baudRate;
    broadcast(latestData);
    res.json({ ok: true });
  });

  serial.on('data', (chunk) => {
    buffer += chunk.toString('utf-8');
    const lines = buffer.split('\n');
    buffer = lines.pop();

    for (const line of lines) {
      const parsed = parseLine(line);
      if (parsed) {
        if (parsed.rpm !== undefined) latestData.rpm = parsed.rpm;
        if (parsed.direction !== undefined) latestData.direction = parsed.direction;
        latestData.connected = true;
        broadcast(latestData);
      }
    }
  });

  serial.on('error', (err) => {
    console.error('Serial error:', err.message);
    if (!res.headersSent) {
      res.status(500).json({ error: err.message });
    }
    disconnectSerial();
  });

  serial.on('close', () => {
    disconnectSerial();
  });

  // timeout if open fails silently
  setTimeout(() => {
    if (!res.headersSent) {
      res.status(500).json({ error: 'Connection timed out' });
    }
  }, 5000);
});

// --- Disconnect ---
app.post('/api/disconnect', (_req, res) => {
  disconnectSerial();
  res.json({ ok: true });
});

function disconnectSerial() {
  if (serial) {
    try { serial.close(); } catch (_) {}
    serial = null;
  }
  latestData.connected = false;
  broadcast(latestData);
}

// --- Line parser ---
function parseLine(line) {
  const trimmed = line.trim();
  if (!trimmed) return null;

  try {
    const parsed = JSON.parse(trimmed);
    const result = {};
    if (parsed.rpm !== undefined) result.rpm = parseFloat(parsed.rpm);
    if (parsed.direction !== undefined) result.direction = parsed.direction;
    if (parsed.dir !== undefined) result.direction = parsed.dir;
    return Object.keys(result).length ? result : null;
  } catch (_) {}

  const result = {};
  const kvPattern = /(RPM|DIR|DIRECTION|MOTOR|STATE)\s*[:=]\s*(\S+)/gi;
  let match;
  while ((match = kvPattern.exec(trimmed)) !== null) {
    if (match[1].toLowerCase() === 'rpm') result.rpm = parseFloat(match[2]) || 0;
    else if (['dir', 'direction'].includes(match[1].toLowerCase())) result.direction = match[2].toUpperCase();
  }
  if (result.rpm !== undefined || result.direction !== undefined) return result;

  const parts = trimmed.split(/[,\s]+/);
  if (parts.length >= 2) {
    const rpm = parseFloat(parts[0]);
    if (!isNaN(rpm)) {
      result.rpm = rpm;
      result.direction = parts[1].toUpperCase();
      return result;
    }
  }

  return null;
}

wss.on('connection', (ws) => {
  ws.send(JSON.stringify(latestData));
});

server.listen(PORT, () => {
  console.log(`Dashboard → http://localhost:${PORT}`);
});
