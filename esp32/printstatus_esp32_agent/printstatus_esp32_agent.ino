// ESP32 print status agent (Firebase Realtime Database version). Every minute it:
//   1. signs in to Firebase Auth and reads the printer list from /config/printers
//   2. asks each printer for its status          (Klipper/Moonraker, Prusa/PrusaLink, Bambu LAN)
//   3. writes all printers to /status in one request (the server adds the update time)
//
// Printer list fields:  id, name, enabled, kind ("klipper" | "prusa" | "bambu"), moonrakerUrl (klipper),
//                       ip + accessCode (prusa: PrusaLink API key),
//                       ip + serial + accessCode (bambu, LAN mode must be on)
//
// Needs (Library Manager): "ArduinoJson" by Benoit Blanchon (version 7)
//                          "PubSubClient" by Nick O'Leary   (for Bambu)
// Board: ESP32 Dev Module. Serial Monitor: 115200 baud.
//
// LED: solid = last cycle pushed OK, fast blink = last cycle failed.

#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>
#include <PubSubClient.h>
#include <time.h>

// ---------- change these ----------
const char* WIFI_SSID      = "KSUM_2G";               // 2.4 GHz network
const char* WIFI_PASS      = "k$um4WiF1";
const char* FB_API_KEY     = "AIzaSyDoqgsxBkZDTWCenP5dW_CDlR8Q7iItujQ";            // Firebase console > Project settings > General > Web API Key
const char* FB_DB_URL      = "https://YOUR-DATABASE-URL";   // Realtime Database URL from the console, no trailing slash
const char* FB_EMAIL       = "agent@printer-timeline.local"; // the Authentication user made for this ESP32
const char* FB_PASSWORD    = "CHANGE_ME";
const char* AGENT_NAME     = "esp32-printstatus-1";
const unsigned long INTERVAL_MS = 60000;                      // 1 minute
const int   LED_PIN        = 2;
// ----------------------------------

const int MAX_PRINTERS = 8;
struct Printer { String id, name, kind, moonraker, ip, key, serial; bool enabled; };
// (types must be defined before the first function, or the IDE's auto-generated declarations fail)
struct Reading { String state = "offline"; float progress = -1; long remainingSec = 0; String filename, error; };
Printer printers[MAX_PRINTERS];
int nPrinters = 0;

unsigned long nextRun = 0, lastBlink = 0;
bool lastOk = false, ledOn = false;

// ---------- HTTP helper: works for http:// and https:// (https skips certificate checking) ----------
// Returns the HTTP status code (or a negative error). If out is given, the JSON reply is parsed into it.
int httpJson(const char* method, const String& url, const String& bearer, const String& apiKey,
             const String* body, JsonDocument* out, JsonDocument* filter, unsigned long timeoutMs) {
  WiFiClient plain;
  WiFiClientSecure secure;
  secure.setInsecure();
  HTTPClient http;
  bool began = url.startsWith("https://") ? http.begin(secure, url) : http.begin(plain, url);
  if (!began) return -100;
  http.setTimeout(timeoutMs);
  http.useHTTP10(true);
  if (bearer.length()) http.addHeader("Authorization", "Bearer " + bearer);
  if (apiKey.length()) http.addHeader("X-Api-Key", apiKey);
  int code;
  if (body) { http.addHeader("Content-Type", "application/json"); code = http.sendRequest(method, *body); }
  else code = http.sendRequest(method);
  if (code == 200 && out) {
    DeserializationError err = filter
      ? deserializeJson(*out, http.getStream(), DeserializationOption::Filter(*filter))
      : deserializeJson(*out, http.getStream());
    if (err) { Serial.println(String("  JSON error: ") + err.c_str()); code = -101; }
  }
  http.end();
  return code;
}

// text value of a JSON field, or a default when it is missing / not a string
String str(JsonVariantConst v, const String& def) { return v.is<const char*>() ? String(v.as<const char*>()) : def; }

String errText(int code) {
  if (code > 0) return "HTTP " + String(code);
  if (code == -1) return "cannot reach (connection refused)";
  if (code == -11) return "timed out";
  return "error " + String(code);
}

void connectWifi() {
  if (WiFi.status() == WL_CONNECTED) return;
  Serial.print("Connecting to Wi-Fi");
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  unsigned long t0 = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - t0 < 20000) { delay(500); Serial.print("."); }
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\nConnected, ESP32 IP: " + WiFi.localIP().toString());
    configTime(0, 0, "pool.ntp.org", "time.google.com");   // for the sentAt timestamp
  } else Serial.println("\nWi-Fi failed (check name/password, and that it is 2.4 GHz)");
}

// ---------- Firebase: sign in (token lasts 1 h, renewed by signing in again) ----------
String fbToken;
unsigned long fbTokenExpiry = 0;

bool firebaseSignIn() {
  if (fbToken.length() && (long)(millis() - fbTokenExpiry) < 0) return true;
  JsonDocument req, filter, res;
  req["email"] = FB_EMAIL; req["password"] = FB_PASSWORD; req["returnSecureToken"] = true;
  filter["idToken"] = true; filter["expiresIn"] = true;
  String body; serializeJson(req, body);
  int code = httpJson("POST", String("https://identitytoolkit.googleapis.com/v1/accounts:signInWithPassword?key=") + FB_API_KEY,
                      "", "", &body, &res, &filter, 10000);
  if (code != 200) {
    Serial.println("Firebase sign-in failed: " + errText(code) + (code == 400 ? " (wrong email/password, API key, or Email/Password sign-in is not enabled)" : ""));
    fbToken = ""; return false;
  }
  fbToken = str(res["idToken"], "");
  long ttl = atol(str(res["expiresIn"], "3600").c_str());
  fbTokenExpiry = millis() + (unsigned long)max(60L, ttl - 300) * 1000UL;
  return fbToken.length() > 0;
}

// ---------- 1. printer list (/config/printers: an array, or an object keyed by printer id) ----------
void readPrinter(Printer& r, JsonObject p, const String& fallbackId) {
  r.id = str(p["id"], fallbackId);
  r.name = str(p["name"], r.id);
  r.moonraker = str(p["moonrakerUrl"], "");
  while (r.moonraker.endsWith("/")) r.moonraker.remove(r.moonraker.length() - 1);
  r.ip = str(p["ip"], "");
  r.key = str(p["accessCode"], "");
  r.serial = str(p["serial"], "");
  r.kind = str(p["kind"], r.moonraker.length() ? "klipper" : "manual");
  r.enabled = p["enabled"] | true;
}

bool loadPrinters() {
  JsonDocument doc;
  int code = httpJson("GET", String(FB_DB_URL) + "/config/printers.json?auth=" + fbToken, "", "", nullptr, &doc, nullptr, 10000);
  if (code != 200) { Serial.println("Printer list failed: " + errText(code) + (code == 401 ? " (permission denied: check database.rules.json and FB_EMAIL)" : "") + (nPrinters ? " (keeping the previous list)" : "")); if (code == 401) fbToken = ""; return nPrinters > 0; }
  int n = 0;
  if (doc.is<JsonArray>()) {
    for (JsonObject p : doc.as<JsonArray>()) { if (n >= MAX_PRINTERS) break; if (p.isNull()) continue; readPrinter(printers[n], p, "p" + String(n + 1)); n++; }
  } else if (doc.is<JsonObject>()) {
    for (JsonPair kv : doc.as<JsonObject>()) { if (n >= MAX_PRINTERS) break; readPrinter(printers[n], kv.value().as<JsonObject>(), String(kv.key().c_str())); n++; }
  }
  if (n == 0) { Serial.println("config/printers is empty or missing" + String(nPrinters ? " (keeping the previous list)" : "")); return nPrinters > 0; }
  nPrinters = n;
  Serial.println("Got " + String(n) + " printer(s) from the database");
  return true;
}

// ---------- 2. poll one printer ----------

Reading pollKlipper(const Printer& p) {
  Reading r;
  JsonDocument filter;
  filter["result"]["status"]["print_stats"]["state"] = true;
  filter["result"]["status"]["print_stats"]["filename"] = true;
  filter["result"]["status"]["print_stats"]["print_duration"] = true;
  filter["result"]["status"]["virtual_sdcard"]["progress"] = true;
  filter["result"]["status"]["display_status"]["progress"] = true;
  JsonDocument doc;
  int code = httpJson("GET", p.moonraker + "/printer/objects/query?print_stats&display_status&virtual_sdcard", "", "", nullptr, &doc, &filter, 4000);
  if (code != 200) { r.error = errText(code); return r; }
  JsonObject st = doc["result"]["status"];
  String s = st["print_stats"]["state"] | "unknown";
  float prog = st["virtual_sdcard"]["progress"] | 0.0f;
  if (prog == 0) prog = st["display_status"]["progress"] | 0.0f;
  float dur = st["print_stats"]["print_duration"] | 0.0f;
  r.filename = st["print_stats"]["filename"] | "";
  if (s == "printing") r.state = "printing";
  else if (s == "paused") r.state = "paused";
  else if (s == "complete") r.state = "finished";
  else if (s == "error") r.state = "error";
  else r.state = "idle";                       // standby, cancelled
  if (r.state == "printing" || r.state == "paused") {
    r.progress = prog;
    if (r.state == "printing" && prog > 0.02) r.remainingSec = max(0L, (long)(dur / prog - dur));
  }
  return r;
}

Reading pollPrusa(const Printer& p) {
  Reading r;
  if (!p.ip.length() || !p.key.length()) { r.error = "ip and accessCode are needed"; return r; }
  JsonDocument doc;
  int code = httpJson("GET", "http://" + p.ip + "/api/v1/status", "", p.key, nullptr, &doc, nullptr, 4000);
  if (code == 401 || code == 403) { r.error = "the printer rejected the API key"; return r; }
  if (code != 200) { r.error = errText(code); return r; }
  String s = doc["printer"]["state"] | "";
  s.toUpperCase();
  if (s == "PRINTING") r.state = "printing";
  else if (s == "PAUSED") r.state = "paused";
  else if (s == "FINISHED") r.state = "finished";
  else if (s == "ERROR" || s == "ATTENTION") r.state = "error";
  else r.state = "idle";                       // IDLE, READY, BUSY, STOPPED
  if (r.state == "printing" || r.state == "paused") {
    r.progress = (doc["job"]["progress"] | 0.0f) / 100.0f;
    JsonVariant tr = doc["job"]["time_remaining"];
    r.remainingSec = tr.isNull() ? 0 : tr.as<long>();
    JsonDocument jd, jf;
    jf["file"]["display_name"] = true;
    jf["file"]["name"] = true;
    if (httpJson("GET", "http://" + p.ip + "/api/v1/job", "", p.key, nullptr, &jd, &jf, 4000) == 200) {
      r.filename = str(jd["file"]["display_name"], str(jd["file"]["name"], ""));
    }
  }
  return r;
}

// ---------- Bambu Lab (LAN mode): MQTT over TLS, port 8883, user "bblp", password = access code ----------
// Each cycle: connect, subscribe, ask for a full report ("pushall"), wait for it, disconnect.
struct BambuState { bool got = false; String state; int percent = -1; long remainingMin = -1; String filename; };
BambuState bambuSt;

void bambuCallback(char* topic, byte* payload, unsigned int length) {
  JsonDocument filter;
  filter["print"]["gcode_state"] = true;
  filter["print"]["mc_percent"] = true;
  filter["print"]["mc_remaining_time"] = true;
  filter["print"]["subtask_name"] = true;
  filter["print"]["gcode_file"] = true;
  JsonDocument doc;
  if (deserializeJson(doc, (const char*)payload, length, DeserializationOption::Filter(filter))) return;
  JsonObject pr = doc["print"];
  if (pr.isNull()) return;
  if (pr["gcode_state"].is<const char*>()) { bambuSt.state = pr["gcode_state"].as<String>(); bambuSt.got = true; }
  if (pr["mc_percent"].is<int>()) bambuSt.percent = pr["mc_percent"].as<int>();
  if (pr["mc_remaining_time"].is<int>()) bambuSt.remainingMin = pr["mc_remaining_time"].as<long>();
  if (pr["subtask_name"].is<const char*>()) bambuSt.filename = pr["subtask_name"].as<String>();
  else if (!bambuSt.filename.length() && pr["gcode_file"].is<const char*>()) bambuSt.filename = pr["gcode_file"].as<String>();
}

Reading pollBambu(const Printer& p) {
  Reading r;
  if (!p.ip.length() || !p.serial.length() || !p.key.length()) { r.error = "ip, serial and accessCode are needed"; return r; }
  WiFiClientSecure net;
  net.setInsecure();                       // the printer uses a self-signed certificate
  PubSubClient mqtt(net);
  mqtt.setServer(p.ip.c_str(), 8883);
  mqtt.setSocketTimeout(8);
  if (!mqtt.setBufferSize(20000)) { r.error = "not enough memory for the MQTT buffer"; return r; }
  mqtt.setCallback(bambuCallback);
  bambuSt = BambuState();
  String clientId = "esp32ps-" + String((uint32_t)ESP.getEfuseMac(), HEX);
  if (!mqtt.connect(clientId.c_str(), "bblp", p.key.c_str())) {
    int st = mqtt.state();
    r.error = (st == 4 || st == 5) ? "wrong access code" : "cannot connect (check the IP, and that LAN mode is on)";
    return r;
  }
  mqtt.subscribe(("device/" + p.serial + "/report").c_str());
  mqtt.publish(("device/" + p.serial + "/request").c_str(), "{\"pushing\":{\"sequence_id\":\"1\",\"command\":\"pushall\"}}");
  unsigned long t0 = millis();
  while (!bambuSt.got && millis() - t0 < 10000) { mqtt.loop(); delay(20); }
  mqtt.disconnect();
  if (!bambuSt.got) { r.error = "connected, but no status arrived (check the serial number)"; return r; }

  const String& g = bambuSt.state;
  if (g == "RUNNING" || g == "PREPARE") r.state = "printing";
  else if (g == "PAUSE") r.state = "paused";
  else if (g == "FINISH") r.state = "finished";
  else if (g == "FAILED") r.state = "error";
  else r.state = "idle";                   // IDLE and anything else
  if (r.state == "printing" || r.state == "paused") {
    r.progress = bambuSt.percent >= 0 ? bambuSt.percent / 100.0f : 0;
    if (bambuSt.remainingMin >= 0) r.remainingSec = bambuSt.remainingMin * 60;
    r.filename = bambuSt.filename;
  }
  return r;
}

Reading pollPrinter(const Printer& p) {
  if (p.kind == "klipper" && p.moonraker.length()) return pollKlipper(p);
  if (p.kind == "prusa") return pollPrusa(p);
  if (p.kind == "bambu") return pollBambu(p);
  Reading r; r.state = "unsupported";
  r.error = "no live status for kind '" + p.kind + "'";
  return r;
}

// ---------- 3. write the result to /status (one PUT replaces the whole node) ----------
bool pushResult() {
  JsonDocument out;
  for (int i = 0; i < nPrinters; i++) {
    const Printer& p = printers[i];
    if (!p.enabled) continue;
    Reading r = pollPrinter(p);
    Serial.println("  " + p.name + ": " + r.state + (r.progress >= 0 ? "  " + String((int)(r.progress * 100)) + "%" : "") +
                   (r.state == "printing" ? "  left " + String(r.remainingSec / 60) + "m" : "") +
                   (r.error.length() ? "  (" + r.error + ")" : ""));
    JsonObject o = out[p.id].to<JsonObject>();
    o["name"] = p.name; o["kind"] = p.kind; o["state"] = r.state;
    if (r.progress >= 0) o["progress"] = r.progress;
    o["remainingSec"] = r.remainingSec;
    if (r.filename.length()) o["filename"] = r.filename;
    if (r.error.length()) o["error"] = r.error;
    o["updatedAt"][".sv"] = "timestamp";          // the database fills in the time
  }
  String body; serializeJson(out, body);
  int code = httpJson("PUT", String(FB_DB_URL) + "/status.json?auth=" + fbToken, "", "", &body, nullptr, nullptr, 10000);
  if (code == 200) { Serial.println("Written to /status"); return true; }
  Serial.println("Write failed: " + errText(code) + (code == 401 ? " (permission denied: check database.rules.json and FB_EMAIL)" : ""));
  if (code == 401) fbToken = "";                   // sign in again next cycle
  return false;
}

void runCycle() {
  Serial.println("\n--- cycle ---");
  connectWifi();
  if (WiFi.status() != WL_CONNECTED) { lastOk = false; return; }
  if (!firebaseSignIn()) { lastOk = false; return; }
  if (!loadPrinters()) { lastOk = false; return; }
  lastOk = pushResult();
}

void updateLed() {
  if (lastOk) { digitalWrite(LED_PIN, HIGH); return; }
  if (millis() - lastBlink >= 150) { lastBlink = millis(); ledOn = !ledOn; digitalWrite(LED_PIN, ledOn); }
}

void setup() {
  Serial.begin(115200);
  pinMode(LED_PIN, OUTPUT);
  delay(500);
  nextRun = millis();
}

void loop() {
  if ((long)(millis() - nextRun) >= 0) {
    runCycle();
    nextRun += INTERVAL_MS;
    if ((long)(millis() - nextRun) >= 0) nextRun = millis() + INTERVAL_MS;   // a cycle ran long: do not pile up
  }
  updateLed();
}
