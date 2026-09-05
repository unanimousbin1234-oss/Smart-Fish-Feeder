#include <WiFi.h>
#include <WebServer.h>
#include <AccelStepper.h>

// --- Wi-Fi Credentials ---
const char* ssid     = "YOUR_WIFI_SSID";
const char* password = "YOUR_WIFI_PASSWORD";

// --- Stepper Configuration ---
const int STEP_PIN = 18;
const int DIR_PIN  = 19;
AccelStepper stepper(AccelStepper::DRIVER, STEP_PIN, DIR_PIN);

const float GRAMS_PER_TURN = 6.0;
const int STEPS_PER_REV = 200;

WebServer server(80);

// --- Scheduling State ---
bool autoFeedEnabled = false;
unsigned long intervalMillis = 0;
unsigned long lastFeedTime = 0;
float scheduledGramsPerFeed = 0.0;
float scheduledIntervalHours = 0.0;

// --- Web UI (HTML, CSS, JS) ---
const char MAIN_PAGE[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>Smart Tank Multi-Species Feeder</title>
  <style>
    body { font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, sans-serif; background: #0f172a; color: #f8fafc; padding: 16px; margin: 0; }
    .card { background: #1e293b; border-radius: 12px; padding: 20px; max-width: 460px; margin: 0 auto 16px auto; box-shadow: 0 4px 6px -1px rgba(0,0,0,0.3); }
    h2 { margin-top: 0; color: #38bdf8; font-size: 1.3rem; }
    .species-row { display: flex; justify-content: space-between; align-items: center; margin-bottom: 10px; padding: 8px; background: #0f172a; border-radius: 8px; }
    .species-label { font-size: 0.9rem; }
    .species-sub { font-size: 0.75rem; color: #64748b; }
    .qty-input { width: 70px; padding: 8px; border-radius: 6px; border: 1px solid #334155; background: #1e293b; color: #fff; text-align: center; font-size: 1rem; }
    label { display: block; margin: 14px 0 6px; font-size: 0.85rem; color: #94a3b8; }
    select, input[type="number"].full-width { width: 100%; padding: 10px; border-radius: 8px; border: 1px solid #334155; background: #0f172a; color: #fff; box-sizing: border-box; font-size: 1rem; }
    button { width: 100%; padding: 12px; border-radius: 8px; border: none; font-weight: bold; font-size: 0.95rem; cursor: pointer; margin-top: 10px; transition: 0.2s; }
    .btn-feed { background: #0ea5e9; color: #fff; }
    .btn-schedule { background: #10b981; color: #fff; }
    .btn-cancel { background: #ef4444; color: #fff; }
    .stats { background: #0f172a; border-radius: 8px; padding: 12px; margin-top: 14px; font-size: 0.85rem; border-left: 4px solid #38bdf8; line-height: 1.5; }
    .status-banner { padding: 10px; border-radius: 8px; margin-bottom: 14px; font-size: 0.85rem; text-align: center; font-weight: bold; }
    .status-active { background: rgba(16, 185, 129, 0.2); color: #34d399; border: 1px solid #059669; }
    .status-idle { background: rgba(148, 163, 184, 0.1); color: #94a3b8; border: 1px solid #334155; }
  </style>
</head>
<body>
  <div class="card">
    <div id="scheduleBanner" class="status-banner status-idle">
      Status: No Active Schedule
    </div>

    <h2>🐠 Tank Inhabitants</h2>
    
    <div class="species-row">
      <div>
        <div class="species-label">Guppy / Neon Tetra</div>
        <div class="species-sub">~0.03g / day per fish</div>
      </div>
      <input type="number" class="qty-input fish-qty" data-rate="0.03" value="0" min="0" oninput="recalculate()">
    </div>

    <div class="species-row">
      <div>
        <div class="species-label">Betta</div>
        <div class="species-sub">~0.06g / day per fish</div>
      </div>
      <input type="number" class="qty-input fish-qty" data-rate="0.06" value="0" min="0" oninput="recalculate()">
    </div>

    <div class="species-row">
      <div>
        <div class="species-label">Angelfish</div>
        <div class="species-sub">~0.12g / day per fish</div>
      </div>
      <input type="number" class="qty-input fish-qty" data-rate="0.12" value="0" min="0" oninput="recalculate()">
    </div>

    <div class="species-row">
      <div>
        <div class="species-label">Cichlid</div>
        <div class="species-sub">~0.20g / day per fish</div>
      </div>
      <input type="number" class="qty-input fish-qty" data-rate="0.20" value="0" min="0" oninput="recalculate()">
    </div>

    <div class="species-row">
      <div>
        <div class="species-label">Goldfish</div>
        <div class="species-sub">~0.35g / day per fish</div>
      </div>
      <input type="number" class="qty-input fish-qty" data-rate="0.35" value="0" min="0" oninput="recalculate()">
    </div>

    <label>Feeding Frequency</label>
    <select id="frequency" onchange="recalculate()">
      <option value="2">Twice Daily (Every 12 Hours)</option>
      <option value="1">Once Daily (Every 24 Hours)</option>
      <option value="3">3 Times Daily (Every 8 Hours)</option>
    </select>

    <div class="stats" id="calcBreakdown">
      Enter fish quantities to calculate requirements.
    </div>

    <button class="btn-schedule" onclick="activateSchedule()">💾 Activate Feeding Schedule</button>
    <button class="btn-cancel" onclick="cancelSchedule()">🛑 Cancel Schedule</button>
  </div>

  <div class="card">
    <h2>⚡ Manual Dispense</h2>
    <label>Portion to Dispense (Grams)</label>
    <input type="number" id="manualGrams" class="full-width" value="0.5" step="0.05" min="0.05">
    <button class="btn-feed" onclick="manualFeed()">Feed Now</button>
  </div>

  <script>
    let calculatedPerFeedGrams = 0;

    function recalculate() {
      let totalDaily = 0;
      let totalFish = 0;

      document.querySelectorAll('.fish-qty').forEach(input => {
        const count = parseInt(input.value) || 0;
        const rate = parseFloat(input.dataset.rate);
        totalDaily += count * rate;
        totalFish += count;
      });

      const freq = parseInt(document.getElementById('frequency').value);
      calculatedPerFeedGrams = freq > 0 ? (totalDaily / freq) : 0;
      const turns = (calculatedPerFeedGrams / 6.0).toFixed(2);

      document.getElementById('manualGrams').value = calculatedPerFeedGrams.toFixed(2);
      document.getElementById('calcBreakdown').innerHTML = 
        `<strong>Total Fish:</strong> ${totalFish}<br>` +
        `<strong>Combined Daily Food:</strong> ${totalDaily.toFixed(2)}g<br>` +
        `<strong>Food per Serving:</strong> ${calculatedPerFeedGrams.toFixed(2)}g (~${turns} turns)`;
    }

    function activateSchedule() {
      if (calculatedPerFeedGrams <= 0) {
        alert("Please specify at least one fish.");
        return;
      }
      const freq = parseInt(document.getElementById('frequency').value);
      const hours = 24 / freq;

      fetch(`/setSchedule?hours=${hours}&grams=${calculatedPerFeedGrams.toFixed(3)}`)
        .then(res => res.text())
        .then(msg => {
          alert(msg);
          updateStatusBanner(true, hours, calculatedPerFeedGrams.toFixed(2));
        });
    }

    function cancelSchedule() {
      fetch('/cancelSchedule')
        .then(res => res.text())
        .then(msg => {
          alert(msg);
          updateStatusBanner(false);
        });
    }

    function manualFeed() {
      const grams = document.getElementById('manualGrams').value;
      fetch(`/dispense?grams=${grams}`)
        .then(res => res.text())
        .then(alert);
    }

    function updateStatusBanner(active, hours = 0, grams = 0) {
      const banner = document.getElementById('scheduleBanner');
      if (active) {
        banner.className = "status-banner status-active";
        banner.innerHTML = `Active: ${grams}g every ${hours} hrs`;
      } else {
        banner.className = "status-banner status-idle";
        banner.innerHTML = "Status: Feeder Idle (No Schedule)";
      }
    }

    recalculate();
  </script>
</body>
</html>
)rawliteral";

// --- Stepper Execution Helper ---
void dispenseGrams(float grams) {
  if (grams <= 0) return;
  long steps = (long)((grams / GRAMS_PER_TURN) * STEPS_PER_REV);
  
  // Guard minimum micro-turn
  if (steps < 1) steps = 1;

  stepper.move(steps);
  while (stepper.distanceToGo() != 0) {
    stepper.run();
  }
}

//Edited version for github 123456

// --- Route Handlers ---
void handleRoot() {
  server.send(200, "text/html", MAIN_PAGE);
}

void handleDispense() {
  if (server.hasArg("grams")) {
    float grams = server.arg("grams").toFloat();
    dispenseGrams(grams);
    server.send(200, "text/plain", "Dispensed " + String(grams, 2) + "g successfully!");
  } else {
    server.send(400, "text/plain", "Missing grams parameter.");
  }
}

void handleSetSchedule() {
  if (server.hasArg("hours") && server.hasArg("grams")) {
    scheduledIntervalHours = server.arg("hours").toFloat();
    scheduledGramsPerFeed = server.arg("grams").toFloat();
    
    intervalMillis = (unsigned long)(scheduledIntervalHours * 3600 * 1000UL);
    autoFeedEnabled = true;
    lastFeedTime = millis(); // Start timing interval from now

    server.send(200, "text/plain", "Schedule Activated: " + String(scheduledGramsPerFeed, 2) + "g every " + String(scheduledIntervalHours, 0) + " hours.");
  } else {
    server.send(400, "text/plain", "Invalid schedule parameters.");
  }
}

void handleCancelSchedule() {
  autoFeedEnabled = false;
  scheduledGramsPerFeed = 0.0;
  scheduledIntervalHours = 0.0;
  server.send(200, "text/plain", "Active feeding schedule has been canceled.");
}

void setup() {
  Serial.begin(115200);

  stepper.setMaxSpeed(600.0);
  stepper.setAcceleration(400.0);

  WiFi.begin(ssid, password);
  Serial.print("Connecting to Wi-Fi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println("\nWi-Fi Connected!");
  Serial.print("Access App at: http://");
  Serial.println(WiFi.localIP());

  server.on("/", HTTP_GET, handleRoot);
  server.on("/dispense", HTTP_GET, handleDispense);
  server.on("/setSchedule", HTTP_GET, handleSetSchedule);
  server.on("/cancelSchedule", HTTP_GET, handleCancelSchedule);

  server.begin();
}

void loop() {
  server.handleClient();

  // --- Automatic Timer Trigger ---
  if (autoFeedEnabled && (millis() - lastFeedTime >= intervalMillis)) {
    lastFeedTime = millis();
    dispenseGrams(scheduledGramsPerFeed);
  }
}