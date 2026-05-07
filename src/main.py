from flask import Flask, request, jsonify, render_template_string
from datetime import datetime

app = Flask(__name__)

# Shared in-memory state
state = {
    "sensor_value": None,
    "sensor_name": "sensor",
    "pattern": "blink",
    "last_esp_seen": None,
    "esp_ip": None
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

        .layout {
            max-width: 960px;
            margin: 0 auto;
        }

        header {
            display: flex;
            align-items: center;
            justify-content: space-between;
            margin-bottom: 2rem;
            padding-bottom: 1.25rem;
            border-bottom: 1px solid var(--border);
        }

        .header-left {
            display: flex;
            align-items: center;
            gap: 12px;
        }

        .logo-dot {
            width: 10px;
            height: 10px;
            border-radius: 50%;
            background: var(--accent);
            box-shadow: 0 0 8px var(--accent);
            animation: pulse 2s ease-in-out infinite;
        }

        @keyframes pulse {
            0%, 100% { opacity: 1; }
            50% { opacity: 0.4; }
        }

        h1 {
            font-size: 1rem;
            font-weight: 500;
            letter-spacing: 0.05em;
            text-transform: uppercase;
            color: var(--text-muted);
        }

        .connection-badge {
            font-family: var(--mono);
            font-size: 0.72rem;
            padding: 4px 10px;
            border-radius: 4px;
            border: 1px solid var(--border-strong);
            color: var(--text-muted);
            background: var(--surface);
            letter-spacing: 0.04em;
        }

        .grid {
            display: grid;
            grid-template-columns: repeat(3, 1fr);
            gap: 1px;
            background: var(--border);
            border: 1px solid var(--border);
            border-radius: 12px;
            overflow: hidden;
            margin-bottom: 1px;
        }

        .card {
            background: var(--surface);
            padding: 1.5rem;
            position: relative;
        }

        .card-label {
            font-family: var(--mono);
            font-size: 0.68rem;
            letter-spacing: 0.1em;
            text-transform: uppercase;
            color: var(--text-dim);
            margin-bottom: 0.75rem;
        }

        .card-value {
            font-family: var(--mono);
            font-size: 2.5rem;
            font-weight: 500;
            line-height: 1;
            color: var(--text);
            margin-bottom: 0.4rem;
        }

        .card-sub {
            font-family: var(--mono);
            font-size: 0.72rem;
            color: var(--text-muted);
        }

        .status-online { color: var(--accent); }
        .status-offline { color: var(--red); }

        .status-indicator {
            display: inline-flex;
            align-items: center;
            gap: 8px;
        }

        .dot {
            width: 6px;
            height: 6px;
            border-radius: 50%;
            flex-shrink: 0;
        }

        .dot-online {
            background: var(--accent);
            animation: pulse 2s ease-in-out infinite;
        }

        .dot-offline { background: var(--red); }
        .dot-unknown { background: var(--text-dim); }

        .pattern-section {
            background: var(--surface);
            border-top: 1px solid var(--border);
            padding: 1.5rem;
        }

        .pattern-header {
            display: flex;
            align-items: center;
            justify-content: space-between;
            margin-bottom: 1.25rem;
        }

        .current-pattern {
            font-family: var(--mono);
            font-size: 0.8rem;
            color: var(--accent);
            padding: 4px 10px;
            background: var(--accent-dim);
            border: 1px solid var(--accent-border);
            border-radius: 4px;
            letter-spacing: 0.05em;
        }

        .pattern-buttons {
            display: flex;
            gap: 8px;
            flex-wrap: wrap;
        }

        .btn {
            font-family: var(--mono);
            font-size: 0.75rem;
            letter-spacing: 0.06em;
            padding: 8px 16px;
            border: 1px solid var(--border-strong);
            border-radius: 6px;
            background: var(--surface-2);
            color: var(--text-muted);
            cursor: pointer;
            transition: all 0.15s ease;
            text-transform: uppercase;
        }

        .btn:hover {
            border-color: var(--accent-border);
            color: var(--accent);
            background: var(--accent-dim);
        }

        .btn.active {
            border-color: var(--accent-border);
            color: var(--accent);
            background: var(--accent-dim);
        }

        .btn-off:hover, .btn-off.active {
            border-color: rgba(248, 113, 113, 0.3);
            color: var(--red);
            background: var(--red-dim);
        }

        .btn:active { transform: scale(0.97); }

        .history-section {
            background: var(--surface);
            border-top: 1px solid var(--border);
            padding: 1.5rem;
        }

        .history-label {
            font-family: var(--mono);
            font-size: 0.68rem;
            letter-spacing: 0.1em;
            text-transform: uppercase;
            color: var(--text-dim);
            margin-bottom: 1rem;
        }

        .history-track {
            display: flex;
            gap: 3px;
            align-items: flex-end;
            height: 48px;
        }

        .history-bar {
            flex: 1;
            background: var(--surface-2);
            border-radius: 2px;
            transition: height 0.4s ease, background 0.3s ease;
            min-height: 4px;
        }

        .history-bar.active {
            background: var(--accent);
            opacity: 0.7;
        }

        .footer-strip {
            display: flex;
            align-items: center;
            justify-content: space-between;
            margin-top: 1.25rem;
            padding-top: 1rem;
            border-top: 1px solid var(--border);
        }

        .footer-stat {
            font-family: var(--mono);
            font-size: 0.7rem;
            color: var(--text-dim);
        }

        .footer-stat span {
            color: var(--text-muted);
        }

        .pill {
            display: inline-flex;
            align-items: center;
            gap: 5px;
            font-family: var(--mono);
            font-size: 0.68rem;
            color: var(--text-dim);
            letter-spacing: 0.06em;
        }

        .toast {
            position: fixed;
            bottom: 1.5rem;
            right: 1.5rem;
            background: var(--surface-2);
            border: 1px solid var(--border-strong);
            border-radius: 8px;
            padding: 10px 16px;
            font-family: var(--mono);
            font-size: 0.75rem;
            color: var(--text);
            opacity: 0;
            transform: translateY(8px);
            transition: all 0.2s ease;
            pointer-events: none;
            z-index: 100;
        }

        .toast.show {
            opacity: 1;
            transform: translateY(0);
        }

        .refresh-tick {
            display: inline-block;
            width: 8px;
            height: 8px;
            border: 1px solid var(--text-dim);
            border-top-color: var(--accent);
            border-radius: 50%;
        }

        .spinning { animation: spin 1s linear infinite; }
        @keyframes spin { to { transform: rotate(360deg); } }

        @media (max-width: 680px) {
            .grid { grid-template-columns: 1fr; }
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
            <div class="card-label">// sensor reading</div>
            <div class="card-value" id="sensorValue">--</div>
            <div class="card-sub" id="sensorName">no data</div>
        </div>

        <div class="card">
            <div class="card-label">// led pattern</div>
            <div class="card-value" style="font-size:1.6rem; padding-top:0.4rem;" id="patternDisplay">blink</div>
            <div class="card-sub">active pattern</div>
        </div>

        <div class="card">
            <div class="card-label">// esp32 status</div>
            <div class="card-value" style="font-size:1.4rem; padding-top:0.5rem;">
                <div class="status-indicator">
                    <div class="dot dot-unknown" id="statusDot"></div>
                    <span id="statusText">—</span>
                </div>
            </div>
            <div class="card-sub" id="lastSeenText">last seen: —</div>
        </div>
    </div>

    <div class="pattern-section">
        <div class="pattern-header">
            <div class="card-label" style="margin:0">// set pattern</div>
            <div class="current-pattern" id="currentPatternBadge">blink</div>
        </div>
        <div class="pattern-buttons">
            <button class="btn active" onclick="setPattern('blink')" data-pattern="blink">blink</button>
            <button class="btn" onclick="setPattern('chase')" data-pattern="chase">chase</button>
            <button class="btn" onclick="setPattern('flicker')" data-pattern="flicker">flicker</button>
            <button class="btn" onclick="setPattern('alternate')" data-pattern="alternate">alternate</button>
            <button class="btn btn-off" onclick="setPattern('off')" data-pattern="off">off</button>
        </div>
    </div>

    <div class="history-section">
        <div class="history-label">// sensor history (last 30 readings)</div>
        <div class="history-track" id="historyTrack"></div>
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
    const MAX_HISTORY = 30;
    let sensorHistory = [];
    let currentPattern = 'blink';

    function initHistory() {
        const track = document.getElementById('historyTrack');
        track.innerHTML = '';
        for (let i = 0; i < MAX_HISTORY; i++) {
            const bar = document.createElement('div');
            bar.className = 'history-bar';
            bar.style.height = '4px';
            track.appendChild(bar);
        }
    }

    function updateHistory(value) {
        if (value === null || value === undefined) return;
        sensorHistory.push(Number(value));
        if (sensorHistory.length > MAX_HISTORY) sensorHistory.shift();

        const bars = document.querySelectorAll('.history-bar');
        const max = Math.max(...sensorHistory, 1);
        const latest = sensorHistory.length - 1;

        bars.forEach((bar, i) => {
            const dataIdx = i - (MAX_HISTORY - sensorHistory.length);
            if (dataIdx < 0) {
                bar.style.height = '4px';
                bar.classList.remove('active');
            } else {
                const pct = (sensorHistory[dataIdx] / max) * 44 + 4;
                bar.style.height = pct + 'px';
                bar.classList.toggle('active', dataIdx === latest);
            }
        });
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

            const val = data.sensor_value ?? '--';
            document.getElementById('sensorValue').textContent = val;
            document.getElementById('sensorName').textContent = data.sensor_name ?? 'sensor';
            updateHistory(data.sensor_value);

            const pat = data.pattern ?? 'blink';
            document.getElementById('patternDisplay').textContent = pat;
            document.getElementById('currentPatternBadge').textContent = pat;
            currentPattern = pat;
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
                document.getElementById('lastSeenText').textContent =
                    `last seen: ${ago}s ago`;
            }

            document.getElementById('espIpBadge').textContent =
                'ESP: ' + (data.esp_ip ?? '--');

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
                showToast(`pattern → ${pattern}`);
                fetchState();
            }
        } catch (err) {
            console.error('setPattern error:', err);
        }
    }

    initHistory();
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
        "sensor_value": state["sensor_value"],
        "sensor_name": state["sensor_name"],
        "pattern": state["pattern"],
        "last_esp_seen": last_seen.isoformat() if last_seen else None,
        "esp_ip": state["esp_ip"],
        "is_online": is_online
    })


@app.route("/api/set_pattern", methods=["POST"])
def set_pattern():
    data = request.get_json(silent=True) or {}
    pattern = data.get("pattern")

    allowed_patterns = {"blink", "chase", "flicker", "alternate", "off"}
    if pattern not in allowed_patterns:
        return jsonify({"ok": False, "error": "Invalid pattern"}), 400

    state["pattern"] = pattern
    return jsonify({"ok": True, "pattern": pattern})


@app.route("/api/update_sensor", methods=["POST"])
def update_sensor():
    data = request.get_json(silent=True) or {}

    state["sensor_value"] = data.get("value")
    state["sensor_name"] = data.get("name", "sensor")
    state["last_esp_seen"] = datetime.utcnow()
    state["esp_ip"] = request.remote_addr

    return jsonify({"ok": True})


@app.route("/api/get_command", methods=["GET"])
def get_command():
    state["last_esp_seen"] = datetime.utcnow()
    state["esp_ip"] = request.remote_addr
    return jsonify({
        "pattern": state["pattern"]
    })


if __name__ == "__main__":
    app.run(host="0.0.0.0", port=5000, debug=True)