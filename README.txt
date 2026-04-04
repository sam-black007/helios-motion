================================================================================
                    DUAL AXIS SOLAR TRACKER - TEST CASE 1
================================================================================

PROJECT OVERVIEW
--------------------------------------------------------------------------------
This is an enhanced version of the dual-axis solar tracker project featuring
improved servo control with jitter reduction and better web interface integration.

HARDWARE ARCHITECTURE
--------------------------------------------------------------------------------
- ESP32 (Main Controller)
  - Web server with real-time dashboard
  - DHT11 Temperature & Humidity sensor (GPIO 4)
  - BMP280 Pressure & Altitude sensor (I2C: SDA=21, SCL=22)
  - Serial communication with Arduino (GPIO 16=RX, GPIO 17=TX)

- Arduino UNO (Motor Controller)
  - Dual servo control (Horizontal: Pin 9, Vertical: Pin 10)
  - 4x LDR sensors for sun tracking (A0, A1, A2, A3)
  - 16x2 I2C LCD display (0x27)
  - Rotary encoder for manual control (CLK=2, DT=3, SW=4)

WIRING DIAGRAM
--------------------------------------------------------------------------------
ESP32                    Arduino UNO
------                   -----------
GPIO16 (RX)  --------->  TX (Pin 1)
GPIO17 (TX)  --------->  RX (Pin 0) [via 1k+2k voltage divider]
GPIO4         --------->  DHT11 DATA
GPIO21 (SDA)  --------->  BMP280 SDA
GPIO22 (SCL)  --------->  BMP280 SCL
3.3V          --------->  BMP280 VCC
GND           --------->  BMP280 GND

Arduino                    Servos/LDRs
-------                    ----------
Pin 9         --------->  Horizontal Servo Signal
Pin 10        --------->  Vertical Servo Signal
A0            --------->  LDR Top-Left (with 10k pull-down)
A1            --------->  LDR Top-Right (with 10k pull-down)
A2            --------->  LDR Bottom-Left (with 10k pull-down)
A3            --------->  LDR Bottom-Right (with 10k pull-down)
A4 (SDA)      --------->  LCD SDA
A5 (SCL)      --------->  LCD SCL
5V            --------->  Servos VCC, LCD VCC
GND           --------->  Servos GND, LCD GND

FEATURES
--------------------------------------------------------------------------------
1. WEB DASHBOARD (ESP32)
   - Real-time sensor monitoring
   - Manual servo control
   - LDR sensor visualization
   - OpenWeatherMap API integration
   - Flip H/V axis controls
   - Home position configuration
   - Tracking speed adjustment

2. LCD DISPLAY (Arduino)
   - Main menu navigation
   - Auto/Manual mode display
   - Weather information
   - Telemetry data
   - Encoder-based control

3. SERVO JITTER REDUCTION
   - Automatic servo detachment after 2 seconds of inactivity in MANUAL mode
   - Servo reattachment on command receipt
   - Smooth movement with configurable speed

4. LDR TRACKING
   - Exponential Moving Average (EMA) filtering
   - Configurable tolerance thresholds
   - H/V flip options for axis correction
   - Multi-step movement for precision

5. PRESET POSITIONS
   - Morning (East-facing)
   - Forenoon
   - Noon (South-facing)
   - Afternoon
   - Evening (West-facing)
   - Night (Parked position)

CONFIGURATION
--------------------------------------------------------------------------------
Edit these values in the ESP32 sketch before uploading:

const char* WIFI_SSID     = "Your_WiFi_Name";
const char* WIFI_PASSWORD = "Your_WiFi_Password";
const char* OWM_API_KEY  = "Your_OpenWeatherMap_API_Key";
#define LATITUDE   "Your_Latitude"
#define LONGITUDE  "Your_Longitude"

LIBRARIES REQUIRED
--------------------------------------------------------------------------------
ESP32 Board:
- WiFi (built-in)
- WebServer (built-in)
- HTTPClient (built-in)
- Wire (built-in)
- Adafruit BMP280 (by Adafruit)
- DHT sensor (by Adafruit)
- Preferences (built-in)

Arduino Board:
- Servo (built-in)
- Wire (built-in)
- EEPROM (built-in)
- LiquidCrystal_I2C (by Frank de Brabander)
- Encoder (by Paul Stoffregen)

INSTINCTIONS
--------------------------------------------------------------------------------
1. Upload arduino_solar_tracker_final.ino to Arduino UNO
2. Upload esp32_solar_tracker_final.ino to ESP32
3. Connect ESP32 and Arduino via Serial (TX->RX, RX->TX with voltage divider)
4. Power on and connect to the ESP32's web dashboard
5. Use web controls or LCD/encoder for manual operation

WEB DASHBOARD ACCESS
--------------------------------------------------------------------------------
1. Connect to the configured WiFi network
2. Find ESP32 IP address (shown on LCD or Serial Monitor)
3. Open browser and navigate to: http://<ESP32_IP>

API SETUP
--------------------------------------------------------------------------------
1. Create free account at openweathermap.org
2. Generate free API key
3. Update OWM_API_KEY in ESP32 sketch
4. Set your location coordinates (LATITUDE, LONGITUDE)

SERVO ANGLE REFERENCE
--------------------------------------------------------------------------------
Horizontal Servo:
- 5 degrees   = East (Morning sun)
- 90 degrees  = South (Noon sun)
- 175 degrees = West (Evening sun)

Vertical Servo:
- 5 degrees   = Horizon (Sunrise/Sunset)
- 90 degrees  = Mid-position
- 175 degrees = Zenith (Noon sun)

DEFAULT SETTINGS
--------------------------------------------------------------------------------
Home Position: H=175, V=5
Tracking Speed: 8ms step interval
LDR Tolerance: H=110, V=70
EMA Alpha: 0.12

TROUBLESHOOTING
--------------------------------------------------------------------------------
- Servos jitter: Enable FLIP H/V or adjust home position
- Web page not loading: Check WiFi connection
- LDR values all zero: Check LDR wiring and pull-down resistors
- Arduino not responding: Verify Serial connection and voltage divider

CREDITS
--------------------------------------------------------------------------------
Original Project: Sam Joseph (@sam-black007) & K M Sri Hari (@kmsrihari123)
Enhancements: Test Case 1 Implementation

LICENSE
--------------------------------------------------------------------------------
MIT License

================================================================================
