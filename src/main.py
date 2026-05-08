from flask import Flask, request, jsonify, render_template_string
from datetime import datetime
from collections import deque

app = Flask(__name__)

# Allowed patterns must mirror parseMode() in the ESP32 firmware exactly.
ALLOWED_PATTERNS = {
    "off",
    "blink",
    "chase",
    "flicker",
    "alternate",
    "snake",
    "diagonal",
    "fill",
    "scroll_text",
    "alert_bars",   # NEW
    "rainbow",      # NEW
}

# Rolling sensor history. Stored server-side so the chart survives page
# refresh and so multiple browsers see the same data.
MAX_HISTORY = 60
sensor_history: "deque[dict]" = deque(maxlen=MAX_HISTORY)

state = {
    "sensor_value": None,
    "sensor_name": "sensor",
    "pattern": "blink",
    "scroll_text": "HELLO ESP32 ",
    "last_esp_seen": None,
    "esp_ip": None,
    # Alert state. user_pattern is what the user last manually chose --
    # we restore it when the temperature drops back below threshold.
    "alert_threshold": 85.0,
    "alert_active": False,
    "user_pattern": "blink",
}

HTML_PAGE = """
<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>ESP32 IoT Control Panel</title>
    <link rel="preconnect" href="https://fonts.googleapis.com">
    <link href="https://fonts.googleapis.com/css2?family=IBM+Plex+Mono:wght@400;500&family=Space+Grotesk:wght@400;500;600&display=swap" rel="stylesheet">
    <script src="https://cdn.jsdelivr.net/npm/chart.js@4.4.1/dist/chart.umd.min.js"></script>
    <style>
        *, *::before, *::after { box-sizing: border-box; margin: 0; padding: 0; }

        :root {
            --bg: #0d0f14;
            --surface: #141720;
            --surface-2: #1c2030;
            --border: rgba(255,255,255,0.07);
            --border-strong: rgba(255,255,255,0.14);
            --text: #e8eaf0;
            --text-muted: #6b7280;
            --text-dim: #3d4352;
            --accent: #4ade80;
            --accent-dim: rgba(74, 222, 128, 0.12);
            --accent-border: rgba(74, 222, 128, 0.3);
            --amber: #fbbf24;
            --amber-dim: rgba(251, 191, 36, 0.1);
            --red: #f87171;
            --red-dim: rgba(248, 113, 113, 0.1);
            --blue: #60a5fa;
            --blue-dim: rgba(96, 165, 250, 0.1);
            --mono: 'IBM Plex Mono', monospace;
            --sans: 'Space Grotesk', sans-serif;
        }

        body {
            font-family: var(--sans);
            background: var(--bg);
            color: var(--text);
            min-height: 100vh;
            padding: 2rem 1.5rem;
        }

        .layout { max-width: 960px; margin: 0 auto; }

        header {
            display: flex; align-items: center; justify-content: space-between;
            margin-bottom: 2rem; padding-bottom: 1.25rem;
            border-bottom: 1px solid var(--border);
        }
        .header-left { display: flex; align-items: center; gap: 12px; }

        .logo-dot {
            width: 10px; height: 10px; border-radius: 50%;
            background: var(--accent); box-shadow: 0 0 8px var(--accent);
            animation: pulse 2s ease-in-out infinite;
        }
        @keyframes pulse { 0%, 100% { opacity: 1; } 50% { opacity: 0.4; } }

        h1 {
            font-size: 1rem; font-weight: 500; letter-spacing: 0.05em;
            text-transform: uppercase; color: var(--text-muted);
        }

        .connection-badge {
            font-family: var(--mono); font-size: 0.72rem;
            padding: 4px 10px; border-radius: 4px;
            border: 1px solid var(--border-strong);
            color: var(--text-muted); background: var(--surface);
            letter-spacing: 0.04em;
        }

        .grid {
            display: grid; grid-template-columns: repeat(3, 1fr); gap: 1px;
            background: var(--border); border: 1px solid var(--border);
            border-radius: 12px; overflow: hidden; margin-bottom: 1px;
        }

        .card { background: var(--surface); padding: 1.5rem; position: relative; }

        .card-label {
            font-family: var(--mono); font-size: 0.68rem;
            letter-spacing: 0.1em; text-transform: uppercase;
            color: var(--text-dim); margin-bottom: 0.75rem;
        }

        .card-value {
            font-family: var(--mono); font-size: 2.5rem; font-weight: 500;
            line-height: 1; color: var(--text); margin-bottom: 0.4rem;
        }

        .card-sub { font-family: var(--mono); font-size: 0.72rem; color: var(--text-muted); }

        .status-online { color: var(--accent); }
        .status-offline { color: var(--red); }

        .status-indicator { display: inline-flex; align-items: center; gap: 8px; }
        .dot { width: 6px; height: 6px; border-radius: 50%; flex-shrink: 0; }
        .dot-online { background: var(--accent); animation: pulse 2s ease-in-out infinite; }
        .dot-offline { background: var(--red); }
        .dot-unknown { background: var(--text-dim); }

        .section {
            background: var(--surface);
            border-top: 1px solid var(--border);
            padding: 1.5rem;
        }

        .section-header {
            display: flex; align-items: center; justify-content: space-between;
            margin-bottom: 1.25rem;
        }

        .current-pattern {
            font-family: var(--mono); font-size: 0.8rem;
            color: var(--accent); padding: 4px 10px;
            background: var(--accent-dim);
            border: 1px solid var(--accent-border);
            border-radius: 4px; letter-spacing: 0.05em;
        }

        .pattern-buttons { display: flex; gap: 8px; flex-wrap: wrap; }

        .btn {
            font-family: var(--mono); font-size: 0.75rem; letter-spacing: 0.06em;
            padding: 8px 16px; border: 1px solid var(--border-strong);
            border-radius: 6px; background: var(--surface-2);
            color: var(--text-muted); cursor: pointer;
            transition: all 0.15s ease; text-transform: uppercase;
        }

        .btn:hover {
            border-color: var(--accent-border);
            color: var(--accent); background: var(--accent-dim);
        }
        .btn.active {
            border-color: var(--accent-border);
            color: var(--accent); background: var(--accent-dim);
        }
        .btn-off:hover, .btn-off.active {
            border-color: rgba(248, 113, 113, 0.3);
            color: var(--red); background: var(--red-dim);
        }
        .btn:active { transform: scale(0.97); }

        .text-row {
            display: flex; gap: 8px; align-items: stretch;
        }

        .text-input {
            flex: 1;
            font-family: var(--mono); font-size: 0.85rem;
            padding: 10px 14px;
            background: var(--surface-2);
            border: 1px solid var(--border-strong);
            border-radius: 6px;
            color: var(--text);
            letter-spacing: 0.05em;
            outline: none;
            transition: border-color 0.15s ease;
            text-transform: uppercase;
        }
        .text-input:focus { border-color: var(--accent-border); }

        .btn-send {
            font-family: var(--mono); font-size: 0.75rem; letter-spacing: 0.06em;
            padding: 0 18px; border: 1px solid var(--accent-border);
            border-radius: 6px; background: var(--accent-dim);
            color: var(--accent); cursor: pointer;
            transition: all 0.15s ease; text-transform: uppercase;
        }
        .btn-send:hover { background: rgba(74, 222, 128, 0.2); }
        .btn-send:active { transform: scale(0.97); }

        .text-hint {
            font-family: var(--mono); font-size: 0.68rem;
            color: var(--text-dim); margin-top: 0.6rem;
            letter-spacing: 0.05em;
        }

        .chart-wrap {
            position: relative;
            height: 220px;
            width: 100%;
        }

        .threshold-wrap {
            display: flex; align-items: center; gap: 6px;
            font-family: var(--mono); font-size: 0.72rem;
            color: var(--text-muted);
        }
        .threshold-wrap input {
            width: 56px;
            font-family: var(--mono); font-size: 0.78rem;
            padding: 3px 6px;
            background: var(--surface-2);
            border: 1px solid var(--border-strong);
            border-radius: 4px;
            color: var(--text);
            outline: none;
            text-align: center;
        }
        .threshold-wrap input:focus { border-color: var(--accent-border); }

        .alert-flash {
            border-color: var(--amber) !important;
            box-shadow: 0 0 0 1px var(--amber-dim);
        }

        .history-meta {
            display: flex; justify-content: space-between;
            font-family: var(--mono); font-size: 0.68rem;
            color: var(--text-dim); margin-top: 0.6rem;
        }

        .footer-strip {
            display: flex; align-items: center; justify-content: space-between;
            margin-top: 1.25rem; padding-top: 1rem;
            border-top: 1px solid var(--border);
        }
        .footer-stat {
            font-family: var(--mono); font-size: 0.7rem; color: var(--text-dim);
        }
        .footer-stat span { color: var(--text-muted); }

        .pill {
            display: inline-flex; align-items: center; gap: 5px;
            font-family: var(--mono); font-size: 0.68rem;
            color: var(--text-dim); letter-spacing: 0.06em;
        }

        .toast {
            position: fixed; bottom: 1.5rem; right: 1.5rem;
            background: var(--surface-2);
            border: 1px solid var(--border-strong);
            border-radius: 8px; padding: 10px 16px;
            font-family: var(--mono); font-size: 0.75rem; color: var(--text);
            opacity: 0; transform: translateY(8px);
            transition: all 0.2s ease; pointer-events: none; z-index: 100;
        }
        .toast.show { opacity: 1; transform: translateY(0); }

        .refresh-tick {
            display: inline-block; width: 8px; height: 8px;
            border: 1px solid var(--text-dim);
            border-top-color: var(--accent); border-radius: 50%;
        }
        .spinning { animation: spin 1s linear infinite; }
        @keyframes spin { to { transform: rotate(360deg); } }

        @media (max-width: 680px) {
            .grid { grid-template-columns: 1fr; }
            .text-row { flex-direction: column; }
            .btn-send { padding: 10px; }
        }
    </style>
</head>
<body>
<div class="layout">

    <header>
        <div class="header-left">
            <div class="logo-dot" id="headerDot"></div>
            <h1>IoT Control Panel</h1>
        </div>
        <div class="connection-badge" id="espIpBadge">ESP: --</div>
    </header>

    <div class="grid">
        <div class="card">
            <div class="card-label">// thermistor</div>
            <div class="card-value" id="sensorValue">--</div>
            <div class="card-sub" id="sensorName">no data</div>
        </div>

        <div class="card">
            <div class="card-label">// led pattern</div>
            <div class="card-value" style="font-size:1.4rem; padding-top:0.6rem;" id="patternDisplay">blink</div>
            <div class="card-sub">active pattern</div>
        </div>

        <div class="card">
            <div class="card-label">// esp32 status</div>
            <div class="card-value" style="font-size:1.4rem; padding-top:0.6rem;">
                <div class="status-indicator">
                    <div class="dot dot-unknown" id="statusDot"></div>
                    <span id="statusText">—</span>
                </div>
            </div>
            <div class="card-sub" id="lastSeenText">last seen: —</div>
        </div>
    </div>

    <div class="section">
        <div class="section-header">
            <div class="card-label" style="margin:0">// 10x4 matrix patterns</div>
            <div class="current-pattern" id="currentPatternBadge">blink</div>
        </div>
        <div class="pattern-buttons">
            <button class="btn active" onclick="setPattern('blink')" data-pattern="blink">blink</button>
            <button class="btn" onclick="setPattern('chase')" data-pattern="chase">chase</button>
            <button class="btn" onclick="setPattern('flicker')" data-pattern="flicker">flicker</button>
            <button class="btn" onclick="setPattern('alternate')" data-pattern="alternate">alternate</button>
            <button class="btn" onclick="setPattern('snake')" data-pattern="snake">snake</button>
            <button class="btn" onclick="setPattern('diagonal')" data-pattern="diagonal">diagonal</button>
            <button class="btn" onclick="setPattern('fill')" data-pattern="fill">fill</button>
            <button class="btn" onclick="setPattern('alert_bars')" data-pattern="alert_bars">alert</button>
            <button class="btn" onclick="setPattern('rainbow')" data-pattern="rainbow">rainbow</button>
            <button class="btn btn-off" onclick="setPattern('off')" data-pattern="off">off</button>
        </div>
    </div>

    <div class="section">
        <div class="section-header">
            <div class="card-label" style="margin:0">// 6x4 scrolling text</div>
            <div class="current-pattern" id="scrollBadge" style="background: var(--blue-dim); color: var(--blue); border-color: rgba(96,165,250,0.3);">scroll_text</div>
        </div>
        <div class="text-row">
            <input type="text" id="scrollInput" class="text-input" maxlength="60" placeholder="enter text to scroll..." value="HELLO ESP32 ">
            <button class="btn-send" onclick="sendScrollText()">send &amp; play</button>
        </div>
        <div class="text-hint">// A–Z and 0–9 supported · uppercase only · max 60 chars · trailing space recommended</div>
    </div>

    <div class="section">
        <div class="section-header">
            <div class="card-label" style="margin:0">// thermistor live graph (last 60 readings)</div>
            <div class="threshold-wrap">
                <label for="thresholdInput">alert &gt;</label>
                <input type="number" id="thresholdInput" value="85" step="1">
                <span>°F</span>
            </div>
        </div>
        <div class="chart-wrap">
            <canvas id="sensorChart"></canvas>
        </div>
        <div class="history-meta">
            <span id="historyMin">min: --</span>
            <span id="historyMax">max: --</span>
            <span id="historyAvg">avg: --</span>
        </div>
    </div>

    <div class="footer-strip">
        <div class="footer-stat">refresh <span>2s</span> &nbsp;·&nbsp; timeout <span>10s</span></div>
        <div class="pill">
            <div class="refresh-tick" id="refreshTick"></div>
            polling
        </div>
    </div>

</div>

<div class="toast" id="toast"></div>

<script>
    let sensorChart = null;

    function initChart() {
        const ctx = document.getElementById('sensorChart').getContext('2d');
        sensorChart = new Chart(ctx, {
            type: 'line',
            data: {
                labels: [],
                datasets: [{
                    label: 'sensor',
                    data: [],
                    borderColor: '#4ade80',
                    backgroundColor: 'rgba(74,222,128,0.08)',
                    borderWidth: 2,
                    tension: 0.35,
                    pointRadius: 1.5,
                    pointHoverRadius: 4,
                    fill: true,
                }]
            },
            options: {
                animation: false,
                responsive: true,
                maintainAspectRatio: false,
                interaction: { intersect: false, mode: 'index' },
                scales: {
                    x: {
                        ticks: {
                            color: '#6b7280',
                            font: { family: 'IBM Plex Mono', size: 10 },
                            maxTicksLimit: 6,
                            maxRotation: 0,
                        },
                        grid: { color: 'rgba(255,255,255,0.04)' },
                    },
                    y: {
                        ticks: {
                            color: '#6b7280',
                            font: { family: 'IBM Plex Mono', size: 10 },
                        },
                        grid: { color: 'rgba(255,255,255,0.04)' },
                    },
                },
                plugins: {
                    legend: { display: false },
                    tooltip: {
                        backgroundColor: '#1c2030',
                        titleFont: { family: 'IBM Plex Mono', size: 11 },
                        bodyFont:  { family: 'IBM Plex Mono', size: 11 },
                        borderColor: 'rgba(255,255,255,0.14)',
                        borderWidth: 1,
                    },
                }
            }
        });
    }

    function fmt(v) {
        if (v === null || v === undefined || isNaN(v)) return '--';
        const n = Number(v);
        return Number.isInteger(n) ? n.toString() : n.toFixed(1);
    }

    function updateChart(history) {
        if (!sensorChart || !Array.isArray(history) || history.length === 0) return;

        const labels = history.map(h => {
            const d = new Date(h.t + 'Z');
            return d.toLocaleTimeString([], { hour12: false });
        });
        const values = history.map(h => h.v);

        sensorChart.data.labels = labels;
        sensorChart.data.datasets[0].data = values;
        sensorChart.update('none');

        const minV = Math.min(...values);
        const maxV = Math.max(...values);
        const avg  = values.reduce((a, b) => a + b, 0) / values.length;
        document.getElementById('historyMin').textContent = 'min: ' + fmt(minV);
        document.getElementById('historyMax').textContent = 'max: ' + fmt(maxV);
        document.getElementById('historyAvg').textContent = 'avg: ' + fmt(avg);
    }

    function checkThreshold(value) {
        const card = document.querySelector('.card');
        if (!card) return;
        const thrEl = document.getElementById('thresholdInput');
        const thr = parseFloat(thrEl.value);
        if (!isNaN(thr) && value !== null && value !== undefined && Number(value) > thr) {
            card.classList.add('alert-flash');
        } else {
            card.classList.remove('alert-flash');
        }
    }

    function showToast(msg) {
        const t = document.getElementById('toast');
        t.textContent = msg;
        t.classList.add('show');
        setTimeout(() => t.classList.remove('show'), 2000);
    }

    function tickRefresh() {
        const tick = document.getElementById('refreshTick');
        tick.classList.add('spinning');
        setTimeout(() => tick.classList.remove('spinning'), 600);
    }

    async function fetchState() {
        tickRefresh();
        try {
            const res = await fetch('/api/state');
            const data = await res.json();

            document.getElementById('sensorValue').textContent = fmt(data.sensor_value);
            document.getElementById('sensorName').textContent = data.sensor_name ?? 'sensor';

            const thresholdInput = document.getElementById('thresholdInput');
            if (data.alert_threshold !== undefined && document.activeElement !== thresholdInput) {
                thresholdInput.value = data.alert_threshold;
            }

            updateChart(data.history);
            checkThreshold(data.sensor_value);

            const pat = data.pattern ?? 'blink';
            document.getElementById('patternDisplay').textContent = pat;
            document.getElementById('currentPatternBadge').textContent = pat;
            document.querySelectorAll('.btn[data-pattern]').forEach(btn => {
                btn.classList.toggle('active', btn.dataset.pattern === pat);
            });

            const online = data.is_online;
            const dot = document.getElementById('statusDot');
            const statusText = document.getElementById('statusText');
            const headerDot = document.getElementById('headerDot');

            dot.className = 'dot ' + (online ? 'dot-online' : 'dot-offline');
            statusText.textContent = online ? 'online' : 'offline';
            statusText.className = online ? 'status-online' : 'status-offline';
            headerDot.style.background = online ? 'var(--accent)' : 'var(--red)';
            headerDot.style.boxShadow = online ? '0 0 8px var(--accent)' : '0 0 8px var(--red)';

            if (data.last_esp_seen) {
                const d = new Date(data.last_esp_seen + 'Z');
                const ago = Math.round((Date.now() - d.getTime()) / 1000);
                document.getElementById('lastSeenText').textContent = `last seen: ${ago}s ago`;
            }

            document.getElementById('espIpBadge').textContent = 'ESP: ' + (data.esp_ip ?? '--');
        } catch (err) {
            console.error('fetchState error:', err);
        }
    }

    async function setPattern(pattern) {
        document.querySelectorAll('.btn[data-pattern]').forEach(btn => {
            btn.classList.toggle('active', btn.dataset.pattern === pattern);
        });
        try {
            const res = await fetch('/api/set_pattern', {
                method: 'POST',
                headers: { 'Content-Type': 'application/json' },
                body: JSON.stringify({ pattern })
            });
            const data = await res.json();
            if (data.ok) {
                if (data.queued) {
                    showToast('queued → ' + data.queued);
                } else {
                    showToast('pattern → ' + pattern);
                }
                fetchState();
            } else {
                showToast('error: ' + (data.error || 'unknown'));
            }
        } catch (err) {
            console.error('setPattern error:', err);
        }
    }

    async function sendScrollText() {
        const input = document.getElementById('scrollInput');
        const text = input.value.trim();
        if (!text) {
            showToast('text is empty');
            return;
        }
        try {
            const res = await fetch('/api/set_scroll_text', {
                method: 'POST',
                headers: { 'Content-Type': 'application/json' },
                body: JSON.stringify({ text })
            });
            const data = await res.json();
            if (data.ok) {
                showToast('scrolling: ' + text);
                document.querySelectorAll('.btn[data-pattern]').forEach(btn => {
                    btn.classList.toggle('active', btn.dataset.pattern === 'scroll_text');
                });
                fetchState();
            } else {
                showToast('error: ' + (data.error || 'unknown'));
            }
        } catch (err) {
            console.error('sendScrollText error:', err);
        }
    }

    document.getElementById('scrollInput').addEventListener('keydown', (e) => {
        if (e.key === 'Enter') sendScrollText();
    });

    document.getElementById('thresholdInput').addEventListener('change', async (e) => {
        const thr = parseFloat(e.target.value);
        if (isNaN(thr)) return;
        try {
            await fetch('/api/set_threshold', {
                method: 'POST',
                headers: { 'Content-Type': 'application/json' },
                body: JSON.stringify({ threshold: thr })
            });
            showToast('threshold → ' + thr + '°F');
            fetchState();
        } catch (err) {
            console.error('set_threshold error:', err);
        }
    });

    initChart();
    fetchState();
    setInterval(fetchState, 2000);
</script>
</body>
</html>
"""


@app.route("/")
def index():
    return render_template_string(HTML_PAGE)


@app.route("/api/state", methods=["GET"])
def get_state():
    last_seen = state["last_esp_seen"]
    is_online = False
    if last_seen is not None:
        delta = datetime.utcnow() - last_seen
        is_online = delta.total_seconds() < 10

    return jsonify({
        "sensor_value":     state["sensor_value"],
        "sensor_name":      state["sensor_name"],
        "pattern":          state["pattern"],
        "scroll_text":      state["scroll_text"],
        "last_esp_seen":    last_seen.isoformat() if last_seen else None,
        "esp_ip":           state["esp_ip"],
        "is_online":        is_online,
        "history":          list(sensor_history),
        "alert_threshold":  state["alert_threshold"],
        "alert_active":     state["alert_active"],
    })


@app.route("/api/set_pattern", methods=["POST"])
def set_pattern():
    data = request.get_json(silent=True) or {}
    pattern = data.get("pattern")

    if pattern not in ALLOWED_PATTERNS:
        return jsonify({
            "ok": False,
            "error": f"Invalid pattern. Allowed: {sorted(ALLOWED_PATTERNS)}",
        }), 400

    # Remember user's choice so we can restore after an alert clears.
    # Don't record alert_bars itself as the "user pattern" -- otherwise
    # an alert that auto-sets it would stick around forever.
    if pattern != "alert_bars":
        state["user_pattern"] = pattern

    # If an alert is currently active, the user's choice is queued but
    # alert_bars stays on the wire until temp drops.
    if state["alert_active"] and pattern != "alert_bars":
        return jsonify({
            "ok": True,
            "pattern": "alert_bars",
            "queued": pattern,
            "note": "alert active; your choice will apply when temp drops below threshold",
        })

    state["pattern"] = pattern
    return jsonify({"ok": True, "pattern": pattern})


@app.route("/api/set_scroll_text", methods=["POST"])
def set_scroll_text():
    data = request.get_json(silent=True) or {}
    text = data.get("text", "")

    if not isinstance(text, str) or not text.strip():
        return jsonify({"ok": False, "error": "text must be a non-empty string"}), 400
    if len(text) > 60:
        return jsonify({"ok": False, "error": "text too long (max 60 chars)"}), 400

    # Trim to ASCII printable; firmware font only handles A-Z, 0-9, space
    cleaned = text.upper()
    if not cleaned.endswith(" "):
        cleaned += " "

    state["scroll_text"] = cleaned
    state["pattern"] = "scroll_text"
    return jsonify({"ok": True, "text": cleaned, "pattern": "scroll_text"})


@app.route("/api/set_threshold", methods=["POST"])
def set_threshold():
    data = request.get_json(silent=True) or {}
    try:
        thr = float(data.get("threshold"))
    except (TypeError, ValueError):
        return jsonify({"ok": False, "error": "threshold must be a number"}), 400

    state["alert_threshold"] = thr

    # Re-evaluate immediately against the latest reading so toggling the
    # threshold doesn't have to wait for the next sensor POST.
    _reevaluate_alert(state["sensor_value"])

    return jsonify({
        "ok": True,
        "threshold": thr,
        "alert_active": state["alert_active"],
    })


def _reevaluate_alert(value):
    """Compare value to threshold and toggle alert_bars / restore user pattern."""
    if not isinstance(value, (int, float)):
        return

    over = value > state["alert_threshold"]

    if over and not state["alert_active"]:
        state["alert_active"] = True
        state["pattern"] = "alert_bars"
    elif not over and state["alert_active"]:
        state["alert_active"] = False

        # Restore whatever the user last chose (defaults to blink).
        state["pattern"] = state.get("user_pattern", "blink")


@app.route("/api/update_sensor", methods=["POST"])
def update_sensor():
    data = request.get_json(silent=True) or {}
    value = data.get("value")

    state["sensor_value"]  = value
    state["sensor_name"]   = data.get("name", "sensor")
    state["last_esp_seen"] = datetime.utcnow()
    state["esp_ip"]        = request.remote_addr

    if isinstance(value, (int, float)):
        sensor_history.append({
            "t": state["last_esp_seen"].isoformat(),
            "v": float(value),
        })
        _reevaluate_alert(value)

    return jsonify({"ok": True})


@app.route("/api/get_command", methods=["GET"])
def get_command():
    state["last_esp_seen"] = datetime.utcnow()
    state["esp_ip"]        = request.remote_addr
    return jsonify({
        "pattern": state["pattern"],
        "text":    state["scroll_text"],
    })


if __name__ == "__main__":
    app.run(host="0.0.0.0", port=5000, debug=True)