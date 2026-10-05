#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <time.h>
#include "config.h"
#include "relays.h"

// La page envoie "nom|commande|secondes" sur CMD_TOPIC.
// Le boîtier publie chaque action sur LOG_TOPIC (journal affiché par la page).
#define NTFY_HOST "ntfy.sh"
#define CMD_TOPIC "esp-" NTFY_SECRET
#define LOG_TOPIC "esp-" NTFY_SECRET "-log"

static Relays relays;
static volatile uint32_t lastOkMs = 0;

// ---------- utilitaires ----------
static bool timeSynced() { return time(nullptr) > 1700000000; }

static String fmtSec(uint32_t ms) { return String(ms / 1000.0, 1) + " s"; }

static uint32_t parseSeconds(const String& arg) {
  if (arg == "max") return MAX_MOVE_S;
  long v = arg.toInt();
  if (v <= 0) return DEFAULT_MOVE_S;
  if (v > MAX_MOVE_S) v = MAX_MOVE_S;
  return (uint32_t)v;
}

// Nettoie un pseudo : pas de saut de ligne ni '|', 24 octets max (sans couper un caractère UTF-8)
static String cleanName(String s) {
  s.replace("\n", " ");
  s.replace("\r", " ");
  s.replace("|", " ");
  s.trim();
  if (s.length() > 24) {
    int p = 24;
    while (p > 0 && (((uint8_t)s[p]) & 0xC0) == 0x80) p--;
    s = s.substring(0, p);
  }
  return s.length() ? s : String("Anonymous");
}

// ---------- journal : publication sur le canal "log" ----------
static void logEvent(const String& text) {
  Serial.println(text);
  WiFiClientSecure client;
  client.setInsecure();  // pas de vérif du certificat : OK pour une blague
  HTTPClient http;
  if (!http.begin(client, String("https://" NTFY_HOST "/" LOG_TOPIC))) return;
  http.setTimeout(8000);
  http.addHeader("Content-Type", "text/plain; charset=utf-8");
  http.POST(text);
  http.end();
}

// ---------- exécution des ordres ----------
// cmd : "up" | "down" | "stop". Retourne le texte d'annonce ("" = rien à annoncer).
static String runMove(const String& who, const String& cmd, const String& arg) {
  if (cmd == "up" || cmd == "down") {
    Dir d = (cmd == "up") ? Dir::UP : Dir::DOWN;
    uint32_t applied = relays.move(d, parseSeconds(arg) * 1000UL);
    return String(d == Dir::UP ? "⬆️ " : "⬇️ ") + who +
           (d == Dir::UP ? ": up " : ": down ") + fmtSec(applied);
  }
  if (cmd == "stop") {
    uint32_t el = relays.stop();
    if (el == 0) return "";
    return "⏹️ " + who + ": stop (" + fmtSec(el) + ")";
  }
  return "";
}

static void handleNtfy(const String& raw) {
  JsonDocument d;
  if (deserializeJson(d, raw)) return;
  if (strcmp(d["event"] | "", "message") != 0) return;
  time_t t = d["time"] | 0;
  if (timeSynced() && t && (time(nullptr) - t) > 30) return;  // ordre trop vieux : ignoré

  String m = d["message"] | "";
  int p1 = m.indexOf('|');
  if (p1 < 0) return;
  int p2 = m.indexOf('|', p1 + 1);
  String who = cleanName(m.substring(0, p1));
  String cmd = p2 < 0 ? m.substring(p1 + 1) : m.substring(p1 + 1, p2);
  String arg = p2 < 0 ? "" : m.substring(p2 + 1);
  cmd.trim(); cmd.toLowerCase();
  arg.trim(); arg.toLowerCase();
  if (cmd != "up" && cmd != "down" && cmd != "stop") return;

  String line = runMove(who, cmd, arg);
  if (line.length()) logEvent(line);
}

// ---------- flux ntfy.sh (HTTP long-lived, une ligne JSON par message) ----------
static void ntfyTask(void*) {
  while (WiFi.status() != WL_CONNECTED) vTaskDelay(pdMS_TO_TICKS(500));
  WiFiClientSecure c;
  c.setInsecure();
  c.setTimeout(5);
  for (;;) {
    if (WiFi.status() != WL_CONNECTED || !c.connect(NTFY_HOST, 443)) {
      vTaskDelay(pdMS_TO_TICKS(5000));
      continue;
    }
    c.print("GET /" CMD_TOPIC "/json HTTP/1.1\r\n"
            "Host: " NTFY_HOST "\r\n"
            "User-Agent: esp32-c3-test\r\n"
            "Connection: keep-alive\r\n\r\n");

    bool statusRead = false, headersDone = false, ok = true;
    uint32_t lastData = millis();
    // ntfy envoie un "keepalive" toutes les ~45 s ; silence > 90 s => on reconnecte
    while (ok && c.connected() && millis() - lastData < 90000) {
      if (!c.available()) {
        vTaskDelay(pdMS_TO_TICKS(20));
        continue;
      }
      String line = c.readStringUntil('\n');
      lastData = millis();
      lastOkMs = millis();
      line.trim();
      if (!statusRead) {
        statusRead = true;
        ok = line.indexOf(" 200") > 0;
        continue;
      }
      if (!headersDone) {
        if (line.length() == 0) headersDone = true;
        continue;
      }
      if (line.startsWith("{")) handleNtfy(line);  // ignore les tailles de chunks
    }
    c.stop();
    vTaskDelay(pdMS_TO_TICKS(3000));
  }
}

// ---------- setup / loop ----------
void setup() {
  relays.begin();  // en premier : sorties à l'état repos
  Serial.begin(115200);

  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  Serial.print("Wi-Fi");
  for (int i = 0; i < 40 && WiFi.status() != WL_CONNECTED; i++) {
    delay(500);
    Serial.print(".");
  }
  Serial.println(WiFi.status() == WL_CONNECTED ? " OK" : " KO (retry en tâche de fond)");

  configTzTime(TIMEZONE, "pool.ntp.org", "time.google.com");
  lastOkMs = millis();
  xTaskCreate(ntfyTask, "ntfy", 16384, nullptr, 1, nullptr);
}

void loop() {
  // Watchdog : plus aucune donnée du canal depuis 10 min => sorties à l'état repos + reboot
  if (millis() - lastOkMs > 10UL * 60UL * 1000UL) {
    relays.stop();
    ESP.restart();
  }
  delay(1000);
}
