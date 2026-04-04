/*
 * ================================================================
 * DUAL AXIS SOLAR TRACKER - ESP32 Main Controller
 * ================================================================
 * 
 * Features:
 * - Web Dashboard with real-time monitoring
 * - DHT11 Temperature & Humidity sensor
 * - BMP280 Pressure & Altitude sensor
 * - OpenWeatherMap API integration
 * - Solar energy calculations
 * - Serial communication with Arduino UNO
 * 
 * Wiring:
 * - DHT11 DATA  → GPIO 4
 * - BMP280      → I2C SDA=21, SCL=22
 * - Arduino TX  → ESP32 GPIO 16 (Serial2 RX)
 * - Arduino RX  → ESP32 GPIO 17 (Serial2 TX)
 * ================================================================
 */

#include <WiFi.h>
#include <WebServer.h>
#include <HTTPClient.h>
#include <Wire.h>
#include <Adafruit_BMP280.h>
#include <DHT.h>
#include <Preferences.h>
#include <math.h>

// ================================================================
// USER CONFIGURATION
// ================================================================
const char* WIFI_SSID     = "Sam";
const char* WIFI_PASSWORD = "123456789";
const char* OWM_API_KEY  = "9a01ccded656dde3024528ed4e28ff12";

#define LATITUDE   "13.0827"
#define LONGITUDE  "80.2707"
#define DHT_PIN    4
#define DHTTYPE    DHT11

// ================================================================
// HARDWARE INITIALIZATION
// ================================================================
DHT dht(DHT_PIN, DHTTYPE);
Adafruit_BMP280 bmp;
WebServer server(80);
Preferences preferences;

// ================================================================
// SYSTEM STATE
// ================================================================
struct SensorData {
    float temperature = 0, humidity = 0, pressure = 0;
    float altitude = 0, heatIndex = 0, dewPoint = 0;
} sensors;

struct LDRData {
    int topLeft = 0, topRight = 0, bottomLeft = 0, bottomRight = 0;
} ldr;

struct EnergyData {
    float irradiance = 0, power = 0, peakPower = 0;
    float energyWh = 0, carbonSaved = 0;
    unsigned long lastCalcTime = 0;
} energy;

struct WeatherData {
    float temperature = 0, humidity = 0, pressure = 0, feelsLike = 0;
    float dewPoint = 0, windSpeed = 0, windDegree = 0, cloudCover = 0;
    float precipitation = 0, uvIndex = 0, irradiance = 0;
    String condition = "--", windDirection = "--", icon = "cloud";
    bool fetched = false;
} weather;

struct SystemState {
    int servoH = 175, servoV = 5, homeH = 175, homeV = 5, trackingSpeed = 8;
    bool flipH = false, flipV = false;
    String wifiIP = "";
    bool wifiConnected = false;
    unsigned long uptime = 0;
} systemState;

bool flipH = false, flipV = false;
String lcdLine1 = "Solar Tracker", lcdLine2 = "Initializing...";
unsigned long lastSensorUpdate = 0, lastAPIFetch = 0;
bool apiFetched = false;
String trackerMode = "AUTO";

// ================================================================
// SOLAR CALCULATIONS
// ================================================================
float calculateDewPoint(float temp, float hum) {
    float a = 17.27f, b = 237.7f;
    float x = ((a * temp) / (b + temp)) + logf(hum / 100.0f);
    return (b * x) / (a - x);
}

float calculateHeatIndex(float temp, float hum) {
    return -8.78469475556f + 1.61139411f * temp + 2.33854883828f * hum
           - 0.14611605f * temp * hum - 0.012308094f * temp * temp
           - 0.01642482f * hum * hum + 0.00221173f * temp * temp * hum
           + 0.00072546f * temp * hum * hum - 0.000003582f * temp * temp * hum * hum;
}

float calculateIrradiance(int tl, int tr, int bl, int br) {
    return ((tl + tr + bl + br) / 4.0f / 1023.0f) * 1000.0f;
}

float calculatePower(float irr) {
    return irr * 0.5f * 0.18f;
}

float calculateUVIndex(float irr, float cloud) {
    return (irr / 1000.0f) * 12.0f * (1.0f - (cloud / 100.0f) * 0.75f);
}

float calculateCarbonSaved(float wh) {
    return wh / 1000.0f * 820.0f;
}

// ================================================================
// OPENWEATHERMAP API
// ================================================================
const char* getWeatherIcon(int weatherId) {
    if (weatherId == 800) return "sun";
    if (weatherId == 801) return "cloud-sun";
    if (weatherId <= 804) return "cloud";
    if (weatherId >= 200 && weatherId < 300) return "cloud-lightning";
    if (weatherId >= 300 && weatherId < 400) return "cloud-drizzle";
    if (weatherId >= 500 && weatherId < 600) return "cloud-rain";
    if (weatherId >= 600 && weatherId < 700) return "cloud-snow";
    return "cloud";
}

float parseJSONFloat(const String& buffer, const char* key) {
    String search = "\"" + String(key) + "\":";
    int idx = buffer.indexOf(search);
    if (idx < 0) return 0;
    int pos = idx + search.length();
    while (pos < buffer.length() && buffer[pos] == ' ') pos++;
    int end = pos;
    while (end < buffer.length() && buffer[end] != ',' && buffer[end] != '}' && buffer[end] != ']' && buffer[end] != ' ') end++;
    return buffer.substring(pos, end).toFloat();
}

String parseJSONString(const String& buffer, const char* key) {
    String search = "\"" + String(key) + "\":\"";
    int idx = buffer.indexOf(search);
    if (idx < 0) return "";
    int pos = idx + search.length();
    int end = buffer.indexOf('"', pos);
    if (end < 0) return "";
    return buffer.substring(pos, end);
}

void fetchWeatherData() {
    if (WiFi.status() != WL_CONNECTED) return;
    
    HTTPClient http;
    String baseURL = "http://api.openweathermap.org/data/2.5/";
    String location = "lat=" + String(LATITUDE) + "&lon=" + String(LONGITUDE) + "&appid=" + String(OWM_API_KEY) + "&units=metric";

    http.begin(baseURL + "weather?" + location);
    http.setTimeout(8000);
    
    if (http.GET() == 200) {
        String response = http.getString();
        
        weather.temperature = parseJSONFloat(response, "temp");
        weather.feelsLike = parseJSONFloat(response, "feels_like");
        weather.humidity = parseJSONFloat(response, "humidity");
        weather.pressure = parseJSONFloat(response, "pressure");
        weather.windSpeed = parseJSONFloat(response, "speed");
        weather.windDegree = parseJSONFloat(response, "deg");
        weather.cloudCover = parseJSONFloat(response, "all");
        
        int rainIdx = response.indexOf("\"rain\":{");
        if (rainIdx >= 0) {
            String rainData = response.substring(rainIdx, rainIdx + 60);
            weather.precipitation = parseJSONFloat(rainData, "1h");
            if (weather.precipitation == 0) weather.precipitation = parseJSONFloat(rainData, "3h");
        } else {
            weather.precipitation = 0;
        }
        
        int weatherIdx = response.indexOf("\"weather\":[{");
        if (weatherIdx >= 0) {
            String weatherInfo = response.substring(weatherIdx + 12, weatherIdx + 200);
            int weatherId = (int)parseJSONFloat(weatherInfo, "id");
            weather.icon = String(getWeatherIcon(weatherId));
            weather.condition = parseJSONString(weatherInfo, "description");
            if (weather.condition.length() > 0) weather.condition[0] = toupper(weather.condition[0]);
        }
        
        if (weather.windDegree < 22.5 || weather.windDegree >= 337.5) weather.windDirection = "N";
        else if (weather.windDegree < 67.5) weather.windDirection = "NE";
        else if (weather.windDegree < 112.5) weather.windDirection = "E";
        else if (weather.windDegree < 157.5) weather.windDirection = "SE";
        else if (weather.windDegree < 202.5) weather.windDirection = "S";
        else if (weather.windDegree < 247.5) weather.windDirection = "SW";
        else if (weather.windDegree < 292.5) weather.windDirection = "W";
        else weather.windDirection = "NW";
        
        weather.dewPoint = calculateDewPoint(weather.temperature, weather.humidity);
        weather.fetched = true;
        Serial.println("Weather updated");
    }
    http.end();

    http.begin(baseURL + "uvi?" + location);
    http.setTimeout(5000);
    if (http.GET() == 200) {
        float uv = parseJSONFloat(http.getString(), "value");
        if (uv > 0) weather.uvIndex = uv;
    }
    http.end();
}

// ================================================================
// PREFERENCES (Persistent Storage)
// ================================================================
void loadSettings() {
    preferences.begin("solar-tracker", false);
    systemState.homeH = preferences.getInt("homeH", 175);
    systemState.homeV = preferences.getInt("homeV", 5);
    systemState.trackingSpeed = preferences.getInt("speed", 8);
    flipH = preferences.getBool("flipH", false);
    flipV = preferences.getBool("flipV", false);
    preferences.end();
}

void saveSettings() {
    preferences.begin("solar-tracker", false);
    preferences.putInt("homeH", systemState.homeH);
    preferences.putInt("homeV", systemState.homeV);
    preferences.putInt("speed", systemState.trackingSpeed);
    preferences.putBool("flipH", flipH);
    preferences.putBool("flipV", flipV);
    preferences.end();
}

// ================================================================
// WEB SERVER HANDLERS
// ================================================================
void handleRoot() {
    server.send(200, "text/html; charset=utf-8", generateDashboard());
}

void handleData() {
    updateEnergyCalculations();
    
    String json = "{";
    json += "\"temp\":" + String(sensors.temperature, 1) + ",";
    json += "\"hum\":" + String(sensors.humidity, 1) + ",";
    json += "\"pres\":" + String(sensors.pressure, 1) + ",";
    json += "\"alt\":" + String(sensors.altitude, 1) + ",";
    json += "\"hi\":" + String(sensors.heatIndex, 1) + ",";
    json += "\"dp\":" + String(sensors.dewPoint, 1) + ",";
    json += "\"irr\":" + String(energy.irradiance, 1) + ",";
    json += "\"pow\":" + String(energy.power, 2) + ",";
    json += "\"peak\":" + String(energy.peakPower, 2) + ",";
    json += "\"energy\":" + String(energy.energyWh, 3) + ",";
    json += "\"co2\":" + String(energy.carbonSaved, 2) + ",";
    json += "\"uvi\":" + String(weather.uvIndex > 0 ? weather.uvIndex : calculateUVIndex(energy.irradiance, weather.cloudCover), 1) + ",";
    json += "\"servoH\":" + String(systemState.servoH) + ",";
    json += "\"servoV\":" + String(systemState.servoV) + ",";
    json += "\"homeH\":" + String(systemState.homeH) + ",";
    json += "\"homeV\":" + String(systemState.homeV) + ",";
    json += "\"mode\":\"" + trackerMode + "\",";
    json += "\"ldrTL\":" + String(ldr.topLeft) + ",";
    json += "\"ldrTR\":" + String(ldr.topRight) + ",";
    json += "\"ldrBL\":" + String(ldr.bottomLeft) + ",";
    json += "\"ldrBR\":" + String(ldr.bottomRight) + ",";
    json += "\"apiTemp\":" + String(weather.temperature, 1) + ",";
    json += "\"apiHum\":" + String(weather.humidity, 0) + ",";
    json += "\"apiPres\":" + String(weather.pressure, 0) + ",";
    json += "\"apiFeels\":" + String(weather.feelsLike, 1) + ",";
    json += "\"apiWind\":" + String(weather.windSpeed, 0) + ",";
    json += "\"apiWDir\":\"" + weather.windDirection + "\",";
    json += "\"apiCloud\":" + String(weather.cloudCover, 0) + ",";
    json += "\"apiPrecip\":" + String(weather.precipitation, 1) + ",";
    json += "\"apiCond\":\"" + weather.condition + "\",";
    json += "\"apiIcon\":\"" + weather.icon + "\",";
    json += "\"apiFetched\":" + String(weather.fetched ? "true" : "false") + ",";
    json += "\"ip\":\"" + systemState.wifiIP + "\",";
    json += "\"uptime\":" + String(systemState.uptime / 1000) + ",";
    json += "\"flipH\":" + String(flipH ? "true" : "false") + ",";
    json += "\"flipV\":" + String(flipV ? "true" : "false");
    json += "}";
    
    server.send(200, "application/json", json);
}

void handleCommand() {
    if (!server.hasArg("val")) {
        server.send(200, "text/plain", "OK");
        return;
    }
    
    String cmd = server.arg("val");
    
    if (cmd.startsWith("SH:")) {
        int angle = cmd.substring(3).toInt();
        Serial.printf("SH:%d\n", angle);
        trackerMode = "MANUAL";
    }
    else if (cmd.startsWith("SV:")) {
        int angle = cmd.substring(3).toInt();
        Serial.printf("SV:%d\n", angle);
        trackerMode = "MANUAL";
    }
    else if (cmd.startsWith("SPD:")) {
        systemState.trackingSpeed = cmd.substring(4).toInt();
        saveSettings();
        Serial.printf("SPD:%d\n", systemState.trackingSpeed);
    }
    else if (cmd == "HOME") {
        Serial.println("HOME");
        trackerMode = "MANUAL";
    }
    else if (cmd.startsWith("SETHOME:")) {
        int commaIdx = cmd.indexOf(',');
        if (commaIdx > 0) {
            systemState.homeH = cmd.substring(8, commaIdx).toInt();
            systemState.homeV = cmd.substring(commaIdx + 1).toInt();
            saveSettings();
            Serial.printf("SETHOME:%d,%d\n", systemState.homeH, systemState.homeV);
        }
    }
    else if (cmd == "FLIP:H") {
        flipH = !flipH;
        saveSettings();
        Serial.println("FLIP:H");
    }
    else if (cmd == "FLIP:V") {
        flipV = !flipV;
        saveSettings();
        Serial.println("FLIP:V");
    }
    else if (cmd.startsWith("MODE:")) {
        trackerMode = cmd.substring(5);
        Serial.println("MODE:" + trackerMode);
    }
    else {
        Serial.println(cmd);
    }
    
    server.send(200, "text/plain", "OK");
}

// ================================================================
// SENSOR READING
// ================================================================
void readSensors() {
    sensors.temperature = dht.readTemperature();
    sensors.humidity = dht.readHumidity();
    
    if (!isnan(sensors.temperature) && !isnan(sensors.humidity)) {
        sensors.heatIndex = calculateHeatIndex(sensors.temperature, sensors.humidity);
        sensors.dewPoint = calculateDewPoint(sensors.temperature, sensors.humidity);
    }
    
    sensors.pressure = bmp.readPressure() / 100.0F;
    sensors.altitude = bmp.readAltitude(1013.25);
}

void parseLDRData(String data) {
    int c1 = data.indexOf(',');
    int c2 = data.indexOf(',', c1 + 1);
    int c3 = data.indexOf(',', c2 + 1);
    
    if (c1 > 0 && c2 > c1 && c3 > c2) {
        ldr.topLeft = data.substring(0, c1).toInt();
        ldr.topRight = data.substring(c1 + 1, c2).toInt();
        ldr.bottomLeft = data.substring(c2 + 1, c3).toInt();
        ldr.bottomRight = data.substring(c3 + 1).toInt();
        
        energy.irradiance = calculateIrradiance(ldr.topLeft, ldr.topRight, ldr.bottomLeft, ldr.bottomRight);
        energy.power = calculatePower(energy.irradiance);
    }
}

void parsePositionData(String data) {
    int comma = data.indexOf(',');
    if (comma > 0) {
        systemState.servoH = data.substring(0, comma).toInt();
        systemState.servoV = data.substring(comma + 1).toInt();
    }
}

void parseLCDData(String data) {
    int pipe = data.indexOf('|');
    if (pipe > 0) {
        lcdLine1 = data.substring(0, pipe);
        lcdLine2 = data.substring(pipe + 1);
    }
}

// ================================================================
// ENERGY CALCULATIONS
// ================================================================
void updateEnergyCalculations() {
    if (energy.power > energy.peakPower) {
        energy.peakPower = energy.power;
    }
    
    unsigned long now = millis();
    if (energy.lastCalcTime > 0) {
        energy.energyWh += energy.power * ((now - energy.lastCalcTime) / 3600000.0f);
    }
    energy.lastCalcTime = now;
    energy.carbonSaved = calculateCarbonSaved(energy.energyWh);
}

// ================================================================
// WEB DASHBOARD
// ================================================================
String generateDashboard() {
    String html = "<!DOCTYPE html><html><head><meta charset='UTF-8'><meta name='viewport' content='width=device-width,initial-scale=1'>";
    html += "<title>Solar Tracker Pro</title>";
    html += "<style>";
    html += "*{box-sizing:border-box;margin:0;padding:0}:root{--bg:#03060f;--bg2:#080f1e;--bg3:#0d1929;--card:#0a1628;--border:#1a3a5c;";
    html += "--cyan:#00d4ff;--green:#00ff88;--amber:#ffaa00;--red:#ff4455;--purple:#a855f7;--text:#c8d8e8;--muted:#4a6a8a}";
    html += "body{font-family:Arial,sans-serif;background:var(--bg);color:var(--text);padding:12px}";
    html += ".header{display:flex;justify-content:space-between;align-items:center;padding:12px;background:var(--card);";
    html += "border:1px solid var(--border);border-radius:12px;margin-bottom:16px}";
    html += ".logo{font-size:18px;font-weight:bold;color:var(--cyan)}.logo span{color:var(--amber)}";
    html += ".badges{display:flex;gap:10px;align-items:center;flex-wrap:wrap}";
    html += ".badge{padding:4px 12px;border-radius:20px;font-size:11px;font-weight:600}";
    html += ".badge-wifi{background:#00ff8815;color:var(--green);border:1px solid #00ff8840}";
    html += ".badge-mode{background:#00d4ff15;color:var(--cyan);border:1px solid #00d4ff40}";
    html += ".badge-api{background:#a855f715;color:var(--purple);border:1px solid #a855f730}";
    html += ".badge-offline{background:#ff445515;color:var(--red);border:1px solid #ff445540}";
    html += ".grid-4{display:grid;grid-template-columns:repeat(4,1fr);gap:10px;margin-bottom:12px}";
    html += ".card{background:var(--card);border:1px solid var(--border);border-radius:10px;padding:14px;position:relative;overflow:hidden}";
    html += ".card::before{content:'';position:absolute;top:0;left:0;right:0;height:2px;background:linear-gradient(90deg,transparent,var(--cyan),transparent);opacity:0.3}";
    html += ".card.green::before{background:linear-gradient(90deg,transparent,var(--green),transparent)}";
    html += ".card.amber::before{background:linear-gradient(90deg,transparent,var(--amber),transparent)}";
    html += ".card.purple::before{background:linear-gradient(90deg,transparent,var(--purple),transparent)}";
    html += ".card-value{font-size:26px;font-weight:bold;color:var(--cyan);line-height:1;margin-bottom:2px}";
    html += ".card-value.green{color:var(--green)}.card-value.amber{color:var(--amber)}.card-value.purple{color:var(--purple)}";
    html += ".card-unit{font-size:12px;color:var(--muted);margin-bottom:2px}";
    html += ".card-label{font-size:11px;color:var(--muted);letter-spacing:1px;text-transform:uppercase}";
    html += ".card-sub{font-size:12px;color:var(--muted);margin-top:4px}";
    html += ".section-title{font-size:12px;font-weight:bold;color:var(--cyan);letter-spacing:2px;text-transform:uppercase;";
    html += "margin-bottom:10px;display:flex;align-items:center;gap:8px}";
    html += ".section-title::after{content:'';flex:1;height:1px;background:var(--border)}";
    html += ".badge-small{display:inline-block;padding:2px 8px;border-radius:10px;font-size:10px;background:#00ff8810;";
    html += "color:var(--green);border:1px solid #00ff8830;margin-left:8px}";
    html += ".ldr-grid{display:grid;grid-template-columns:1fr 1fr;gap:8px}";
    html += ".ldr-cell{background:var(--bg3);border:1px solid var(--border);border-radius:8px;padding:10px;text-align:center}";
    html += ".ldr-label{font-size:10px;color:var(--muted);letter-spacing:1px;margin-bottom:4px}";
    html += ".ldr-value{font-size:20px;color:var(--amber)}";
    html += ".ldr-bar{height:4px;background:var(--bg2);border-radius:2px;margin-top:6px;overflow:hidden}";
    html += ".ldr-fill{height:100%;border-radius:2px;background:var(--amber);transition:width 0.4s}";
    html += ".tabs{display:flex;gap:5px;margin-bottom:12px;flex-wrap:wrap}";
    html += ".tab{flex:1;padding:8px 4px;border:1px solid var(--border);border-radius:8px;background:var(--card);";
    html += "color:var(--muted);font-size:11px;font-weight:600;cursor:pointer;text-align:center;letter-spacing:1px;min-width:60px;transition:all 0.2s}";
    html += ".tab:hover{border-color:var(--cyan);color:var(--cyan)}";
    html += ".tab.active{border-color:var(--cyan);color:var(--cyan);background:linear-gradient(135deg,#001a2a,#00d4ff15)}";
    html += ".slider-row{margin-bottom:14px}";
    html += ".slider-label{display:flex;justify-content:space-between;font-size:13px;margin-bottom:6px;color:var(--text);font-weight:600}";
    html += ".slider-value{font-size:16px;color:var(--cyan)}";
    html += "input[type='range']{width:100%;-webkit-appearance:none;height:6px;border-radius:3px;background:var(--bg3);cursor:pointer;accent-color:var(--cyan)}";
    html += "input[type='range']::-webkit-slider-thumb{-webkit-appearance:none;width:16px;height:16px;border-radius:50%;background:var(--cyan);cursor:pointer;box-shadow:0 0 8px var(--cyan)}";
    html += ".btn-row{display:flex;gap:8px;margin-top:8px;flex-wrap:wrap}";
    html += ".btn{flex:1;padding:10px 8px;border:1px solid var(--border);background:var(--card);color:var(--text);";
    html += "border-radius:8px;font-size:12px;font-weight:600;cursor:pointer;text-align:center;letter-spacing:1px;transition:all 0.15s;min-width:60px}";
    html += ".btn:hover,.btn:active{border-color:var(--cyan);color:var(--cyan);background:#00d4ff10}";
    html += ".btn.success:hover{border-color:var(--green);color:var(--green);background:#00ff8810}";
    html += ".panel{display:none}.panel.active{display:block}";
    html += ".flip-row{display:flex;gap:8px;margin-top:8px}";
    html += ".flip-btn{flex:1;padding:8px;border:1px solid var(--border);background:var(--bg3);color:var(--muted);";
    html += "border-radius:6px;font-size:11px;cursor:pointer;letter-spacing:1px;text-align:center;transition:all 0.2s}";
    html += ".flip-btn.on{border-color:var(--amber);color:var(--amber);background:#ffaa0010}";
    html += ".progress-bar{height:10px;background:var(--bg2);border-radius:5px;margin-top:8px;overflow:hidden}";
    html += ".progress-fill{height:100%;border-radius:5px;background:linear-gradient(90deg,var(--amber),var(--green));transition:width 1s}";
    html += ".status-bar{text-align:center;font-size:11px;color:var(--green);padding:6px;min-height:20px;letter-spacing:1px;margin-top:8px}";
    html += ".status-bar.error{color:var(--red)}";
    html += ".tracker-visual{position:relative;height:16px;background:var(--bg2);border-radius:8px;margin:6px 0}";
    html += ".tracker-dot{position:absolute;width:16px;height:16px;border-radius:50%;background:var(--cyan);top:0;transform:translateX(-50%);box-shadow:0 0 10px var(--cyan);transition:left 0.15s}";
    html += ".grid-2{display:grid;grid-template-columns:1fr 1fr;gap:10px;margin-bottom:12px}";
    html += "@media(max-width:600px){.grid-3,.grid-4{grid-template-columns:1fr 1fr}.card-value{font-size:20px}}";
    html += "</style></head><body>";
    
    html += "<div class='header'><div><div class='logo'>SOLAR<span>TRACK</span> PRO</div>";
    html += "<div style='font-size:11px;color:var(--muted);margin-top:2px'>DUAL AXIS</div></div>";
    html += "<div class='badges'>";
    html += "<span class='badge badge-wifi' id='wifiBadge'>CONNECTING</span>";
    html += "<span class='badge badge-mode' id='modeBadge'>AUTO</span>";
    html += "<span class='badge badge-api' id='apiBadge'>OWM</span>";
    html += "<span id='clock' style='font-size:13px;color:var(--muted)'>--:--:--</span>";
    html += "</div></div>";
    
    html += "<div class='section-title'>LIVE SENSORS <span class='badge-small'>DHT11 + BMP280</span></div>";
    html += "<div class='grid-4'>";
    html += "<div class='card'><div class='card-value' id='tempValue'>--</div><div class='card-unit'>C</div><div class='card-label'>Temperature</div><div class='card-sub' id='heatIndex'>Heat: --</div></div>";
    html += "<div class='card purple'><div class='card-value purple' id='humValue'>--</div><div class='card-unit'>%</div><div class='card-label'>Humidity</div><div class='card-sub' id='dewPoint'>Dew: --</div></div>";
    html += "<div class='card amber'><div class='card-value amber' id='presValue'>--</div><div class='card-unit'>hPa</div><div class='card-label'>Pressure</div><div class='card-sub' id='pressureTrend'>Stable</div></div>";
    html += "<div class='card'><div class='card-value' id='altValue'>--</div><div class='card-unit'>m</div><div class='card-label'>Altitude</div><div class='card-sub' id='pressureOutlook'>--</div></div>";
    html += "</div>";
    
    html += "<div class='section-title'>SOLAR ENERGY</div>";
    html += "<div class='grid-4'>";
    html += "<div class='card green'><div class='card-value green' id='irrValue'>--</div><div class='card-unit'>W/m2</div><div class='card-label'>Irradiance</div><div class='progress-bar'><div class='progress-fill' id='irrBar' style='width:0%'></div></div></div>";
    html += "<div class='card amber'><div class='card-value amber' id='powValue'>--</div><div class='card-unit'>W</div><div class='card-label'>Power</div><div class='card-sub' id='peakPower'>Peak: --</div></div>";
    html += "<div class='card'><div class='card-value' id='energyValue'>0.00</div><div class='card-unit'>Wh</div><div class='card-label'>Energy</div><div class='card-sub' id='co2Saved'>CO2: --</div></div>";
    html += "<div class='card purple'><div class='card-value purple' id='uviValue'>--</div><div class='card-unit'>UV</div><div class='card-label'>UV Index</div><div class='card-sub' id='uviRisk'>Risk: --</div></div>";
    html += "</div>";
    
    html += "<div class='section-title'>OPENWEATHER <span class='badge-small' id='apiStatus'>PENDING</span></div>";
    html += "<div class='grid-4'>";
    html += "<div class='card'><div class='card-value' id='apiTemp'>--</div><div class='card-unit'>C</div><div class='card-label'>OWM Temp</div><div class='card-sub' id='apiFeels'>Feels: --</div></div>";
    html += "<div class='card amber'><div class='card-value amber' id='apiWind'>--</div><div class='card-unit'>km/h</div><div class='card-label'>Wind</div><div class='card-sub' id='apiWindDir'>Dir: --</div></div>";
    html += "<div class='card'><div class='card-value' id='apiCond'>--</div><div class='card-unit' id='apiIcon'>cloud</div><div class='card-label'>Condition</div><div class='card-sub' id='apiCloud'>Cloud: --%</div></div>";
    html += "<div class='card purple'><div class='card-value purple' id='apiPrecip'>--</div><div class='card-unit'>mm</div><div class='card-label'>Rain</div><div class='card-sub'>Last hour</div></div>";
    html += "</div>";
    
    html += "<div class='section-title'>TRACKER STATUS</div>";
    html += "<div class='grid-2'>";
    html += "<div class='card green'><div class='card-value green' id='servoH'>175</div><div class='card-label'>Horizontal</div><div class='tracker-visual'><div class='tracker-dot' id='hDot' style='left:100%'></div></div></div>";
    html += "<div class='card'><div class='card-value' id='servoV'>5</div><div class='card-label'>Vertical</div><div class='tracker-visual'><div class='tracker-dot' id='vDot' style='left:3%;background:var(--green);box-shadow:0 0 10px var(--green)'></div></div></div>";
    html += "</div>";
    
    html += "<div class='tabs'>";
    html += "<div class='tab active' onclick='showTab(\"auto\")'>AUTO</div>";
    html += "<div class='tab' onclick='showTab(\"manual\")'>MANUAL</div>";
    html += "<div class='tab' onclick='showTab(\"settings\")'>SETTINGS</div>";
    html += "</div>";
    
    html += "<div id='panel-auto' class='panel active'>";
    html += "<div class='card'><div class='section-title'>LDR SENSOR ARRAY</div>";
    html += "<div class='ldr-grid'>";
    html += "<div class='ldr-cell'><div class='ldr-label'>TOP LEFT</div><div class='ldr-value' id='ldrTL'>0</div><div class='ldr-bar'><div class='ldr-fill' id='barTL'></div></div></div>";
    html += "<div class='ldr-cell'><div class='ldr-label'>TOP RIGHT</div><div class='ldr-value' id='ldrTR'>0</div><div class='ldr-bar'><div class='ldr-fill' id='barTR'></div></div></div>";
    html += "<div class='ldr-cell'><div class='ldr-label'>BOTTOM LEFT</div><div class='ldr-value' id='ldrBL'>0</div><div class='ldr-bar'><div class='ldr-fill' id='barBL'></div></div></div>";
    html += "<div class='ldr-cell'><div class='ldr-label'>BOTTOM RIGHT</div><div class='ldr-value' id='ldrBR'>0</div><div class='ldr-bar'><div class='ldr-fill' id='barBR'></div></div></div>";
    html += "</div>";
    html += "<div class='flip-row'>";
    html += "<div class='flip-btn' id='flipH' onclick='toggleFlip(\"H\")'>FLIP H AXIS</div>";
    html += "<div class='flip-btn' id='flipV' onclick='toggleFlip(\"V\")'>FLIP V AXIS</div>";
    html += "</div></div></div>";
    
    html += "<div id='panel-manual' class='panel'>";
    html += "<div class='card'><div class='section-title'>MANUAL CONTROL</div>";
    html += "<div class='slider-row'><div class='slider-label'><span>HORIZONTAL</span><span class='slider-value' id='hValue'>175</span></div>";
    html += "<input type='range' min='5' max='175' value='175' id='sliderH' oninput='document.getElementById(\"hValue\").textContent=this.value'></div>";
    html += "<div class='slider-row'><div class='slider-label'><span>VERTICAL</span><span class='slider-value' id='vValue'>5</span></div>";
    html += "<input type='range' min='5' max='175' value='5' id='sliderV' oninput='document.getElementById(\"vValue\").textContent=this.value'></div>";
    html += "<div class='btn-row'>";
    html += "<button class='btn' onclick='sendCmd(\"HOME\")'>HOME</button>";
    html += "<button class='btn' onclick='sendCmd(\"SH:90\")'>CENTER</button>";
    html += "<button class='btn' onclick='sendCmd(\"SH:5\");sendCmd(\"SV:5\")'>MIN</button>";
    html += "<button class='btn' onclick='sendCmd(\"SH:175\");sendCmd(\"SV:175\")'>MAX</button>";
    html += "</div></div></div>";
    
    html += "<div id='panel-settings' class='panel'>";
    html += "<div class='card'><div class='section-title'>HOME POSITION</div>";
    html += "<div class='grid-2' style='margin-bottom:12px'>";
    html += "<div><div class='slider-label'>Home H: <span id='newHomeH'>175</span></div>";
    html += "<input type='range' min='5' max='175' value='175' id='homeHSlider' oninput='document.getElementById(\"newHomeH\").textContent=this.value'></div>";
    html += "<div><div class='slider-label'>Home V: <span id='newHomeV'>5</span></div>";
    html += "<input type='range' min='5' max='175' value='5' id='homeVSlider' oninput='document.getElementById(\"newHomeV\").textContent=this.value'></div>";
    html += "</div>";
    html += "<div class='btn-row'>";
    html += "<button class='btn success' onclick='saveHome()'>SAVE HOME</button>";
    html += "<button class='btn' onclick='sendCmd(\"HOME\")'>GO HOME</button>";
    html += "</div></div>";
    html += "<div class='card' style='margin-top:12px'><div class='section-title'>TRACKING SPEED</div>";
    html += "<div class='slider-row'><div class='slider-label'><span>Speed</span><span class='slider-value' id='speedValue'>8</span>ms</div>";
    html += "<input type='range' min='2' max='50' value='8' id='speedSlider' oninput='document.getElementById(\"speedValue\").textContent=this.value' onchange='sendCmd(\"SPD:\"+this.value)'></div>";
    html += "</div></div>";
    
    html += "<div class='status-bar' id='statusBar'>SYSTEM READY</div>";
    
    html += "<script>";
    html += "setInterval(()=>{var n=new Date();document.getElementById('clock').textContent=n.getHours().toString().padStart(2,'0')+':'+n.getMinutes().toString().padStart(2,'0')+':'+n.getSeconds().toString().padStart(2,'0')},1000);";
    html += "function showTab(tab){document.querySelectorAll('.panel').forEach(p=>p.classList.remove('active'));document.querySelectorAll('.tab').forEach(t=>t.classList.remove('active'));document.getElementById('panel-'+tab).classList.add('active');event.target.classList.add('active')};";
    html += "function showStatus(msg,isError){var el=document.getElementById('statusBar');el.textContent=msg;el.className=isError?'status-bar error':'status-bar';setTimeout(()=>{el.textContent='SYSTEM READY';el.className='status-bar'},3000)};";
    html += "function sendCmd(cmd){fetch('/cmd?val='+encodeURIComponent(cmd)).then(r=>r.text()).then(()=>showStatus('Sent: '+cmd)).catch(()=>showStatus('Error',true))};";
    html += "function toggleFlip(axis){sendCmd('FLIP:'+axis);document.getElementById('flip'+axis).classList.toggle('on')};";
    html += "function saveHome(){var h=document.getElementById('homeHSlider').value;var v=document.getElementById('homeVSlider').value;sendCmd('SETHOME:'+h+','+v);showStatus('Home saved')};";
    html += "document.getElementById('sliderH').addEventListener('change',function(){sendCmd('SH:'+this.value);updateTracker('h',this.value)});";
    html += "document.getElementById('sliderV').addEventListener('change',function(){sendCmd('SV:'+this.value);updateTracker('v',this.value)});";
    html += "function updateTracker(axis,value){document.getElementById(axis+'Dot').style.left=((value-5)/170)*86+7+'%'};";
    html += "function getUVRisk(uv){if(uv<3)return'Low';if(uv<6)return'Moderate';if(uv<8)return'High';if(uv<11)return'Very High';return'Extreme'};";
    html += "function getUVColor(uv){if(uv<3)return'var(--green)';if(uv<6)return'var(--amber)';return'var(--red)'};";
    html += "function fetchData(){fetch('/data').then(r=>r.json()).then(d=>{";
    html += "var wb=document.getElementById('wifiBadge');if(d.ip){wb.textContent='WIFI';wb.className='badge badge-wifi'}else{wb.textContent='OFFLINE';wb.className='badge badge-offline'};";
    html += "document.getElementById('modeBadge').textContent=d.mode;";
    html += "document.getElementById('tempValue').textContent=d.temp.toFixed(1);document.getElementById('humValue').textContent=d.hum.toFixed(1);";
    html += "document.getElementById('presValue').textContent=d.pres.toFixed(1);document.getElementById('altValue').textContent=d.alt.toFixed(0);";
    html += "document.getElementById('heatIndex').textContent='Heat: '+d.hi.toFixed(1);document.getElementById('dewPoint').textContent='Dew: '+d.dp.toFixed(1);";
    html += "document.getElementById('irrValue').textContent=d.irr.toFixed(0);document.getElementById('irrBar').style.width=Math.min(d.irr/10,100)+'%';";
    html += "document.getElementById('powValue').textContent=d.pow.toFixed(1);document.getElementById('peakPower').textContent='Peak: '+d.peak.toFixed(1);";
    html += "document.getElementById('energyValue').textContent=d.energy.toFixed(2);document.getElementById('co2Saved').textContent='CO2: '+d.co2.toFixed(1);";
    html += "var uv=d.apiUVI>0?d.apiUVI:d.uvi;document.getElementById('uviValue').textContent=uv.toFixed(1);document.getElementById('uviValue').style.color=getUVColor(uv);document.getElementById('uviRisk').textContent='Risk: '+getUVRisk(uv);";
    html += "document.getElementById('servoH').textContent=d.servoH;document.getElementById('servoV').textContent=d.servoV;";
    html += "updateTracker('h',d.servoH);updateTracker('v',d.servoV);";
    html += "document.getElementById('sliderH').value=d.servoH;document.getElementById('sliderV').value=d.servoV;";
    html += "document.getElementById('hValue').textContent=d.servoH;document.getElementById('vValue').textContent=d.servoV;";
    html += "document.getElementById('homeHSlider').value=d.homeH;document.getElementById('homeVSlider').value=d.homeV;";
    html += "document.getElementById('newHomeH').textContent=d.homeH;document.getElementById('newHomeV').textContent=d.homeV;";
    html += "['TL','TR','BL','BR'].forEach(k=>{var v=d['ldr'+k];document.getElementById('ldr'+k).textContent=v;document.getElementById('bar'+k).style.width=(v/1023*100)+'%'});";
    html += "document.getElementById('flipH').classList.toggle('on',d.flipH);document.getElementById('flipV').classList.toggle('on',d.flipV);";
    html += "if(d.apiFetched){document.getElementById('apiBadge').textContent='LIVE';document.getElementById('apiBadge').className='badge badge-wifi';document.getElementById('apiStatus').textContent='LIVE'};";
    html += "document.getElementById('apiTemp').textContent=d.apiTemp.toFixed(1);document.getElementById('apiFeels').textContent='Feels: '+d.apiFeels.toFixed(1);";
    html += "document.getElementById('apiWind').textContent=d.apiWind.toFixed(0);document.getElementById('apiWindDir').textContent='Dir: '+d.apiWDir;";
    html += "document.getElementById('apiCond').textContent=d.apiCond;document.getElementById('apiIcon').textContent=d.apiIcon;";
    html += "document.getElementById('apiCloud').textContent='Cloud: '+d.apiCloud.toFixed(0)+'%';document.getElementById('apiPrecip').textContent=d.apiPrecip.toFixed(1);";
    html += "}).catch(()=>{document.getElementById('wifiBadge').textContent='OFFLINE';document.getElementById('wifiBadge').className='badge badge-offline'})};";
    html += "fetchData();setInterval(fetchData,2000);";
    html += "</script></body></html>";
    
    return html;
}

// ================================================================
// SERIAL COMMUNICATION
// ================================================================
void handleSerialInput() {
    while (Serial2.available()) {
        char c = Serial2.read();
        if (c == '\n') {
            String data = Serial2.readStringUntil('\r');
            data.trim();
            
            if (data.startsWith("LDR:")) parseLDRData(data.substring(4));
            else if (data.startsWith("POS:")) parsePositionData(data.substring(4));
            else if (data.startsWith("LCD:")) parseLCDData(data.substring(4));
            else if (data.startsWith("WIFI:")) {
                if (data.substring(5) == "FAIL") systemState.wifiConnected = false;
                else {
                    systemState.wifiConnected = true;
                    systemState.wifiIP = data.substring(5);
                }
            }
        }
    }
}

// ================================================================
// SETUP
// ================================================================
void setup() {
    Serial.begin(115200);
    Serial2.begin(9600, SERIAL_8N1, 16, 17);
    
    dht.begin();
    if (!bmp.begin(0x76)) Serial.println("BMP280 not found!");
    
    loadSettings();
    
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    Serial.print("Connecting to WiFi");
    
    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 30) {
        delay(500);
        Serial.print(".");
        attempts++;
    }
    
    if (WiFi.status() == WL_CONNECTED) {
        systemState.wifiConnected = true;
        systemState.wifiIP = WiFi.localIP().toString();
        Serial.println("\nWiFi connected! IP: " + systemState.wifiIP);
        Serial2.println("WIFI:" + systemState.wifiIP);
        fetchWeatherData();
    } else {
        Serial.println("\nWiFi connection failed!");
        Serial2.println("WIFI:FAIL");
    }
    
    server.on("/", handleRoot);
    server.on("/data", handleData);
    server.on("/cmd", handleCommand);
    server.begin();
    Serial.println("Web server started!");
    
    Serial2.println("POS:" + String(systemState.homeH) + "," + String(systemState.homeV));
}

// ================================================================
// MAIN LOOP
// ================================================================
void loop() {
    server.handleClient();
    handleSerialInput();
    
    unsigned long now = millis();
    systemState.uptime = now;
    
    if (now - lastSensorUpdate > 2000) {
        lastSensorUpdate = now;
        readSensors();
    }
    
    if (now - lastAPIFetch > 600000 && systemState.wifiConnected) {
        lastAPIFetch = now;
        fetchWeatherData();
    }
    
    delay(10);
}
