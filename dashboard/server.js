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
let latestData = { rpm: 0, direction: 'STOPPED', on: false, connected: false, port: null, baud: DEFAULT_BAUD };

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
      const parsed = parseLine(line.trim());
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
  setTimeout(() => { if (!res.headersSent) res.status(500).json({ error: 'Connection timed out' }); }, 5000);
});

app.post('/api/disconnect', (_req, res) => { disconnectSerial(); res.json({ ok: true }); });

function disconnectSerial() {
  if (serial) { try { serial.close(); } catch (_) {} serial = null; }
  latestData.connected = false; broadcast(latestData);
}

function parseLine(line) {
  if (!line || line.startsWith('=') || line.startsWith('ERR')) return null;
  const result = {};
  // Status line: P:<pos>,E:<err>,V:<vel>,T:<target>,F:<fault>,M:<moving>,A:<at_target>,Q:<qidx>,QL:<qlen>
  // Q/QL optional for backward compatibility with pre-v5.2.0 firmware
  const m = /P:(-?\d+),E:(-?\d+),V:(-?\d+),T:(-?\d+),F:(\d+),M:(\d+),A:(\d+)(?:,Q:(\d+),QL:(\d+))?/.exec(line);
  if (m) {
    result.position = parseInt(m[1], 10);
    result.error = parseInt(m[2], 10);
    result.velocity = parseInt(m[3], 10);
    result.target = parseInt(m[4], 10);
    result.fault = m[5] === '1';
    result.moving = m[6] === '1';
    result.atTarget = m[7] === '1';
    if (m[8] !== undefined) result.qidx = parseInt(m[8], 10);
    if (m[9] !== undefined) result.qlen = parseInt(m[9], 10);
    return Object.keys(result).length ? result : null;
  }
  // GET response: T=...,PROFILE=S/T,ACCEL=,JERK=,MAXV=,KD=,I=,US= etc.
  const p = /PROFILE=([ST])/.exec(line);
  if (p) result.profile = p[1];
  const a = /ACCEL=(-?\d+)/.exec(line);
  if (a) result.accel = parseInt(a[1], 10);
  const j = /JERK=(-?\d+)/.exec(line);
  if (j) result.jerk = parseInt(j[1], 10);
  const mv = /MAXV=(-?\d+)/.exec(line);
  if (mv) result.maxv = parseInt(mv[1], 10);
  const kd = /KD=(-?\d+)/.exec(line);
  if (kd) result.kd = parseInt(kd[1], 10);
  const i = /I=(-?\d+)/.exec(line);
  if (i) result.i = parseInt(i[1], 10);
  const us = /US=(-?\d+)/.exec(line);
  if (us) result.us = parseInt(us[1], 10);
  const qlen = /QLEN=(\d+)/.exec(line);
  if (qlen) result.qlen = parseInt(qlen[1], 10);
  const qidx = /QIDX=(\d+)/.exec(line);
  if (qidx) result.qidx = parseInt(qidx[1], 10);
  const dwell = /DWELL=(\d+)/.exec(line);
  if (dwell) result.dwell = parseInt(dwell[1], 10);
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
