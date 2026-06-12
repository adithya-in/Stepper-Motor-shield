const MAX_BAR = 500;

const els = {
  posTarget: document.getElementById('pos-target'),
  posActual: document.getElementById('pos-actual'),
  posBar: document.getElementById('pos-bar'),
  statusError: document.getElementById('status-error'),
  statusVel: document.getElementById('status-vel'),
  statusFault: document.getElementById('status-fault'),
  statusMoving: document.getElementById('status-moving'),
  statusATarget: document.getElementById('status-atarget'),
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
  btnZero: document.getElementById('btn-zero'),
  btnClear: document.getElementById('btn-clear'),
  kpSlider: document.getElementById('kp-slider'),
  kiSlider: document.getElementById('ki-slider'),
  kpLabel: document.getElementById('kp-label'),
  kiLabel: document.getElementById('ki-label'),
  maxvInput: document.getElementById('maxv-input'),
  tolInput: document.getElementById('tol-input'),
  ftInput: document.getElementById('ft-input'),
  btnMaxv: document.getElementById('btn-maxv'),
  btnTol: document.getElementById('btn-tol'),
  btnFt: document.getElementById('btn-ft'),
};

let selectedPort = null;
let connected = false;
let lastTarget = 0;

function updateBar(actual, target) {
  const center = MAX_BAR / 2;
  const diff = Math.abs(actual - target);
  const pct = Math.min(100, (diff / center) * 100);
  if (actual === target) {
    els.posBar.style.width = '2px';
    els.posBar.style.left = '50%';
    els.posBar.style.background = 'var(--green)';
  } else {
    const dir = actual < target ? 1 : -1;
    const fill = Math.max(2, pct * 2);
    els.posBar.style.width = Math.min(100, fill) + '%';
    els.posBar.style.left = dir > 0 ? '50%' : (50 - Math.min(50, fill)) + '%';
    els.posBar.style.background = 'var(--orange)';
  }
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
  const pad = n => String(n).padStart(2, '0');
  els.timestamp.textContent = `Update: ${now.getHours()}:${pad(now.getMinutes())}:${pad(now.getSeconds())}`;
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
    if (data.position !== undefined && data.target !== undefined) {
      els.posTarget.textContent = data.target.toLocaleString();
      els.posActual.textContent = data.position.toLocaleString();
      updateBar(data.position, data.target);
      lastTarget = data.target;
    }
    if (data.error !== undefined) els.statusError.textContent = data.error.toLocaleString();
    if (data.velocity !== undefined) els.statusVel.textContent = data.velocity + ' steps/s';
    if (data.fault !== undefined) els.statusFault.textContent = data.fault ? 'FAULT' : 'OK';
    if (data.moving !== undefined) els.statusMoving.textContent = data.moving ? 'Yes' : 'No';
    if (data.atTarget !== undefined) els.statusATarget.textContent = data.atTarget ? 'Yes' : 'No';
    if (data.connected !== undefined) updateConnection(data);
    if (data.profile !== undefined) {
      if (data.profile === 'S') {
        els.btnProfileS.classList.add('active');
        els.btnProfileT.classList.remove('active');
      } else {
        els.btnProfileT.classList.add('active');
        els.btnProfileS.classList.remove('active');
      }
    }
    if (data.accel !== undefined) els.accelInput.value = data.accel;
    if (data.jerk !== undefined) els.jerkInput.value = data.jerk;
    if (data.maxv !== undefined) els.maxvInput.value = data.maxv;
    updateTimestamp();
  } catch (e) {}
};

ws.onopen = () => setTimeout(() => sendCmd('GET'), 300);
ws.onclose = () => updateConnection({ connected: false, port: null });

function sendCmd(cmd) { ws.send(JSON.stringify({ command: cmd })); }

// ── Motor Test Controls ──
els.btnOn = document.getElementById('btn-on');
els.btnOff = document.getElementById('btn-off');
els.btnCW = document.getElementById('btn-cw');
els.btnCCW = document.getElementById('btn-ccw');
els.speedSlider = document.getElementById('speed-slider');
els.speedLabel = document.getElementById('speed-label');

els.btnOn.addEventListener('click', () => sendCmd('ON'));
els.btnOff.addEventListener('click', () => sendCmd('OFF'));
els.btnCW.addEventListener('click', () => sendCmd('CW'));
els.btnCCW.addEventListener('click', () => sendCmd('CCW'));

els.speedSlider.addEventListener('input', () => {
  els.speedLabel.textContent = els.speedSlider.value;
});
els.speedSlider.addEventListener('change', () => {
  sendCmd('SPEED=' + els.speedSlider.value);
});

// ── Controls ──
els.btnGo.addEventListener('click', () => {
  const val = parseInt(els.targetInput.value, 10);
  if (!isNaN(val)) sendCmd('T=' + val);
});

els.btnStop.addEventListener('click', () => sendCmd('STOP'));

els.btnHome.addEventListener('click', () => sendCmd('HOME'));

els.btnZero.addEventListener('click', () => sendCmd('ZERO'));

els.btnClear.addEventListener('click', () => sendCmd('CLEAR'));

els.kpSlider.addEventListener('input', () => {
  const val = parseInt(els.kpSlider.value, 10);
  els.kpLabel.textContent = val;
});

els.kpSlider.addEventListener('change', () => {
  const val = parseInt(els.kpSlider.value, 10);
  sendCmd('KP=' + val);
});

els.kiSlider.addEventListener('input', () => {
  const val = parseInt(els.kiSlider.value, 10);
  els.kiLabel.textContent = val;
});

els.kiSlider.addEventListener('change', () => {
  const val = parseInt(els.kiSlider.value, 10);
  sendCmd('KI=' + val);
});

els.btnMaxv.addEventListener('click', () => {
  const val = parseInt(els.maxvInput.value, 10);
  if (!isNaN(val) && val > 0) sendCmd('MAXV=' + val);
});

els.btnTol.addEventListener('click', () => {
  const val = parseInt(els.tolInput.value, 10);
  if (!isNaN(val) && val >= 0) sendCmd('TOL=' + val);
});

els.btnFt.addEventListener('click', () => {
  const val = parseInt(els.ftInput.value, 10);
  if (!isNaN(val) && val > 0) sendCmd('FT=' + val);
});

// ── Profile Controls ──
els.btnProfileS = document.getElementById('btn-profile-s');
els.btnProfileT = document.getElementById('btn-profile-t');
els.accelInput = document.getElementById('accel-input');
els.btnAccel = document.getElementById('btn-accel');
els.jerkInput = document.getElementById('jerk-input');
els.btnJerk = document.getElementById('btn-jerk');

els.btnProfileS.addEventListener('click', () => {
  els.btnProfileS.classList.add('active');
  els.btnProfileT.classList.remove('active');
  sendCmd('PROFILE=S');
});

els.btnProfileT.addEventListener('click', () => {
  els.btnProfileT.classList.add('active');
  els.btnProfileS.classList.remove('active');
  sendCmd('PROFILE=T');
});

els.btnAccel.addEventListener('click', () => {
  const val = parseInt(els.accelInput.value, 10);
  if (!isNaN(val) && val >= 100) sendCmd('ACCEL=' + val);
});

els.btnJerk.addEventListener('click', () => {
  const val = parseInt(els.jerkInput.value, 10);
  if (!isNaN(val) && val >= 1000) sendCmd('JERK=' + val);
});

// ── Port Modal ──
els.btnPorts.addEventListener('click', showModal);
els.btnRefresh.addEventListener('click', fetchPorts);
els.btnConnect.addEventListener('click', connectToPort);
els.modal.addEventListener('click', (e) => { if (e.target === els.modal) hideModal(); });

// Also allow Enter in target input
els.targetInput.addEventListener('keydown', (e) => { if (e.key === 'Enter') els.btnGo.click(); });

setTimeout(() => { if (!connected) showModal(); }, 500);
