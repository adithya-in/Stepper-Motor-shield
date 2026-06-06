const MAX_RPM = 3000;

const els = {
  rpmValue: document.getElementById('rpm-value'),
  rpmBar: document.getElementById('rpm-bar'),
  directionIndicator: document.getElementById('direction-indicator'),
  directionIcon: document.getElementById('direction-icon'),
  directionLabel: document.getElementById('direction-label'),
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
};

const DIRECTION_MAP = {
  FORWARD:  { icon: '\u25B6', label: 'FORWARD' },
  REVERSE:  { icon: '\u25C0', label: 'REVERSE' },
  BACKWARD: { icon: '\u25C0', label: 'REVERSE' },
  CW:       { icon: '\u25B6', label: 'FORWARD' },
  CCW:      { icon: '\u25C0', label: 'REVERSE' },
  STOPPED:  { icon: '\u23F9', label: 'STOPPED' },
  STOP:     { icon: '\u23F9', label: 'STOPPED' },
  IDLE:     { icon: '\u23F9', label: 'STOPPED' },
};

let selectedPort = null;
let connected = false;

function updateRPM(rpm) {
  const value = Math.max(0, Math.min(MAX_RPM, Math.round(rpm)));
  const pct = Math.min(100, (value / MAX_RPM) * 100);
  els.rpmValue.textContent = value;
  els.rpmBar.style.width = pct + '%';
  const hue = 120 - (pct / 100) * 120;
  els.rpmValue.style.color = `hsl(${hue}, 80%, 55%)`;
}

function updateDirection(dir) {
  const upper = (dir || 'STOPPED').toUpperCase();
  const mapping = DIRECTION_MAP[upper] || { icon: '\u2753', label: upper };
  els.directionIcon.textContent = mapping.icon;
  els.directionLabel.textContent = mapping.label;
  els.directionIndicator.className = 'direction-indicator';
  if (upper === 'FORWARD' || upper === 'CW') {
    els.directionIndicator.classList.add('forward');
  } else if (['REVERSE', 'BACKWARD', 'CCW'].includes(upper)) {
    els.directionIndicator.classList.add('reverse');
  } else {
    els.directionIndicator.classList.add('stopped');
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
  const pad = (n) => String(n).padStart(2, '0');
  els.timestamp.textContent =
    `Last update: ${now.getHours()}:${pad(now.getMinutes())}:${pad(now.getSeconds())}`;
}

// --- Port picker ---
async function fetchPorts() {
  els.portList.innerHTML = '<p class="muted">Scanning for ports...</p>';
  els.btnConnect.disabled = true;
  selectedPort = null;
  try {
    const res = await fetch('/api/ports');
    const ports = await res.json();
    if (ports.length === 0) {
      els.portList.innerHTML = '<p class="muted">No serial devices found. Plug in your board and click Refresh.</p>';
      return;
    }
    els.portList.innerHTML = '';
    ports.forEach((p) => {
      const div = document.createElement('div');
      div.className = 'port-item';
      div.innerHTML = `
        <input type="radio" name="port" value="${p.path}">
        <div>
          <div class="port-label">${p.path}</div>
          <div class="port-desc">${p.manufacturer || p.productId || 'Unknown device'}</div>
        </div>
      `;
      div.querySelector('input').addEventListener('change', () => {
        document.querySelectorAll('.port-item').forEach((el) => el.classList.remove('selected'));
        div.classList.add('selected');
        selectedPort = p.path;
        els.btnConnect.disabled = false;
      });
      div.addEventListener('click', () => {
        div.querySelector('input').click();
      });
      els.portList.appendChild(div);
    });
  } catch (err) {
    els.portList.innerHTML = `<p class="muted">Error: ${err.message}</p>`;
  }
}

function showModal() {
  els.modal.classList.add('active');
  els.modalError.textContent = '';
  fetchPorts();
}

function hideModal() {
  els.modal.classList.remove('active');
}

async function connectToPort() {
  if (!selectedPort) return;
  els.btnConnect.disabled = true;
  els.modalError.textContent = '';
  const baud = parseInt(document.getElementById('baud-input').value, 10) || 115200;
  try {
    const res = await fetch('/api/connect', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ path: selectedPort, baud }),
    });
    const data = await res.json();
    if (!res.ok) throw new Error(data.error || 'Connection failed');
    hideModal();
  } catch (err) {
    els.modalError.textContent = err.message;
    els.btnConnect.disabled = false;
  }
}

// --- WebSocket ---
const protocol = location.protocol === 'https:' ? 'wss:' : 'ws:';
const ws = new WebSocket(`${protocol}//${location.host}`);

ws.onmessage = (event) => {
  try {
    const data = JSON.parse(event.data);
    if (data.rpm !== undefined) updateRPM(data.rpm);
    if (data.direction !== undefined) updateDirection(data.direction);
    if (data.connected !== undefined) updateConnection(data);
    updateTimestamp();
  } catch (e) {}
};

ws.onclose = () => {
  updateConnection({ connected: false, port: null });
};

// --- Events ---
els.btnPorts.addEventListener('click', showModal);
els.btnRefresh.addEventListener('click', fetchPorts);
els.btnConnect.addEventListener('click', connectToPort);

// Close modal on overlay click
els.modal.addEventListener('click', (e) => {
  if (e.target === els.modal) hideModal();
});

// Show modal on load if not connected
setTimeout(() => {
  if (!connected) showModal();
}, 500);
