#include "net.h"
#include "settings.h"
#include "log.h"

#include <WiFi.h>
#include <DNSServer.h>
#include <ArduinoJson.h>

static DNSServer s_dns;
static bool s_apMode = false;
static bool s_staMode = false;
static String s_apSsid;

bool netConnectSta(uint32_t timeoutMs) {
  if (g_cfg.wifiSsid.isEmpty()) return false;

  WiFi.persistent(false);
  WiFi.mode(WIFI_STA);
  // The radio defaults to a power-save mode that adds latency to every request.
  // We are only up for a few seconds per wake, so trade that for speed.
  WiFi.setSleep(false);
  WiFi.begin(g_cfg.wifiSsid.c_str(), g_cfg.wifiPass.c_str());

  LOG.printf("[net] connecting to '%s'", g_cfg.wifiSsid.c_str());
  const uint32_t t0 = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - t0 < timeoutMs) {
    delay(250);
    LOG.print('.');
  }
  LOG.println();

  if (WiFi.status() != WL_CONNECTED) {
    LOG.printf("[net] failed after %lu ms (status %d)\n",
               (unsigned long)(millis() - t0), (int)WiFi.status());
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
    return false;
  }
  s_staMode = true;
  s_apMode = false;
  LOG.printf("[net] connected: %s  rssi=%d dBm  (%lu ms)\n",
             WiFi.localIP().toString().c_str(), WiFi.RSSI(),
             (unsigned long)(millis() - t0));
  return true;
}

void netStartAp(String& ssidOut) {
  // WiFi.macAddress() reads the factory MAC from efuse, so it works before the
  // driver is started (esp_wifi_get_mac() would not).
  uint8_t mac[6] = {0};
  WiFi.macAddress(mac);
  char ssid[32];
  snprintf(ssid, sizeof(ssid), "reTerminal-E1004-%02X%02X", mac[4], mac[5]);
  s_apSsid = ssid;

  WiFi.persistent(false);
  WiFi.mode(WIFI_AP);
  WiFi.softAP(ssid, AP_PASSWORD);
  delay(200);

  // Answer every DNS query with our own address so phones pop the captive
  // portal automatically instead of the user having to type an IP.
  s_dns.setErrorReplyCode(DNSReplyCode::NoError);
  s_dns.start(53, "*", WiFi.softAPIP());

  s_apMode = true;
  s_staMode = false;
  ssidOut = s_apSsid;
  LOG.printf("[net] AP '%s' up at %s (password %s)\n", ssid,
             WiFi.softAPIP().toString().c_str(), AP_PASSWORD);
}

void netLoop() {
  if (s_apMode) s_dns.processNextRequest();
}

void netStop() {
  if (s_apMode) s_dns.stop();
  WiFi.disconnect(true);
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_OFF);
  s_apMode = s_staMode = false;
  LOG.println("[net] radio off");
}

bool netIsSta() { return s_staMode; }
bool netIsAp()  { return s_apMode; }
bool netConnected() { return s_staMode && WiFi.status() == WL_CONNECTED; }

String netIp() {
  if (s_apMode) return WiFi.softAPIP().toString();
  if (s_staMode) return WiFi.localIP().toString();
  return String("0.0.0.0");
}

int netRssi() { return s_staMode ? WiFi.RSSI() : 0; }

String netSsid() {
  if (s_apMode) return s_apSsid;
  if (s_staMode) return WiFi.SSID();
  return String();
}

String netScanJson() {
  // Scanning needs the station interface even while the AP is up.
  if (!s_staMode) WiFi.mode(s_apMode ? WIFI_AP_STA : WIFI_STA);
  const int n = WiFi.scanNetworks();

  // Built with ArduinoJson, not string concatenation: an SSID is arbitrary bytes
  // chosen by someone else and may contain quotes or control characters.
  JsonDocument doc;
  JsonArray arr = doc.to<JsonArray>();
  for (int i = 0; i < n; ++i) {
    JsonObject o = arr.add<JsonObject>();
    o["ssid"] = WiFi.SSID(i);
    o["rssi"] = WiFi.RSSI(i);
    o["lock"] = WiFi.encryptionType(i) != WIFI_AUTH_OPEN;
  }
  String out;
  serializeJson(doc, out);
  WiFi.scanDelete();
  if (s_apMode) WiFi.mode(WIFI_AP);
  return out;
}
