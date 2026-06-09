const POS_RANGE = 10000;

const els = {
  posActual: document.getElementById('pos-actual'),
  posTarget: document.getElementById('pos-target'),
  posBarActual: document.getElementById('pos-bar-actual'),
  posBarTarget: document.getElementById('pos-bar-target'),
  errorValue: document.getElementById('error-value'),
  errorBarNeg: document.getElementById('error-bar-neg'),
  errorBarPos: document.getElementById('error-bar-pos'),
  velValue: document.getElementById('vel-value'),
  ledHomed: document.getElementById('led-homed'),
  ledMoving: document.getElementById('led-moving'),
  ledAtTarget: document.getElementById('led-at-target'),
  ledFault: document.getElementById('led-fault'),
  statusBadge: document.getElementById('status-badge'),
  connStatus: document.getElementById('conn-status'),
  portInfo: document.getElementById('port-info'),
  baudInfo: document.getElementById('baud-info'),
  timestamp: document.getElementById('timestamp'),
  modal: document.getElementById('modal-overlay'),
  portList: document.getElementById('port-list'),
  btnRefresh: document.getElementById('btn-refresh'),
  btnConnect: document.getElementById('btn-connect'),
  btnPorts: document.getElementById('btn-ports'),
  modalError: document.getElementById('modal-error'),
  baudInput: document.getElementById('baud-input'),
  targetInput: document.getElementById('target-input'),
  btnGo: document.getElementById('btn-go'),
  btnStop: document.getElementById('btn-stop'),
  btnHome: document.getElementById('btn-home'),
  btnClear: document.getElementById('btn-clear'),
  kpSlider: document.getElementById('kp-slider'),
  kiSlider: document.getElementById('ki-slider'),
  maxvSlider: document.getElementById('maxv-slider'),
  tolSlider: document.getElementById('tol-slider'),
  ftSlider: document.getElementById('ft-slider'),
  kpLabel: document.getElementById('kp-label'),
  kiLabel: document.getElementById('ki-label'),
  maxvLabel: document.getElementById('maxv-label'),
  tolLabel: document.getElementById('tol-label'),
  ftLabel: document.getElementById('ft-label'),
};

let selectedPort = null;
let connected = false;

function clamp(v, min, max) { return Math.max(min, Math.min(max, v)); }

function updatePosition(actual, target) {
  els.posActual.textContent = actual.toLocaleString();
  els.posTarget.textContent = target.toLocaleString();
  const aPct = clamp(((actual / POS_RANGE) + 1) * 50, 0, 100);
  const tPct = clamp(((target / POS_RANGE) + 1) * 50, 0, 100);
  els.posBarActual.style.width = aPct + '%';
  els.posBarTarget.style.width = tPct + '%';
}

function updateError(error) {
  els.errorValue.textContent = error.toLocaleString();
  els.errorValue.className = 'error-value';
  if (Math.abs(error) < 5) els.errorValue.classList.add('zero');
  else if (error > 0) els.errorValue.classList.add('positive');
  else els.errorValue.classList.add('negative');

  const ePct = clamp(Math.abs(error) / 500 * 50, 0, 50);
  if (error >= 0) { els.errorBarPos.style.width = ePct + '%'; els.errorBarNeg.style.width = '0%'; }
  else { els.errorBarNeg.style.width = ePct + '%'; els.errorBarPos.style.width = '0%'; }
}

function updateVelocity(vel) { els.velValue.textContent = vel.toLocaleString(); }

function updateLED(el, state, colorOn) {
  el.style.color = state ? colorOn : 'var(--border)';
  el.style.textShadow = state ? `0 0 8px ${colorOn}` : 'none';
}

function updateStatus(data) {
  updateLED(els.ledHomed, data.homed, 'var(--cyan)');
  updateLED(els.ledMoving, data.moving, 'var(--green)');
  updateLED(els.ledAtTarget, !data.moving && data.homed, 'var(--green)');
  updateLED(els.ledFault, data.fault, 'var(--red)');
  if (data.position !== undefined && data.target !== undefined) updatePosition(data.position, data.target);
  if (data.error !== undefined) updateError(data.error);
  if (data.velocity !== undefined) updateVelocity(data.velocity);
}

function updateConnection(data) {
  connected = !!data.connected;
  if (connected) {
    els.statusBadge.textContent = 'Connected';
    els.statusBadge.className = 'badge connected';
    els.connStatus.textContent = 'Connected';
    els.connStatus.style.color = 'var(--green)';
  } else {
    els.statusBadge.textContent = 'Disconnected';
    els.statusBadge.className = 'badge disconnected';
    els.connStatus.textContent = 'Disconnected';
    els.connStatus.style.color = 'var(--red)';
  }
  if (data.port) els.portInfo.textContent = data.port;
  if (data.baud) els.baudInfo.textContent = data.baud + ' baud';
}

function updateTimestamp() {
  const now = new Date();
  const pad = (n) => String(n).padStart(2, '0');
  els.timestamp.textContent = `Last update: ${now.getHours()}:${pad(now.getMinutes())}:${pad(now.getSeconds())}`;
}

// ── Port picker ──
async function fetchPorts() {
  els.portList.innerHTML = '<p class="muted">Scanning...</p>';
  els.btnConnect.disabled = true;
  selectedPort = null;
  try {
    const res = await fetch('/api/ports');
    const ports = await res.json();
    if (ports.length === 0) {
      els.portList.innerHTML = '<p class="muted">No serial devices found.</p>';
      return;
    }
    els.portList.innerHTML = '';
    ports.forEach(p => {
      const div = document.createElement('div');
      div.className = 'port-item';
      div.innerHTML = `<input type="radio" name="port" value="${p.path}"><div><div class="port-label">${p.path}</div><div class="port-desc">${p.manufacturer || p.productId || 'Unknown'}</div></div>`;
      div.querySelector('input').addEventListener('change', () => {
        document.querySelectorAll('.port-item').forEach(el => el.classList.remove('selected'));
        div.classList.add('selected');
        selectedPort = p.path;
        els.btnConnect.disabled = false;
      });
      div.addEventListener('click', () => div.querySelector('input').click());
      els.portList.appendChild(div);
    });
  } catch (err) { els.portList.innerHTML = `<p class="muted">Error: ${err.message}</p>`; }
}

function showModal() { els.modal.classList.add('active'); els.modalError.textContent = ''; fetchPorts(); }
function hideModal() { els.modal.classList.remove('active'); }

async function connectToPort() {
  if (!selectedPort) return;
  els.btnConnect.disabled = true; els.modalError.textContent = '';
  const baud = parseInt(document.getElementById('baud-input').value, 10) || 19200;
  try {
    const res = await fetch('/api/connect', {
      method: 'POST', headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ path: selectedPort, baud }),
    });
    const data = await res.json();
    if (!res.ok) throw new Error(data.error || 'Connection failed');
    hideModal();
  } catch (err) { els.modalError.textContent = err.message; els.btnConnect.disabled = false; }
}

// ── WebSocket ──
const protocol = location.protocol === 'https:' ? 'wss:' : 'ws:';
const ws = new WebSocket(`${protocol}//${location.host}`);

ws.onmessage = (event) => {
  try {
    const data = JSON.parse(event.data);
    if (data.connected !== undefined) updateConnection(data);
    updateStatus(data);
    updateTimestamp();
  } catch (e) {}
};

ws.onclose = () => updateConnection({ connected: false, port: null });

function sendCmd(cmd) { ws.send(JSON.stringify({ command: cmd })); }

// ── Control handlers ──
function goTarget() {
  const val = parseInt(els.targetInput.value, 10);
  if (!isNaN(val)) sendCmd('T=' + val);
}

function stopMotor() { sendCmd('STOP'); els.targetInput.value = '0'; }
function homeMotor() { sendCmd('HOME'); els.targetInput.value = '0'; }
function clearFault() { sendCmd('CLEAR'); }

// ── Tuning handlers ──
function sendTune(cmd, slider, label) {
  const val = parseInt(slider.value, 10);
  label.textContent = val;
  sendCmd(cmd + val);
}

// ── Events ──
els.btnGo.addEventListener('click', goTarget);
els.targetInput.addEventListener('keydown', (e) => { if (e.key === 'Enter') goTarget(); });
els.btnStop.addEventListener('click', stopMotor);
els.btnHome.addEventListener('click', homeMotor);
els.btnClear.addEventListener('click', clearFault);

els.kpSlider.addEventListener('input', () => sendTune('KP=', els.kpSlider, els.kpLabel));
els.kiSlider.addEventListener('input', () => sendTune('KI=', els.kiSlider, els.kiLabel));
els.maxvSlider.addEventListener('input', () => sendTune('MAXV=', els.maxvSlider, els.maxvLabel));
els.tolSlider.addEventListener('input', () => sendTune('TOL=', els.tolSlider, els.tolLabel));
els.ftSlider.addEventListener('input', () => sendTune('FT=', els.ftSlider, els.ftLabel));

els.btnPorts.addEventListener('click', showModal);
els.btnRefresh.addEventListener('click', fetchPorts);
els.btnConnect.addEventListener('click', connectToPort);
els.modal.addEventListener('click', (e) => { if (e.target === els.modal) hideModal(); });

setTimeout(() => { if (!connected) showModal(); }, 500);
