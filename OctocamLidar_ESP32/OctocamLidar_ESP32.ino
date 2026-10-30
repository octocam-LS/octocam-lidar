/*
  Octocam Lidar -- ESP-NOW Camera Trigger Sender with a TF-Luna distance sensor (ESP32)
  The ESP32 successor of the Octocam ToF (OctocamToF_8266); same detection, radio, pairing
  and web app, plus the battery, home-WiFi firmware updates and a 0.91" OLED.

  Board: "T-Energy S3" clone that is really a classic ESP32 (ESP32-D0WD-V3, CP2102 USB, 18650 holder).
  Arduino: "ESP32 Dev Module", Flash Mode DIO, Flash Frequency 40 MHz (the clone's flash doesn't
           boot with QIO / 80 MHz), Partition Scheme "Minimal SPIFFS (1.9MB APP with OTA/128KB SPIFFS)"
           (the default 1.2 MB app slots are too small for HTTPS updates).
           arduino-cli: esp32:esp32:esp32:FlashMode=dio,FlashFreq=40,PartitionScheme=min_spiffs
  Libraries: "U8g2" by oliver. Everything else ships with the core.
  Power: run it from the 18650 (or a charger / power bank with the cell in). A PC USB port alone
         browns the clone out; flashing over USB still works.

  GitHub: https://github.com/octocam-LS/octocam-lidar (source + the .bin it updates from)

  ---------------------------------------------------------------------------
  WIRING
  ---------------------------------------------------------------------------
    TF-Luna pin 1 (+5V) -> 5V          (TF-Luna needs 3.7-5.2 V)
    TF-Luna pin 2 (RXD) -> IO26        (the sketch sends the sensitivity settings here)
    TF-Luna pin 3 (TXD) -> IO27        (3.3 V logic, safe for the ESP32)
    TF-Luna pin 4 (GND) -> GND
    TF-Luna pin 5/6     -> not connected (open = UART mode)

    OLED 0.91" SSD1306 128x32 I2C:  VCC -> 3V3 (VCC pin), GND -> GND, SDA -> IO21, SCL -> IO22
    Optocoupler input (via its resistor, as on the triggers) -> IO32, HIGH = shutter closed
    Battery: cell + -> 100k -> IO35 -> 100k -> GND (the clone has no battery sense of its own)
    RGB LED (one WS2812, as on the LS RGB sender): DIN -> IO25, VCC -> 3V3, GND -> GND
      battery colour at 30 %: green > 85 %, yellow > 50 %, orange > 25 %, red below, red flashing < 5 %;
      white 250 ms at full brightness on every signal sent (trigger, interval, TEST);
      blue blinking during the 8 s sender-pairing window. Dark when there is no battery reading.
    Battery: 3.10 V = 0 %, 4.20 V = 100 %. Below 3.10 V for 10 s: the RGB LED blinks red very fast
      for 5 s, then the ESP32 goes into deep sleep (opto off, WiFi off). It wakes every 60 s, measures
      and sleeps again until the cell is back above 3.30 V (charger) or the power switch is cycled.
      The TF-Luna stays powered from the 5V pin in deep sleep: switch the unit off to stop the drain.
    User LED IO16 (on the board): short blink at power-on, flashes when the battery is low
    BOOT button (IO0, on the board) = "button 1", same tiers as the ESP8266 ToF / trigger,
      decided on RELEASE; while held the OLED shows what a release would do:
        < 1 s     : switch Movement <-> Light-Barrier
        1 - 2.5 s : web app on (or a fresh idle time for a running one)
        2.5 - 5 s : extra triggers on / off
        5 - 10 s  : PAIRING: 8 s Listen-for-Sender window + 10 s pairing beacon for triggers
        10 - 20 s : CLEAR all pairings (senders and triggers)
        20 s +    : RESET the WiFi password to the default (88888888)

  ---------------------------------------------------------------------------
  WHAT IT DOES (unchanged from the ESP8266 ToF 1.5.1)
  ---------------------------------------------------------------------------
  MOVEMENT (default): only something coming TOWARDS the sensor triggers -- inside the range
    and at least "Sideways Movement Threshold" cm (20-100, default 60) closer than the farthest
    point it was seen at. LIGHT-BARRIER: anything inside the range for "Movement Filter"
    readings in a row. On a trigger: the camera on IO32 fires (duration / extras / delay set
    here) and a TRIGGER goes to every paired trigger that is on. Receiver mode: paired LS
    senders fire the local camera. Interval / Delayed Start / Timer as on the ESP8266 ToF.
  Radio: channel 11, signature 8F 3C D1 62, same packets and pairing as all Octocam devices.
  The station runs 802.11b/g/n + Long Range, so it hears both ESP32 (LR) and ESP8266 senders.

  ---------------------------------------------------------------------------
  WEB APP + FIRMWARE UPDATE
  ---------------------------------------------------------------------------
  WiFi "OctocamLidar<number>" -> http://8.8.8.8 (sign-in popup). Open for the first 10 s
  after power-on, then password (default 88888888). Off 2 min after the last phone left
  (unless Keep App Alive); BOOT 1-2.5 s brings it back. Battery % top right of the page.
  "Update Firmware": the Lidar leaves its own network, joins the home WiFi saved in the app
  (name default "birdbox"; the password is entered in the app and stored only on the device,
  never in this sketch), reads firmware/version.txt from the GitHub repo and, if it differs
  from FIRMWARE_VERSION, installs firmware/OctocamLidar_ESP32.bin and restarts. Otherwise it
  comes back to its own network. Detection is paused during the update. The OLED shows each step.

  ---------------------------------------------------------------------------
  OLED (0.91" 128x32)
  ---------------------------------------------------------------------------
    top line : trigger duration ("Pairing" while the window is open) | battery % | mode
    middle   : live distance in cm ("---" = no valid reading)
    bottom   : distance meter 0-8 m: trigger range as a bar, live distance as a marker
    frame    : flashes on every signal sent and every camera release

  SAVING: changes take effect at once, "Save Settings" stores them (password, network name,
  home WiFi and pairings are stored right away).
*/

#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <esp_mac.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <EEPROM.h>
#include <Wire.h>
#include <U8g2lib.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <HTTPUpdate.h>
#include <esp_sleep.h>

// ---------- USER-CONFIGURABLE SETTINGS ----------
#define FIRMWARE_VERSION      "1.2.0-lidar32"   // must match firmware/version.txt for a release

#define PRODUCT_SIGNATURE_0   0x8F              // must match all Octocam devices
#define PRODUCT_SIGNATURE_1   0x3C
#define PRODUCT_SIGNATURE_2   0xD1
#define PRODUCT_SIGNATURE_3   0x62

#define PACKET_TYPE_TRIGGER   0x01
#define PACKET_TYPE_INTERVAL  0x02
#define PACKET_TYPE_PAIR_ACK  0x03
#define PACKET_TYPE_PAIR_INFO 0x05     // trigger -> sender after its acks: [7] name length, [8..] SSID
#define PACKET_TYPE_BEACON    0x04
#define PACKET_LEN            7

#define MAX_PAIRED_RECEIVERS  16
#define MAX_PAIRED_SENDERS    8       // LS senders this device listens to (receiver mode)
#define SENDER_PAIR_WINDOW_MS 8000UL  // "Listen for Sender" window
#define PAIR_ACK_COPIES       3
#define PAIR_ACK_GAP_MS       30
#define WIFI_CHANNEL          11

// ----- pins -----
#define OPTO_PIN              32      // -> optocoupler -> camera
#define OPTO_ACTIVE_HIGH      true
#define LUNA_RX_PIN           27      // <- TF-Luna TXD (UART2)
#define LUNA_TX_PIN           26      // -> TF-Luna RXD
#define WAKE_BUTTON_PIN       0       // the BOOT button on the board
#define OLED_SDA              21
#define OLED_SCL              22
#define LED_PIN               16      // user LED on the board
#define LED_ACTIVE_LOW        true    // flip if the LED is lit all the time and goes dark on the blinks
#define BAT_PIN               35      // cell + -> 100k -> IO35 -> 100k -> GND

// ----- battery -----
#define BAT_DIVIDER           2.0f    // 100k / 100k
#define BAT_SAMPLES           32
#define BAT_MEASURE_MS        500
#define BAT_EMA_ALPHA         0.15f
#define BAT_NONE_BELOW_V      2.50f   // no cell / divider not fitted
#define BAT_USB_ABOVE_V       4.35f   // charger output without a cell
#define LOW_BAT_PCT           15      // LED flash every 4 s at or below this ...
#define LOW_BAT_CLEAR_PCT     20      // ... until the cell is back above this
#define CRIT_BAT_PCT          5       // flash every 1 s at or below this
#define BAT_EMPTY_V           3.10f   // = 0 %; below this the ESP32 goes into deep sleep
#define BAT_EMPTY_HOLD_MS     10000UL // must stay below BAT_EMPTY_V this long (load dips don't count)
#define BAT_SLEEP_WARN_MS     5000UL  // very fast red blink this long before deep sleep
#define BAT_SLEEP_BLINK_MS    60      // on / off time of that blink
#define BAT_SLEEP_CHECK_S     60      // deep sleep wakes this often to measure the cell
#define BAT_WAKE_V            3.30f   // after a battery sleep: stay awake only above this (cell recovers when resting)
#define LED_BOOT_BLINK_MS     150
#define LED_FLASH_MS          80

// ----- RGB status LED (WS2812 on IO25, driven with the core's neopixelWrite) -----
#define RGB_PIN               25
#define RGB_DIM               77      // 30 % of 255: battery colours and the pairing blink
#define RGB_FLASH_MS          250     // white at full brightness on every signal sent
#define RGB_BLINK_MS          500     // on / off time of the critical-battery and pairing blinks
#define BAT_GREEN_PCT         85      // colour thresholds: green above, yellow above 50, orange above 25,
#define BAT_YELLOW_PCT        50      // red below, red flashing below CRIT_BAT_PCT
#define BAT_ORANGE_PCT        25
#define BAT_COLOR_HYST        2       // a colour only changes once the % is this far past the threshold

// ----- TF-Luna -----
#define LUNA_BAUD             115200
#define LUNA_RANGE_CM         800     // full ruler range in the web app = TF-Luna max range
#define LUNA_TIMEOUT_MS       300     // no valid frame for this long -> "no sensor"
#define DEFAULT_LUNA_RATE     100     // readings/s  -- "Bright" preset
#define DEFAULT_LUNA_MIN_AMP  100     // Min Signal = ISO 400 -- "Bright" preset (TF-Luna datasheet limit)

// ----- trigger range + filter (saved) -----
#define DEFAULT_MIN_CM        30      // default range 0.3-8 m (both modes)
#define DEFAULT_MAX_CM        800
#define MIN_LIMIT_CM          20      // TF-Luna blind zone
#define MIN_SPAN_CM           10      // min and max can't get closer than this
#define DEFAULT_FILTER        2       // Movement Filter: readings in a row inside the range -- "Bright" preset
#define MIN_FILTER            1
#define MAX_FILTER            8       // slider range 1-8
#define REARM_MS              300     // object must be out of range this long before it can trigger again

// ----- trigger mode (saved) -----
#define MODE_MOVEMENT         0
#define MODE_BARRIER          1
#define DEFAULT_MODE          MODE_MOVEMENT
#define MOVE_MIN_SPAN_CM      100     // Movement mode: min and max at least 1 m apart
#define DEFAULT_MOVE_FILTER_CM 60     // must come this much closer to trigger (20..100, 20 cm steps)
#define MIN_MOVE_FILTER_CM    20
#define MAX_MOVE_FILTER_CM    100
#define MOVE_MAX_SPEED_CM_PER_MS 4    // 40 m/s: a bigger jump between two readings is a different object
#define MOVE_JUMP_MARGIN_CM   15      // + this much for sensor noise
#define MOVE_TRACK_GAP_MS     250     // no valid reading for this long ends the track
// BOOT button tiers (upper bounds, same as the trigger's button 1)
#define SHORT_PRESS_UPPER_MS    1000UL  // < this: switch Movement / Light-Barrier
#define WEBAPP_PRESS_UPPER_MS   2500UL  // < this: web app on
#define MEDIUM_PRESS_UPPER_MS   5000UL  // < this: extra triggers on / off
#define PAIRING_PRESS_UPPER_MS 10000UL  // < this: pairing
#define CLEAR_PAIR_UPPER_MS    20000UL  // < this: clear all pairings; longer: reset password
#define BUTTON_MSG_MS           1500UL  // result message on the OLED

// ----- local camera output -----
#define LEGACY_DURATIONS      5       // old preset slots, still in the saved-settings layout
#define DEFAULT_TRIGGER_DURATION_MS 250
#define MIN_DURATION_MS       10
#define MAX_DURATION_MS       10000
#define DEFAULT_EXTRA_SHOTS_COUNT 1
#define MAX_EXTRA_SHOTS_TYPED 200
#define DEFAULT_EXTRA_GAP_MS  50
#define MIN_EXTRA_GAP_MS      10
#define MAX_EXTRA_GAP_MS      10000
#define DEFAULT_TRIGGER_DELAY_MS 0
#define MAX_TRIGGER_DELAY_MS  10000

// ----- interval (RAM only, never saved) -----
#define DEFAULT_INTERVAL_MS   2000
#define MIN_INTERVAL_MS       300
#define MAX_INTERVAL_MS       10000
#define MAX_TIMER_MINUTES     10000UL

// ----- pairing beacon -----
#define BEACON_INTERVAL_MS     500UL  // pairing hello every 0.5 s ...
#define BEACON_DURATION_MS   10000UL  // ... for 10 s after power-on / "Pairing Signal"
#define PAIR_ACK_GRACE_MS     1000UL  // acks still accepted this long after the beacon (in flight)
#define SENDER_NAME_MAX         32    // SSID length limit (the name the trigger shows)

// ----- home WiFi + firmware update from GitHub -----
#define DEFAULT_WIFI_SSID     "birdbox"   // only the name; the password is entered in the app
#define WIFI_JOIN_TIMEOUT_MS  20000UL
#define UPDATE_BASE_URL       "https://raw.githubusercontent.com/octocam-LS/octocam-lidar/main/firmware/"
#define UPDATE_VERSION_FILE   "version.txt"
#define UPDATE_BIN_FILE       "OctocamLidar_ESP32.bin"

// ----- web app -----
#define PASSWORD_GRACE_MS     10000UL     // network is always open this long after power-on
#define DEFAULT_AP_PASSWORD   "88888888"  // used only if no password has ever been saved
#define WEBAPP_IDLE_OFF_MS    120000UL    // app switches off this long after the last phone left

#define MAX_HISTORY_MINUTES   1440        // 24 h, session only (RAM)
#define DEFAULT_TX_POWER_INDEX 8          // 18.5 dBm, same as the LS sender

#define TRIGGER_FLASH_MS      250         // OLED frame flashes this long on every signal / shutter release
#define FLASH_FRAME_PX        2
#define DISPLAY_REFRESH_MS    200         // OLED redraw rate, only when the text changed (a redraw blocks
                                          // ~25 ms; readings keep arriving in the UART buffer meanwhile)
// -------------------------------------------------

// ---------------- Persistent settings (EEPROM emulation in NVS) ----------------
#define SETTINGS_MAGIC 0x4C445231UL   // "LDR1" -- change to force a reset to defaults
struct PersistedSettings {
  uint32_t magic;
  uint16_t minCm;
  uint16_t maxCm;
  uint8_t  filter;
  uint8_t  txpower;
  uint16_t durations[LEGACY_DURATIONS];  // unused since 1.1 (kept so older saved settings stay valid)
  uint16_t extraCount;
  uint16_t extraGap;
  uint16_t triggerDelay;
  uint8_t  passwordSaved;               // 0 = never saved -> DEFAULT_AP_PASSWORD
  char     password[65];
  char     ssidCustom[33];              // empty = automatic MAC-based name
  uint8_t  rcvcnt;
  uint8_t  rcvmacs[MAX_PAIRED_RECEIVERS][6];
  uint16_t lunaRate;                    // added last so older saved settings stay valid
  uint16_t lunaMinAmp;
  uint16_t triggerDuration;             // 1.1: single trigger duration
  uint8_t  mode;                        // 1.1: MODE_MOVEMENT / MODE_BARRIER
  uint8_t  moveFilter;                  // 1.1: cm
  uint8_t  sndcnt;                      // 1.1: paired LS senders (receiver mode)
  uint8_t  sndmacs[MAX_PAIRED_SENDERS][6];
  uint16_t rcvActive;                   // 1.4: bit i = trigger i is sent to (old saves: 0xFFFF = all on)
  char     rcvNames[MAX_PAIRED_RECEIVERS][SENDER_NAME_MAX + 1];
  uint8_t  sndActive;                   // 1.4: bit i = sender i is listened to
  char     sndNames[MAX_PAIRED_SENDERS][SENDER_NAME_MAX + 1];
  char     wifiSsid[33];                // home WiFi for firmware updates
  char     wifiPass[65];                // stored only on the device
};
PersistedSettings settings;

void saveSettings() {
  EEPROM.put(0, settings);
  EEPROM.commit();
}

void loadSettings() {
  EEPROM.begin(sizeof(PersistedSettings));
  EEPROM.get(0, settings);
  if (settings.magic != SETTINGS_MAGIC) {
    memset(&settings, 0, sizeof(settings));
    settings.magic = SETTINGS_MAGIC;
    settings.minCm = DEFAULT_MIN_CM;
    settings.maxCm = DEFAULT_MAX_CM;
    settings.filter = DEFAULT_FILTER;
    settings.lunaRate = DEFAULT_LUNA_RATE;
    settings.lunaMinAmp = DEFAULT_LUNA_MIN_AMP;
    settings.txpower = DEFAULT_TX_POWER_INDEX;
    settings.triggerDuration = DEFAULT_TRIGGER_DURATION_MS;
    settings.mode = DEFAULT_MODE;
    settings.moveFilter = DEFAULT_MOVE_FILTER_CM;
    settings.extraCount = DEFAULT_EXTRA_SHOTS_COUNT;
    settings.extraGap = DEFAULT_EXTRA_GAP_MS;
    settings.triggerDelay = DEFAULT_TRIGGER_DELAY_MS;
    settings.rcvActive = 0xFFFF;
    settings.sndActive = 0xFF;
    strlcpy(settings.wifiSsid, DEFAULT_WIFI_SSID, sizeof(settings.wifiSsid));
    saveSettings();
  }
  settings.password[sizeof(settings.password) - 1] = 0;
  settings.ssidCustom[sizeof(settings.ssidCustom) - 1] = 0;
  settings.wifiSsid[sizeof(settings.wifiSsid) - 1] = 0;
  settings.wifiPass[sizeof(settings.wifiPass) - 1] = 0;
}

// ---------------- Live settings ----------------
uint16_t minCm = DEFAULT_MIN_CM, maxCm = DEFAULT_MAX_CM;
uint8_t filterCount = DEFAULT_FILTER;
const uint16_t LUNA_RATES[] = {100, 125, 166, 250};                      // 500/n Hz
const uint16_t LUNA_AMPS[]  = {30, 50, 100, 150, 200};   // = ISO 1600, 800, 400, 200, 100 in the web app (multiples of 10)
uint16_t lunaRate = DEFAULT_LUNA_RATE;
uint16_t lunaMinAmp = DEFAULT_LUNA_MIN_AMP;

uint8_t triggerMode = DEFAULT_MODE;
uint8_t moveFilterCm = DEFAULT_MOVE_FILTER_CM;
uint16_t triggerDurationMs = DEFAULT_TRIGGER_DURATION_MS;
uint16_t extraShotsCount = DEFAULT_EXTRA_SHOTS_COUNT;
uint16_t extraGapMs = DEFAULT_EXTRA_GAP_MS;
bool extraShotsEnabled = false;                                         // RAM only
uint16_t triggerDelayMs = DEFAULT_TRIGGER_DELAY_MS;

// Interval + Timer + Delayed Start: RAM only. Interval ON after power-up (normal mode:
// repeats only while something is inside the range); Timer and Delayed Start off.
bool intervalEnabled = true;
bool intervalForced = false;
unsigned long intervalMs = DEFAULT_INTERVAL_MS;
unsigned long lastIntervalTime = 0;
volatile uint32_t intervalSendCounter = 0;
bool timerActive = false;
unsigned long timerEndMillis = 0;
unsigned long timerPendingSeconds = 0;
bool startDelayActive = false;
unsigned long startDelayEndMillis = 0;

uint8_t txPowerIndex = DEFAULT_TX_POWER_INDEX;
const float txPowerValues[11] = {
  2.0f, 5.0f, 7.0f, 8.5f, 11.0f, 13.0f, 15.0f, 17.0f, 18.5f, 19.0f, 19.5f
};

// ---------------- TF-Luna state ----------------
uint16_t lunaCm = 0;
uint16_t lunaAmp = 0;
int16_t  lunaTempC = 0;
bool lunaValid = false;            // last reading trustworthy (enough signal)
bool sensorOk = false;             // frames are arriving
unsigned long lastFrameMs = 0;
uint32_t frameCount = 0;
uint32_t readingsPerSec = 0;

bool objectPresent = false;        // object in range and already triggered
uint8_t inRangeStreak = 0;         // Light-Barrier mode
unsigned long lastInRangeMs = 0;

// Movement mode: one "track" follows the distance of the object in the beam
bool trackActive = false;
uint16_t trackStartCm = 0;         // farthest (median) distance seen on this track
uint16_t trackLastRaw = 0;
unsigned long trackLastMs = 0;
uint16_t medBuf[3];
uint8_t medCount = 0;
bool moveArmed = true;
uint16_t moveTriggerCm = 0;

// ---------------- Radio / pairing ----------------
uint8_t broadcastAddress[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
uint8_t pairedReceiverMacs[MAX_PAIRED_RECEIVERS][6];
uint8_t pairedReceiverCount = 0;
uint16_t pairedReceiverActive = 0xFFFF;                         // bit i = trigger i is sent to
char pairedReceiverNames[MAX_PAIRED_RECEIVERS][SENDER_NAME_MAX + 1];
uint16_t txSeq = 0;
uint32_t signalsSent = 0;          // trigger + interval signals since power-on (OLED + web)

#define PAIR_ACK_QUEUE_LEN 4
uint8_t pendingAckMacs[PAIR_ACK_QUEUE_LEN][6];
volatile uint8_t pendingAckHead = 0, pendingAckTail = 0;

bool beaconActive = false;
unsigned long beaconStart = 0, lastBeacon = 0;
bool pairAcksAccepted();   // defined next to the beacon code below

// ----- receiver mode: paired LS senders -----
uint8_t pairedSenderMacs[MAX_PAIRED_SENDERS][6];
uint16_t pairedSenderLastSeq[MAX_PAIRED_SENDERS];
bool pairedSenderHaveSeq[MAX_PAIRED_SENDERS];
uint8_t pairedSenderCount = 0;
uint8_t pairedSenderActive = 0xFF;                              // bit i = sender i is listened to
char pairedSenderNames[MAX_PAIRED_SENDERS][SENDER_NAME_MAX + 1];
bool senderPairWindow = false;
unsigned long senderPairWindowEnd = 0;
uint8_t senderPairNewCount = 0;
uint8_t senderReAckedMask = 0;      // already-paired senders acked again in this window (bit = index)
uint32_t signalsReceived = 0;
volatile bool clearSendersRequested = false;

#define RX_QUEUE_LEN 8
struct RxEvent { uint8_t mac[6]; uint8_t type; uint16_t seq; char name[SENDER_NAME_MAX + 1]; };
RxEvent rxQueue[RX_QUEUE_LEN];
volatile uint8_t rxHead = 0, rxTail = 0;

// ---------------- Web app ----------------
char apSSID[33];
IPAddress apIP(8, 8, 8, 8);
IPAddress apSubnet(255, 255, 255, 0);
WebServer server(80);
DNSServer dnsServer;
String apPassword = "";
bool apCurrentlyProtected = false;
bool apConfigApplied = false;
bool apActive = true;
bool keepAppAlive = false;         // RAM only
unsigned long apLastActivity = 0;  // last time a phone was connected (or the app was started)
unsigned long lastApHousekeeping = 0;
volatile bool manualShutdownRequested = false;
volatile bool clearPairingsRequested = false;

// ---------------- Histogram (session only) ----------------
uint16_t minuteCountsContact[MAX_HISTORY_MINUTES] = {0};   // range triggers + TEST
uint16_t minuteCountsInterval[MAX_HISTORY_MINUTES] = {0};  // interval shots
uint16_t minuteCountsReceived[MAX_HISTORY_MINUTES] = {0};  // triggers received from paired LS senders
uint32_t sentSignalTotal = 0;
unsigned long histogramStartMillis = 0;

// ---------------- Firmware update from GitHub ----------------
enum UpdState { UPD_IDLE, UPD_JOINING, UPD_CHECKING, UPD_INSTALLING, UPD_UPTODATE, UPD_ERROR };
UpdState updState = UPD_IDLE;
String updRemoteVersion = "";
String updError = "";
int updProgress = 0;
volatile bool updateRequested = false;

// ---------------- Battery ----------------
uint32_t batPinMv = 0;
float batVolts = 0;
bool batHaveValue = false;
bool lowBattery = false;
unsigned long batEmptySinceMs = 0;    // 0 = not below BAT_EMPTY_V
unsigned long batSleepWarnStartMs = 0; // 0 = no deep-sleep warning running
unsigned long rgbFlashUntil = 0;
uint8_t batColorLevel = 255;          // 0 green, 1 yellow, 2 orange, 3 red, 4 red flashing; 255 = not set yet
unsigned long lastBatMs = 0, ledCycleStartMs = 0;
uint8_t factoryStaMac[6];

// ---------------- Local trigger output (receiver state machine) ----------------
enum TriggerPhase { PHASE_IDLE, PHASE_PULSE_ON, PHASE_GAP };
TriggerPhase triggerPhase = PHASE_IDLE;
unsigned long phaseStartTime = 0;
uint16_t activeDurationMs = 0;
uint16_t activeGapMs = 0;
uint8_t pulsesTotal = 0;
uint8_t pulsesFired = 0;
bool triggerPending = false;
unsigned long pendingTriggerFireTime = 0;
uint32_t shutterReleases = 0;

// ---------------- Display ----------------
U8G2_SSD1306_128X32_UNIVISION_F_HW_I2C u8g2(U8G2_R0, U8X8_PIN_NONE, OLED_SCL, OLED_SDA);
unsigned long lastDisplayDraw = 0;
bool displayDirty = true;
char lastDisplayKey[48] = "";
unsigned long flashUntil = 0;
// BOOT button feedback on the OLED
bool buttonHeld = false;
uint8_t buttonTier = 0;
unsigned long buttonMsgUntil = 0;
char buttonMsg1[20] = "", buttonMsg2[20] = "";
bool settingsDirty = false;       // live settings differ from the saved ones

// ---------------- Forward declarations ----------------
void applyAPConfiguration();
void applySSIDChangeNow();
void rebuildSSID();
void stopWebApp(bool manual);
void startWebApp();
void requestLocalTrigger();
void flashTriggerFrame();
void recordInto(uint16_t* counts);
void processMovement(uint16_t d, bool valid, unsigned long now);
void processBarrier(uint16_t dist, bool valid, unsigned long now);

// =====================================================================
// Paired receivers
// =====================================================================
bool macEquals(const uint8_t* a, const uint8_t* b) {
  for (uint8_t i = 0; i < 6; i++) if (a[i] != b[i]) return false;
  return true;
}

void addEspNowPeer(const uint8_t* mac) {
  if (esp_now_is_peer_exist(mac)) return;
  esp_now_peer_info_t peerInfo = {};
  memcpy(peerInfo.peer_addr, mac, 6);
  peerInfo.channel = WIFI_CHANNEL;
  peerInfo.ifidx = WIFI_IF_STA;
  peerInfo.encrypt = false;
  esp_now_add_peer(&peerInfo);
}

bool isPairedReceiver(const uint8_t* mac) {
  for (uint8_t i = 0; i < pairedReceiverCount; i++)
    if (macEquals(pairedReceiverMacs[i], mac)) return true;
  return false;
}

// Names saved in flash can be garbage on boards that saved with an older firmware
// (those bytes were never written): keep only plain printable text.
void sanitizeName(char* n) {
  n[SENDER_NAME_MAX] = 0;
  for (uint8_t i = 0; n[i]; i++) {
    if ((uint8_t)n[i] < 0x20 || (uint8_t)n[i] > 0x7E) { n[0] = 0; return; }
  }
}

// Copies the name out of a hello / info packet: [7] length, [8..] name
void packetName(const uint8_t* data, uint8_t len, char* out) {
  out[0] = 0;
  if (len <= PACKET_LEN) return;
  uint8_t n = data[PACKET_LEN];
  if (n > SENDER_NAME_MAX) n = SENDER_NAME_MAX;
  if (PACKET_LEN + 1 + n > len) n = len - PACKET_LEN - 1;
  for (uint8_t i = 0; i < n; i++) {
    char c = (char)data[PACKET_LEN + 1 + i];
    out[i] = (c == '"' || c == '\\') ? '_' : c;   // keep the JSON for the web app valid
  }
  out[n] = 0;
  sanitizeName(out);
}

// Removes bit idx from a mask and shifts the higher bits down (list compaction)
uint16_t maskRemoveBit(uint16_t m, uint8_t idx) {
  uint16_t low = m & (uint16_t)((1u << idx) - 1);
  uint16_t high = (uint16_t)(m >> (idx + 1)) << idx;
  return low | high;
}

int findPairedReceiver(const uint8_t* mac) {
  for (uint8_t i = 0; i < pairedReceiverCount; i++) if (macEquals(pairedReceiverMacs[i], mac)) return i;
  return -1;
}

bool receiverIsActive(uint8_t i) { return (pairedReceiverActive >> i) & 1; }

// Pairings are stored right away (not via "Save Settings").
void savePairedReceivers() {
  settings.rcvcnt = pairedReceiverCount;
  memset(settings.rcvmacs, 0, sizeof(settings.rcvmacs));
  memcpy(settings.rcvmacs, pairedReceiverMacs, pairedReceiverCount * 6);
  settings.rcvActive = pairedReceiverActive;
  memset(settings.rcvNames, 0, sizeof(settings.rcvNames));
  memcpy(settings.rcvNames, pairedReceiverNames, pairedReceiverCount * (SENDER_NAME_MAX + 1));
  saveSettings();
}

void addPairedReceiver(const uint8_t* mac) {
  if (isPairedReceiver(mac)) return;
  if (pairedReceiverCount >= MAX_PAIRED_RECEIVERS) return;
  uint8_t i = pairedReceiverCount;
  memcpy(pairedReceiverMacs[i], mac, 6);
  pairedReceiverNames[i][0] = 0;                 // filled in by its PAIR_INFO
  pairedReceiverActive |= (uint16_t)(1u << i);   // a newly paired trigger starts on
  addEspNowPeer(pairedReceiverMacs[i]);
  pairedReceiverCount++;
  savePairedReceivers();
  displayDirty = true;
}

void loadPairedReceivers() {
  uint8_t cnt = settings.rcvcnt;
  if (cnt > MAX_PAIRED_RECEIVERS) cnt = 0;
  pairedReceiverCount = cnt;
  pairedReceiverActive = settings.rcvActive;
  memset(pairedReceiverNames, 0, sizeof(pairedReceiverNames));
  if (cnt > 0) {
    memcpy(pairedReceiverMacs, settings.rcvmacs, cnt * 6);
    memcpy(pairedReceiverNames, settings.rcvNames, cnt * (SENDER_NAME_MAX + 1));
    for (uint8_t i = 0; i < cnt; i++) addEspNowPeer(pairedReceiverMacs[i]);
  }
  for (uint8_t i = 0; i < MAX_PAIRED_RECEIVERS; i++) sanitizeName(pairedReceiverNames[i]);
}

void setReceiverActive(uint8_t i, bool on) {
  if (on) pairedReceiverActive |= (uint16_t)(1u << i);
  else    pairedReceiverActive &= (uint16_t)~(1u << i);
  savePairedReceivers();
}

void forgetReceiver(uint8_t idx) {
  if (idx >= pairedReceiverCount) return;
  if (esp_now_is_peer_exist(pairedReceiverMacs[idx])) esp_now_del_peer(pairedReceiverMacs[idx]);
  uint8_t last = pairedReceiverCount - 1;
  for (uint8_t i = idx; i < last; i++) {
    memcpy(pairedReceiverMacs[i], pairedReceiverMacs[i + 1], 6);
    memcpy(pairedReceiverNames[i], pairedReceiverNames[i + 1], SENDER_NAME_MAX + 1);
  }
  memset(pairedReceiverNames[last], 0, SENDER_NAME_MAX + 1);
  pairedReceiverActive = maskRemoveBit(pairedReceiverActive, idx) | (uint16_t)(1u << last);
  pairedReceiverCount--;
  savePairedReceivers();
}

void setReceiverName(const uint8_t* mac, const char* name) {
  int i = findPairedReceiver(mac);
  if (i < 0 || !name[0] || strcmp(pairedReceiverNames[i], name) == 0) return;
  strncpy(pairedReceiverNames[i], name, SENDER_NAME_MAX);
  pairedReceiverNames[i][SENDER_NAME_MAX] = 0;
  savePairedReceivers();
}

void clearPairedReceivers() {
  for (uint8_t i = 0; i < pairedReceiverCount; i++) {
    if (esp_now_is_peer_exist(pairedReceiverMacs[i])) esp_now_del_peer(pairedReceiverMacs[i]);
  }
  pairedReceiverCount = 0;
  pairedReceiverActive = 0xFFFF;
  memset(pairedReceiverNames, 0, sizeof(pairedReceiverNames));
  savePairedReceivers();
}

// =====================================================================
// ESP-NOW
// =====================================================================
void onDataSent(const wifi_tx_info_t *txInfo, esp_now_send_status_t status) {
  // broadcast copies never get an ACK; unicast retries are done by the hardware
}

// Trigger and interval packets go out as one reliable (hardware-ACKed) unicast
// per paired trigger that is ON -- no broadcast copy since 1.4.0, so a trigger
// switched off or forgotten in Paired Devices really isn't fired. (Pairing works
// on the hello beacon, which is still broadcast.)
void sendPacket(uint8_t type) {
  uint8_t payload[PACKET_LEN] = {
    PRODUCT_SIGNATURE_0, PRODUCT_SIGNATURE_1, PRODUCT_SIGNATURE_2, PRODUCT_SIGNATURE_3,
    type, (uint8_t)(txSeq >> 8), (uint8_t)(txSeq & 0xFF)
  };
  txSeq++;
  if (type == PACKET_TYPE_TRIGGER || type == PACKET_TYPE_INTERVAL) {
    for (uint8_t i = 0; i < pairedReceiverCount; i++) {
      if (!receiverIsActive(i)) continue;
      esp_now_send(pairedReceiverMacs[i], payload, PACKET_LEN);
      yield();
    }
  } else {
    esp_now_send(broadcastAddress, payload, PACKET_LEN);
    yield();
  }
}

// Runs in the WiFi callback: only queue things, the work happens in loop().
//   PAIR_ACK          <- a receiver paired with us (we are its sender)
//   TRIGGER/INTERVAL  <- from a paired LS sender (we are its receiver)
//   anything          <- during the Listen-for-Sender window: a new sender to pair
void onDataRecv(const esp_now_recv_info_t *recvInfo, const uint8_t *data, int len) {
  const uint8_t* mac = recvInfo->src_addr;
  if (len < PACKET_LEN || len > 255) return;
  if (data[0] != PRODUCT_SIGNATURE_0 || data[1] != PRODUCT_SIGNATURE_1 ||
      data[2] != PRODUCT_SIGNATURE_2 || data[3] != PRODUCT_SIGNATURE_3) return;
  uint8_t type = data[4];
  if (type != PACKET_TYPE_BEACON && type != PACKET_TYPE_PAIR_INFO && len != PACKET_LEN) return;   // only hello / info are longer
  if (type == PACKET_TYPE_PAIR_ACK) {
    if (!pairAcksAccepted()) return;   // only pairable while the beacon runs (10 s)
    uint8_t next = (pendingAckHead + 1) % PAIR_ACK_QUEUE_LEN;
    if (next == pendingAckTail) return;
    memcpy(pendingAckMacs[pendingAckHead], mac, 6);
    pendingAckHead = next;
    return;
  }
  if (type != PACKET_TYPE_TRIGGER && type != PACKET_TYPE_INTERVAL &&
      type != PACKET_TYPE_BEACON && type != PACKET_TYPE_PAIR_INFO) return;
  uint8_t next = (rxHead + 1) % RX_QUEUE_LEN;
  if (next == rxTail) return;
  memcpy(rxQueue[rxHead].mac, mac, 6);
  rxQueue[rxHead].type = type;
  rxQueue[rxHead].seq = ((uint16_t)data[5] << 8) | data[6];
  packetName(data, (uint8_t)len, rxQueue[rxHead].name);
  rxHead = next;
}

void processPendingPairAcks() {
  while (pendingAckTail != pendingAckHead) {
    addPairedReceiver(pendingAckMacs[pendingAckTail]);
    pendingAckTail = (pendingAckTail + 1) % PAIR_ACK_QUEUE_LEN;
  }
}

// ---------------- Receiver mode: paired LS senders ----------------
int findPairedSender(const uint8_t* mac) {
  for (uint8_t i = 0; i < pairedSenderCount; i++) if (macEquals(pairedSenderMacs[i], mac)) return i;
  return -1;
}

bool senderIsActive(uint8_t i) { return (pairedSenderActive >> i) & 1; }

void savePairedSenders() {
  settings.sndcnt = pairedSenderCount;
  memset(settings.sndmacs, 0, sizeof(settings.sndmacs));
  memcpy(settings.sndmacs, pairedSenderMacs, pairedSenderCount * 6);
  settings.sndActive = pairedSenderActive;
  memset(settings.sndNames, 0, sizeof(settings.sndNames));
  memcpy(settings.sndNames, pairedSenderNames, pairedSenderCount * (SENDER_NAME_MAX + 1));
  saveSettings();
}

void setSenderActive(uint8_t i, bool on) {
  if (on) pairedSenderActive |= (uint8_t)(1u << i);
  else    pairedSenderActive &= (uint8_t)~(1u << i);
  savePairedSenders();
}

void forgetSender(uint8_t idx) {
  if (idx >= pairedSenderCount) return;
  uint8_t last = pairedSenderCount - 1;
  for (uint8_t i = idx; i < last; i++) {
    memcpy(pairedSenderMacs[i], pairedSenderMacs[i + 1], 6);
    memcpy(pairedSenderNames[i], pairedSenderNames[i + 1], SENDER_NAME_MAX + 1);
    pairedSenderLastSeq[i] = pairedSenderLastSeq[i + 1];
    pairedSenderHaveSeq[i] = pairedSenderHaveSeq[i + 1];
  }
  memset(pairedSenderNames[last], 0, SENDER_NAME_MAX + 1);
  pairedSenderHaveSeq[last] = false;
  pairedSenderActive = (uint8_t)(maskRemoveBit(pairedSenderActive, idx) | (1u << last));
  pairedSenderCount--;
  savePairedSenders();
}

void loadPairedSenders() {
  uint8_t cnt = settings.sndcnt;
  if (cnt > MAX_PAIRED_SENDERS) cnt = 0;
  pairedSenderCount = cnt;
  pairedSenderActive = settings.sndActive;
  memset(pairedSenderNames, 0, sizeof(pairedSenderNames));
  if (cnt > 0) {
    memcpy(pairedSenderMacs, settings.sndmacs, cnt * 6);
    memcpy(pairedSenderNames, settings.sndNames, cnt * (SENDER_NAME_MAX + 1));
  }
  for (uint8_t i = 0; i < MAX_PAIRED_SENDERS; i++) {
    pairedSenderHaveSeq[i] = false;
    sanitizeName(pairedSenderNames[i]);
  }
}

void clearPairedSenders() {
  pairedSenderCount = 0;
  pairedSenderActive = 0xFF;
  memset(pairedSenderNames, 0, sizeof(pairedSenderNames));
  for (uint8_t i = 0; i < MAX_PAIRED_SENDERS; i++) pairedSenderHaveSeq[i] = false;
  savePairedSenders();
}

// Tells the sender "I'm your receiver now", exactly like an Octocam receiver does.
void sendPairAck(uint8_t* mac) {
  addEspNowPeer(mac);
  uint8_t payload[PACKET_LEN] = {
    PRODUCT_SIGNATURE_0, PRODUCT_SIGNATURE_1, PRODUCT_SIGNATURE_2, PRODUCT_SIGNATURE_3,
    PACKET_TYPE_PAIR_ACK, 0, 0
  };
  for (uint8_t i = 0; i < PAIR_ACK_COPIES; i++) {
    esp_now_send(mac, payload, PACKET_LEN);
    if (i < PAIR_ACK_COPIES - 1) delay(PAIR_ACK_GAP_MS);
  }
  // then our name, so an Octocam BT can list this device by name (others ignore it)
  uint8_t nameLen = (uint8_t)strnlen(apSSID, SENDER_NAME_MAX);
  uint8_t info[PACKET_LEN + 1 + SENDER_NAME_MAX] = {
    PRODUCT_SIGNATURE_0, PRODUCT_SIGNATURE_1, PRODUCT_SIGNATURE_2, PRODUCT_SIGNATURE_3,
    PACKET_TYPE_PAIR_INFO, 0, 0
  };
  info[PACKET_LEN] = nameLen;
  memcpy(info + PACKET_LEN + 1, apSSID, nameLen);
  delay(PAIR_ACK_GAP_MS);
  esp_now_send(mac, info, PACKET_LEN + 1 + nameLen);
}

void openSenderPairWindow() {
  senderPairWindow = true;
  senderPairWindowEnd = millis() + SENDER_PAIR_WINDOW_MS;
  senderPairNewCount = 0;
  senderReAckedMask = 0;
  displayDirty = true;
}

void processRxEvents() {
  processPendingPairAcks();                      // a trigger's acks first, so its PAIR_INFO below finds it
  while (rxTail != rxHead) {
    RxEvent ev = rxQueue[rxTail];
    rxTail = (rxTail + 1) % RX_QUEUE_LEN;
    if (ev.type == PACKET_TYPE_PAIR_INFO) {      // a trigger telling us its name
      setReceiverName(ev.mac, ev.name);
      continue;
    }
    int idx = findPairedSender(ev.mac);

    if (senderPairWindow && idx < 0) {           // a new sender heard during the window
      if (ev.type != PACKET_TYPE_BEACON) continue; // only its pairing hello pairs, not running traffic
      if (pairedSenderCount < MAX_PAIRED_SENDERS) {
        memcpy(pairedSenderMacs[pairedSenderCount], ev.mac, 6);
        strncpy(pairedSenderNames[pairedSenderCount], ev.name, SENDER_NAME_MAX);
        pairedSenderNames[pairedSenderCount][SENDER_NAME_MAX] = 0;
        pairedSenderHaveSeq[pairedSenderCount] = false;
        pairedSenderActive |= (uint8_t)(1u << pairedSenderCount);   // a newly paired sender starts on
        pairedSenderCount++;
        senderPairNewCount++;
        savePairedSenders();
        sendPairAck(ev.mac);
      }
      continue;                                  // pairing traffic never fires the camera
    }
    if (idx < 0) continue;                       // not one of our senders
    if (senderPairWindow && ev.type == PACKET_TYPE_BEACON && !((senderReAckedMask >> idx) & 1)) {
      senderReAckedMask |= (uint8_t)(1u << idx); // already paired: ack again once, in case the
      sendPairAck(ev.mac);                       // sender lost us from its own list
    }
    if (ev.type == PACKET_TYPE_BEACON) {         // a paired sender's hello: keep its name current
      if (ev.name[0] && strcmp(pairedSenderNames[idx], ev.name) != 0) {
        strncpy(pairedSenderNames[idx], ev.name, SENDER_NAME_MAX);
        pairedSenderNames[idx][SENDER_NAME_MAX] = 0;
        savePairedSenders();
      }
      continue;
    }
    if (ev.type != PACKET_TYPE_TRIGGER && ev.type != PACKET_TYPE_INTERVAL) continue;
    if (!senderIsActive((uint8_t)idx)) continue; // switched off in Paired Devices
    // the sender sends each event as broadcast + unicast: fire only once per sequence number
    if (pairedSenderHaveSeq[idx] && pairedSenderLastSeq[idx] == ev.seq) continue;
    pairedSenderLastSeq[idx] = ev.seq;
    pairedSenderHaveSeq[idx] = true;

    requestLocalTrigger();                       // local camera only -- nothing is sent on
    flashTriggerFrame();
    signalsReceived++;
    recordInto(minuteCountsReceived);
  }
  if (senderPairWindow && (long)(millis() - senderPairWindowEnd) >= 0) {
    senderPairWindow = false;
    displayDirty = true;
  }
}

// The pairing hello: [header(7)] [name length] [SSID]. Broadcast only.
void sendBeacon() {
  uint8_t nameLen = (uint8_t)strnlen(apSSID, SENDER_NAME_MAX);
  uint8_t payload[PACKET_LEN + 1 + SENDER_NAME_MAX] = {
    PRODUCT_SIGNATURE_0, PRODUCT_SIGNATURE_1, PRODUCT_SIGNATURE_2, PRODUCT_SIGNATURE_3,
    PACKET_TYPE_BEACON, 0, 0
  };
  payload[PACKET_LEN] = nameLen;
  memcpy(payload + PACKET_LEN + 1, apSSID, nameLen);
  esp_now_send(broadcastAddress, payload, PACKET_LEN + 1 + nameLen);
  yield();
}

// Receivers' PAIR_ACKs only count while the beacon runs (+ a moment for acks in flight).
bool pairAcksAccepted() {
  return beaconActive || (millis() - beaconStart) < (BEACON_DURATION_MS + PAIR_ACK_GRACE_MS);
}

void startBeaconBurst() {
  beaconActive = true;
  beaconStart = millis();
  sendBeacon();
  lastBeacon = millis();
}

void updateBeacon() {
  if (!beaconActive) return;
  unsigned long now = millis();
  if (now - beaconStart >= BEACON_DURATION_MS) { beaconActive = false; return; }
  if (now - lastBeacon >= BEACON_INTERVAL_MS) {
    lastBeacon = now;
    sendBeacon();
  }
}

void applyTxPower() {
  esp_wifi_set_max_tx_power((int8_t)(txPowerValues[txPowerIndex] * 4));   // quarter-dBm units
}

// The station interface carries ESP-NOW and never joins a network (except during an update).
// b/g/n + Long Range: hears ESP32 senders set to LR and ESP8266 ones (11b) alike.
void initEspNow() {
  esp_wifi_set_channel(WIFI_CHANNEL, WIFI_SECOND_CHAN_NONE);
  esp_wifi_set_protocol(WIFI_IF_STA, WIFI_PROTOCOL_11B | WIFI_PROTOCOL_11G | WIFI_PROTOCOL_11N | WIFI_PROTOCOL_LR);
  if (esp_now_init() != ESP_OK) return;
  applyTxPower();
  esp_now_register_send_cb(onDataSent);
  esp_now_register_recv_cb(onDataRecv);
  addEspNowPeer(broadcastAddress);
  loadPairedReceivers();
  loadPairedSenders();
}

// =====================================================================
// Histogram (session only)
// =====================================================================
void recordInto(uint16_t* counts) {
  unsigned long idx = (millis() - histogramStartMillis) / 60000UL;
  if (idx >= MAX_HISTORY_MINUTES) idx = MAX_HISTORY_MINUTES - 1;
  if (counts[idx] < 0xFFFF) counts[idx]++;
  sentSignalTotal++;
}

void resetHistory() {
  memset(minuteCountsContact, 0, sizeof(minuteCountsContact));
  memset(minuteCountsInterval, 0, sizeof(minuteCountsInterval));
  memset(minuteCountsReceived, 0, sizeof(minuteCountsReceived));
  histogramStartMillis = millis();
  sentSignalTotal = 0;
}

// =====================================================================
// One signal = local camera + all paired receivers
// =====================================================================
void fireSignal(uint8_t type) {
  sendPacket(type);                    // radio first: it's what the remote cameras wait for
  requestLocalTrigger();
  signalsSent++;
  if (type == PACKET_TYPE_INTERVAL) {
    recordInto(minuteCountsInterval);
    intervalSendCounter++;
  } else {
    recordInto(minuteCountsContact);
  }
  flashTriggerFrame();
  rgbFlashUntil = millis() + RGB_FLASH_MS;
}

// =====================================================================
// Local camera output -- same behaviour as the Octocam receiver
// =====================================================================
void setOpto(bool on) {
  digitalWrite(OPTO_PIN, (OPTO_ACTIVE_HIGH ? on : !on) ? HIGH : LOW);
}

void beginPulse() {
  setOpto(true);
  shutterReleases++;
  flashTriggerFrame();                  // every camera release, extra triggers included
}
void endPulse() {
  setOpto(false);
}

uint16_t currentPulseDurationMs() {
  return activeDurationMs;
}

void startTrigger() {
  activeGapMs = extraGapMs;
  pulsesFired = 0;
  activeDurationMs = triggerDurationMs;
  uint16_t extra = extraShotsEnabled ? extraShotsCount : 0;
  pulsesTotal = (uint8_t)(1 + (extra > 200 ? 200 : extra));
  triggerPhase = PHASE_PULSE_ON;
  phaseStartTime = millis();
  beginPulse();
}

void updateTrigger() {
  if (triggerPhase == PHASE_IDLE) return;
  unsigned long elapsed = millis() - phaseStartTime;
  if (triggerPhase == PHASE_PULSE_ON) {
    if (elapsed >= currentPulseDurationMs()) {
      endPulse();
      pulsesFired++;
      if (pulsesFired >= pulsesTotal) {
        triggerPhase = PHASE_IDLE;
      } else {
        triggerPhase = PHASE_GAP;
        phaseStartTime = millis();
      }
    }
  } else if (elapsed >= activeGapMs) {
    triggerPhase = PHASE_PULSE_ON;
    phaseStartTime = millis();
    beginPulse();
  }
}

// A signal for the local camera. Fires now, or after the trigger delay.
// A signal arriving while the camera is still mid-sequence is skipped,
// exactly like on a receiver.
void requestLocalTrigger() {
  if (triggerDelayMs == 0) {
    if (triggerPhase == PHASE_IDLE) startTrigger();
  } else {
    triggerPending = true;
    pendingTriggerFireTime = millis() + triggerDelayMs;
  }
}

void updatePendingTrigger() {
  if (triggerPending && (long)(millis() - pendingTriggerFireTime) >= 0) {
    triggerPending = false;
    if (triggerPhase == PHASE_IDLE) startTrigger();
  }
}

// =====================================================================
// TF-Luna (UART, 9-byte frames: 59 59 DistL DistH AmpL AmpH TempL TempH Sum)
// =====================================================================
void processReading(uint16_t dist, uint16_t amp, uint16_t rawTemp) {
  unsigned long now = millis();
  lunaCm = dist;
  lunaAmp = amp;
  lunaTempC = (int16_t)(rawTemp / 8) - 256;
  lunaValid = dist > 0 && amp >= lunaMinAmp && amp != 0xFFFF;
  lastFrameMs = now;
  frameCount++;
  sensorOk = true;

  if (triggerMode == MODE_MOVEMENT) processMovement(dist, lunaValid, now);
  else processBarrier(dist, lunaValid, now);
}

// LIGHT-BARRIER: anything inside the range for filterCount readings in a row
void processBarrier(uint16_t dist, bool valid, unsigned long now) {
  bool inRange = valid && dist >= minCm && dist <= maxCm;
  if (inRange) {
    if (inRangeStreak < 255) inRangeStreak++;
    lastInRangeMs = now;
  } else {
    inRangeStreak = 0;
  }
  if (!objectPresent && inRangeStreak >= filterCount) {
    objectPresent = true;
    lastIntervalTime = now;             // interval repeats count from this trigger
    fireSignal(PACKET_TYPE_TRIGGER);
  }
}

// MOVEMENT: follow the distance; trigger when the object is in the range and
// has come at least moveFilterCm closer than the farthest point of its track.
void endTrack() {
  trackActive = false;
  medCount = 0;
  if (!moveArmed) {                     // the object we triggered on is gone
    moveArmed = true;
    objectPresent = false;
    displayDirty = true;
  }
}

uint16_t median3(uint16_t a, uint16_t b, uint16_t c) {
  if (a > b) { uint16_t t = a; a = b; b = t; }
  if (b > c) { b = c; }
  return a > b ? a : b;
}

void processMovement(uint16_t d, bool valid, unsigned long now) {
  if (!valid) {
    if (trackActive && now - trackLastMs > MOVE_TRACK_GAP_MS) endTrack();
    return;
  }
  if (trackActive) {
    unsigned long dt = now - trackLastMs;
    long jump = labs((long)d - (long)trackLastRaw);
    long maxJump = (long)(MOVE_MAX_SPEED_CM_PER_MS * dt) + MOVE_JUMP_MARGIN_CM;
    if (dt > MOVE_TRACK_GAP_MS || jump > maxJump) endTrack();   // gap, or a different object
  }

  // 3-reading median: a single noisy reading can't fake a movement
  medBuf[medCount % 3] = d;
  medCount++;
  uint16_t m = medCount >= 3 ? median3(medBuf[0], medBuf[1], medBuf[2]) : d;

  if (!trackActive) {
    trackActive = true;
    trackStartCm = m;
  }
  if (m > trackStartCm) trackStartCm = m;  // farthest point = where the approach started
  trackLastRaw = d;
  trackLastMs = now;

  bool inRange = m >= minCm && m <= maxCm;
  if (inRange) lastInRangeMs = now;

  if (moveArmed && inRange && trackStartCm >= m + moveFilterCm) {
    moveArmed = false;
    objectPresent = true;
    moveTriggerCm = m;
    lastIntervalTime = now;
    fireSignal(PACKET_TYPE_TRIGGER);
  } else if (!moveArmed && m >= moveTriggerCm + moveFilterCm) {
    moveArmed = true;                   // moved away again: a new approach may trigger
    objectPresent = false;
    trackStartCm = m;
    displayDirty = true;
  }
}

void resetDetection() {
  objectPresent = false;
  inRangeStreak = 0;
  moveArmed = true;
  trackActive = false;
  medCount = 0;
  displayDirty = true;
}

// ---------------- Mode + range rules ----------------
uint16_t minSpanCm() {
  return triggerMode == MODE_MOVEMENT ? MOVE_MIN_SPAN_CM : MIN_SPAN_CM;
}

// Makes min/max legal for the current mode. keepMin: min stays, max moves (if it can).
void enforceSpan(bool keepMin) {
  uint16_t span = minSpanCm();
  if (minCm < MIN_LIMIT_CM) minCm = MIN_LIMIT_CM;
  if (maxCm > LUNA_RANGE_CM) maxCm = LUNA_RANGE_CM;
  if (maxCm >= minCm + span) return;
  if (keepMin) {
    maxCm = minCm + span;
    if (maxCm > LUNA_RANGE_CM) { maxCm = LUNA_RANGE_CM; minCm = maxCm - span; }
  } else {
    minCm = maxCm >= MIN_LIMIT_CM + span ? maxCm - span : MIN_LIMIT_CM;
    if (maxCm < minCm + span) maxCm = minCm + span;
  }
}

void setTriggerMode(uint8_t m) {
  triggerMode = (m == MODE_BARRIER) ? MODE_BARRIER : MODE_MOVEMENT;
  enforceSpan(true);
  settingsDirty = true;                 // stored only by "Save Settings"
  resetDetection();
}

// ---------------- TF-Luna sensitivity (sent over UART2 TX on IO26) ----------------
uint16_t snapTo(const uint16_t* list, uint8_t n, long v) {
  uint16_t best = list[0];
  for (uint8_t i = 0; i < n; i++) if (labs((long)list[i] - v) < labs((long)best - v)) best = list[i];
  return best;
}

void sendLunaCmd(uint8_t* cmd, uint8_t len) {
  uint8_t sum = 0;
  for (uint8_t i = 0; i < len - 1; i++) sum += cmd[i];
  cmd[len - 1] = sum;
  Serial2.write(cmd, len);
}

// Frame rate + amp threshold, applied immediately, not saved in the sensor's flash
// (the sketch re-sends them after every power-up). Below the threshold the TF-Luna
// outputs distance 0, which this sketch treats as "no reading".
void sendLunaConfig() {
  uint8_t rate[6] = {0x5A, 0x06, 0x03, (uint8_t)(lunaRate & 0xFF), (uint8_t)(lunaRate >> 8), 0};
  sendLunaCmd(rate, sizeof(rate));
  delay(5);
  uint8_t amp[7] = {0x5A, 0x07, 0x22, (uint8_t)(lunaMinAmp / 10), 0x00, 0x00, 0};
  sendLunaCmd(amp, sizeof(amp));
}

bool lunaConfigPending = false, lunaVerifyPending = false;
unsigned long lunaConfigAt = 0, lunaVerifyAt = 0;
uint8_t lunaConfigRetries = 0;

void scheduleLunaConfig(unsigned long inMs) {
  lunaConfigPending = true;
  lunaConfigAt = millis() + inMs;
  lunaConfigRetries = 0;
}

// Sends the settings when due, then checks 2.5 s later that the measured rate
// matches -- if not (sensor was still booting, a byte got lost), sends again, max 3x.
void updateLunaConfig() {
  unsigned long now = millis();
  if (lunaConfigPending && (long)(now - lunaConfigAt) >= 0) {
    lunaConfigPending = false;
    sendLunaConfig();
    lunaVerifyPending = true;
    lunaVerifyAt = now + 2500;
  }
  if (lunaVerifyPending && (long)(now - lunaVerifyAt) >= 0) {
    lunaVerifyPending = false;
    bool rateOk = readingsPerSec >= (uint32_t)lunaRate * 8 / 10 && readingsPerSec <= (uint32_t)lunaRate * 12 / 10;
    if (sensorOk && !rateOk && lunaConfigRetries < 3) {
      lunaConfigRetries++;
      lunaConfigPending = true;
      lunaConfigAt = now;
    }
  }
}

void readLuna() {
  static uint8_t buf[9];
  static uint8_t idx = 0;
  while (Serial2.available()) {
    uint8_t b = Serial2.read();
    if (idx < 2) {
      if (b == 0x59) buf[idx++] = b;
      else idx = 0;
      continue;
    }
    buf[idx++] = b;
    if (idx < 9) continue;
    idx = 0;
    uint8_t sum = 0;
    for (uint8_t i = 0; i < 8; i++) sum += buf[i];
    if (sum != buf[8]) continue;        // corrupt frame -- resync on the next 59 59
    processReading(buf[2] | (buf[3] << 8), buf[4] | (buf[5] << 8), buf[6] | (buf[7] << 8));
  }
}

void updatePresence() {
  unsigned long now = millis();
  if (sensorOk && now - lastFrameMs > LUNA_TIMEOUT_MS) {
    sensorOk = false;
    lunaValid = false;
    inRangeStreak = 0;
    displayDirty = true;
  }
  if (objectPresent && now - lastInRangeMs >= REARM_MS) {
    objectPresent = false;              // gone long enough -> the next entry triggers again
    moveArmed = true;
    displayDirty = true;
  }

  static unsigned long secStart = 0;
  static uint32_t secFrames = 0;
  if (now - secStart >= 1000) {
    readingsPerSec = frameCount - secFrames;
    secFrames = frameCount;
    secStart = now;
  }
}

// =====================================================================
// Interval + Delayed Start + Timer
// =====================================================================
void checkInterval() {
  if (!intervalEnabled) return;
  if (!objectPresent && !intervalForced) return;   // normal mode: only while an object is in range
  if (millis() - lastIntervalTime >= intervalMs) {
    lastIntervalTime = millis();
    fireSignal(PACKET_TYPE_INTERVAL);
  }
}

bool timerArmed() {
  return timerActive || (startDelayActive && timerPendingSeconds > 0);
}

long timerRemainingSecondsSigned() {
  if (timerActive) {
    long left = (long)(timerEndMillis - millis()) / 1000L;
    return left < 0 ? 0 : left;
  }
  if (startDelayActive && timerPendingSeconds > 0) return (long)timerPendingSeconds;
  return -1;
}

long startDelayRemainingSecondsSigned() {
  if (!startDelayActive) return -1;
  long left = (long)(startDelayEndMillis - millis()) / 1000L;
  return left < 0 ? 0 : left;
}

void setTimerMinutes(unsigned long minutes) {
  if (minutes > MAX_TIMER_MINUTES) minutes = MAX_TIMER_MINUTES;
  if (startDelayActive) {
    timerPendingSeconds = minutes * 60UL;   // starts once the delayed start hits 0
    timerActive = false;
    return;
  }
  if (minutes == 0) {
    timerActive = false;
  } else {
    timerEndMillis = millis() + minutes * 60000UL;
    timerActive = true;
  }
}

void finishStartDelay() {
  startDelayActive = false;
  intervalEnabled = true;
  lastIntervalTime = millis();
  if (timerPendingSeconds > 0) {
    timerEndMillis = millis() + timerPendingSeconds * 1000UL;
    timerActive = true;
  }
  timerPendingSeconds = 0;
}

void setStartDelayMinutes(unsigned long minutes) {
  if (minutes > MAX_TIMER_MINUTES) minutes = MAX_TIMER_MINUTES;
  if (minutes == 0) {
    if (startDelayActive) finishStartDelay();
    return;
  }
  if (!startDelayActive) {
    if (timerActive) {
      long remSec = timerRemainingSecondsSigned();
      timerPendingSeconds = remSec > 0 ? (unsigned long)remSec : 0;
      timerActive = false;
    } else {
      timerPendingSeconds = 0;
    }
    startDelayActive = true;
    intervalEnabled = false;
  }
  startDelayEndMillis = millis() + minutes * 60000UL;
}

void checkIntervalTimers() {
  if (startDelayActive && (long)(millis() - startDelayEndMillis) >= 0) finishStartDelay();
  if (timerActive && (long)(millis() - timerEndMillis) >= 0) {
    timerActive = false;
    intervalEnabled = false;
  }
}

// =====================================================================
// Battery (IO35, 100k/100k) + low-battery LED (IO16)
// =====================================================================
// Resting Li-ion voltage -> charge %, in 10 % steps. 3.10 V counts as empty (safety margin above
// the usual 3.0 V), so 0-20 % is stretched over 3.10-3.25 V. Under load it reads a bit low,
// on the charger a bit high.
const float LIION_V[] = { 3.10f, 3.18f, 3.25f, 3.45f, 3.58f, 3.67f, 3.72f, 3.80f, 3.90f, 4.05f, 4.20f };
const uint8_t LIION_N = sizeof(LIION_V) / sizeof(LIION_V[0]);

int batteryPercent() {
  float v = batVolts;
  if (v <= LIION_V[0]) return 0;
  if (v >= LIION_V[LIION_N - 1]) return 100;
  for (uint8_t i = 1; i < LIION_N; i++) {
    if (v < LIION_V[i]) {
      float f = (v - LIION_V[i - 1]) / (LIION_V[i] - LIION_V[i - 1]);
      return (int)((i - 1) * 10 + f * 10 + 0.5f);
    }
  }
  return 100;
}

// "ok", "none" (no cell / no divider) or "usb" (charger output without a cell)
const char* batteryState() {
  if (batVolts < BAT_NONE_BELOW_V) return "none";
  if (batVolts > BAT_USB_ABOVE_V) return "usb";
  return "ok";
}
bool batteryOk() { return batVolts >= BAT_NONE_BELOW_V && batVolts <= BAT_USB_ABOVE_V; }

void measureBattery() {
  uint32_t sum = 0;
  for (uint8_t i = 0; i < BAT_SAMPLES; i++) sum += analogReadMilliVolts(BAT_PIN);
  batPinMv = sum / BAT_SAMPLES;
  float v = batPinMv / 1000.0f * BAT_DIVIDER;
  if (!batHaveValue) { batVolts = v; batHaveValue = true; }
  else batVolts += BAT_EMA_ALPHA * (v - batVolts);
}

void ledWrite(bool on) {
  digitalWrite(LED_PIN, (on != LED_ACTIVE_LOW) ? HIGH : LOW);
}

void updateBattery() {
  unsigned long now = millis();
  if (now - lastBatMs >= BAT_MEASURE_MS) {
    lastBatMs = now;
    int before = batteryPercent();
    measureBattery();
    if (batteryPercent() != before) displayDirty = true;
  }
  int pct = batteryPercent();
  if (!batteryOk()) lowBattery = false;
  else if (pct <= LOW_BAT_PCT) lowBattery = true;
  else if (pct >= LOW_BAT_CLEAR_PCT) lowBattery = false;
  if (!lowBattery) { ledWrite(false); return; }
  unsigned long period = pct <= CRIT_BAT_PCT ? 1000UL : 4000UL;
  if (now - ledCycleStartMs >= period) ledCycleStartMs = now;
  ledWrite(now - ledCycleStartMs < LED_FLASH_MS);
}

// ---------------- empty cell -> deep sleep ----------------
void rgbWrite(uint8_t red, uint8_t green, uint8_t blue);

void enterBatterySleep(bool displayStarted) {
  setOpto(false);
  rgbWrite(0, 0, 0);
  ledWrite(false);
  if (displayStarted) u8g2.setPowerSave(1);
  esp_now_deinit();
  WiFi.mode(WIFI_OFF);
  esp_sleep_enable_timer_wakeup((uint64_t)BAT_SLEEP_CHECK_S * 1000000ULL);
  esp_deep_sleep_start();                     // wakes as a fresh boot; setup() decides again
}

// Called at power-on / wake-up, before WiFi: a cell that is still empty goes straight back to sleep.
void checkBatteryAtBoot() {
  bool timerWake = esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_TIMER;
  bool ok = batVolts >= BAT_NONE_BELOW_V && batVolts <= BAT_USB_ABOVE_V;
  if (!ok) return;                            // no reading or on USB: never sleep
  if (timerWake && batVolts < BAT_WAKE_V) enterBatterySleep(false);   // silently, no blink every minute
  if (!timerWake && batVolts < BAT_EMPTY_V) {
    unsigned long start = millis();
    while (millis() - start < BAT_SLEEP_WARN_MS) {
      rgbWrite(((millis() - start) / BAT_SLEEP_BLINK_MS) % 2 == 0 ? 255 : 0, 0, 0);
      delay(5);
    }
    enterBatterySleep(false);
  }
}

// Running: below BAT_EMPTY_V for BAT_EMPTY_HOLD_MS -> warning blink (updateRgb) -> deep sleep.
void checkBatteryEmpty() {
  unsigned long now = millis();
  if (!batteryOk() || batVolts >= BAT_EMPTY_V) {
    batEmptySinceMs = 0;
    batSleepWarnStartMs = 0;                  // cell recovered (or charger): warning off
    return;
  }
  if (batEmptySinceMs == 0) batEmptySinceMs = now;
  if (batSleepWarnStartMs == 0) {
    if (now - batEmptySinceMs >= BAT_EMPTY_HOLD_MS) batSleepWarnStartMs = now;
    return;
  }
  if (now - batSleepWarnStartMs >= BAT_SLEEP_WARN_MS) enterBatterySleep(true);
}

// ---------------- RGB status LED ----------------
// neopixelWrite takes (red, green, blue). If red and green come out swapped, swap them here.
void rgbWrite(uint8_t red, uint8_t green, uint8_t blue) {
  static uint32_t last = 0xFFFFFFFF;
  uint32_t c = ((uint32_t)red << 16) | ((uint32_t)green << 8) | blue;
  if (c == last) return;               // only write on a change
  last = c;
  neopixelWrite(RGB_PIN, red, green, blue);
}

// Battery % -> colour level, with a little hysteresis so the colour doesn't flicker at a threshold.
uint8_t levelForPct(int pct, int margin) {
  if (pct > BAT_GREEN_PCT + margin) return 0;
  if (pct > BAT_YELLOW_PCT + margin) return 1;
  if (pct > BAT_ORANGE_PCT + margin) return 2;
  if (pct >= CRIT_BAT_PCT + margin) return 3;
  return 4;
}
void updateBatColorLevel() {
  int pct = batteryPercent();
  uint8_t up = levelForPct(pct, BAT_COLOR_HYST);    // must be clearly above a threshold to get better
  uint8_t down = levelForPct(pct, -BAT_COLOR_HYST); // and clearly below one to get worse
  if (batColorLevel == 255) batColorLevel = levelForPct(pct, 0);
  else if (up < batColorLevel) batColorLevel = up;
  else if (down > batColorLevel) batColorLevel = down;
}

// The only place that sets the RGB LED (besides checkBatteryAtBoot). Priority: deep-sleep warning,
// white signal flash, blue pairing blink, battery colour.
void updateRgb() {
  unsigned long now = millis();
  if (batSleepWarnStartMs) {                  // going to deep sleep: very fast red blink, full brightness
    rgbWrite(((now - batSleepWarnStartMs) / BAT_SLEEP_BLINK_MS) % 2 == 0 ? 255 : 0, 0, 0);
    return;
  }
  if ((long)(rgbFlashUntil - now) > 0) { rgbWrite(255, 255, 255); return; }
  bool blinkOn = (now / RGB_BLINK_MS) % 2 == 0;
  if (senderPairWindow) { rgbWrite(0, 0, blinkOn ? RGB_DIM : 0); return; }   // like the remote: not during the power-on hello
  if (!batteryOk()) { rgbWrite(0, 0, 0); batColorLevel = 255; return; }
  updateBatColorLevel();
  switch (batColorLevel) {
    case 0:  rgbWrite(0, RGB_DIM, 0); break;                       // green
    case 1:  rgbWrite(RGB_DIM, RGB_DIM * 3 / 4, 0); break;         // yellow (the green die is stronger)
    case 2:  rgbWrite(RGB_DIM, RGB_DIM / 4, 0); break;             // orange
    case 3:  rgbWrite(RGB_DIM, 0, 0); break;                       // red
    default: rgbWrite(blinkOn ? RGB_DIM : 0, 0, 0); break;         // red flashing
  }
}

// =====================================================================
// OLED
// =====================================================================
void flashTriggerFrame() {
  flashUntil = millis() + TRIGGER_FLASH_MS;
  displayDirty = true;
}

uint8_t cmToX(uint16_t cm) {
  if (cm > LUNA_RANGE_CM) cm = LUNA_RANGE_CM;
  return (uint8_t)((uint32_t)cm * 127 / LUNA_RANGE_CM);
}

// 0.91" 128x32:
//  top line (5x7) : trigger duration / "Pairing" left, battery % centre, mode right
//  middle         : live distance in cm
//  bottom         : distance meter 0-8 m, range bar + live marker
void drawDisplay(const char* txt, bool flash) {
  u8g2.clearBuffer();
  u8g2.setFontMode(1);
  u8g2.setDrawColor(1);

  // --- top line ---
  u8g2.setFont(u8g2_font_5x7_tr);
  char left[12];
  if (senderPairWindow) strcpy(left, "Pairing");
  else snprintf(left, sizeof(left), "%ums", triggerDurationMs);
  u8g2.drawStr(0, 6, left);
  const char* mode = triggerMode == MODE_MOVEMENT ? "Movement" : "Barrier";
  u8g2.drawStr(128 - u8g2.getStrWidth(mode), 6, mode);
  if (batteryOk()) {                          // small battery symbol + %
    char b[6];
    snprintf(b, sizeof(b), "%d%%", batteryPercent());
    int w = u8g2.getStrWidth(b);
    int bx = 64 - (w + 12) / 2;
    u8g2.drawFrame(bx, 0, 9, 6);
    u8g2.drawBox(bx + 9, 2, 1, 2);
    int fill = batteryPercent() * 7 / 100;
    if (fill > 0) u8g2.drawBox(bx + 1, 1, fill, 4);
    u8g2.drawStr(bx + 12, 6, b);
  }

  // --- live distance ---
  u8g2.setFont(u8g2_font_helvB12_tr);
  u8g2.drawStr((128 - u8g2.getStrWidth(txt)) / 2, 22, txt);

  // --- distance meter 0-8 m ---
  u8g2.drawHLine(0, 31, 128);
  for (uint16_t m = 0; m <= LUNA_RANGE_CM / 100; m++) u8g2.drawVLine(cmToX(m * 100), 29, 2);
  uint8_t x0 = cmToX(minCm), x1 = cmToX(maxCm);
  u8g2.drawBox(x0, 26, x1 - x0 + 1, 3);
  if (sensorOk && lunaValid) {
    u8g2.setDrawColor(2);                     // XOR: visible on the range bar too
    u8g2.drawVLine(cmToX(lunaCm), 24, 8);
    u8g2.setDrawColor(1);
  }

  // --- trigger flash: frame around the screen, XOR so everything stays readable ---
  if (flash) {
    const uint8_t f = FLASH_FRAME_PX;
    u8g2.setDrawColor(2);
    u8g2.drawBox(0, 0, 128, f);
    u8g2.drawBox(0, 32 - f, 128, f);
    u8g2.drawBox(0, f, f, 32 - 2 * f);
    u8g2.drawBox(128 - f, f, f, 32 - 2 * f);
    u8g2.setDrawColor(1);
  }
  u8g2.sendBuffer();
}

// Two centred lines: the BOOT button's "what happens on release", its result, and the update steps.
void drawTwoLines(const char* l1, const char* l2) {
  u8g2.clearBuffer();
  u8g2.setDrawColor(1);
  u8g2.setFont(u8g2_font_helvB10_tr);
  if (l2 && l2[0]) {
    u8g2.drawStr((128 - u8g2.getStrWidth(l1)) / 2, 13, l1);
    u8g2.drawStr((128 - u8g2.getStrWidth(l2)) / 2, 29, l2);
  } else {
    u8g2.drawStr((128 - u8g2.getStrWidth(l1)) / 2, 21, l1);
  }
  u8g2.sendBuffer();
}

void tierText(uint8_t tier, const char** l1, const char** l2) {
  *l2 = "";
  switch (tier) {
    case 0: *l1 = "Switch to"; *l2 = triggerMode == MODE_MOVEMENT ? "Light-Barrier" : "Movement"; break;
    case 1: *l1 = "Web App"; break;
    case 2: *l1 = "Extra Triggers"; *l2 = extraShotsEnabled ? "off" : "on"; break;
    case 3: *l1 = "Pairing"; break;
    case 4: *l1 = "Clear"; *l2 = "Pairings"; break;
    default: *l1 = "Reset"; *l2 = "Password"; break;
  }
}

void showButtonMessage(const char* l1, const char* l2) {
  strncpy(buttonMsg1, l1, sizeof(buttonMsg1) - 1);
  strncpy(buttonMsg2, l2 ? l2 : "", sizeof(buttonMsg2) - 1);
  buttonMsgUntil = millis() + BUTTON_MSG_MS;
  displayDirty = true;
}

void updateDisplay() {
  unsigned long now = millis();
  // BOOT button: while held, show what a release would do; afterwards the result
  if (buttonHeld || (long)(buttonMsgUntil - now) > 0) {
    const char *l1, *l2;
    char key[48];
    if (buttonHeld) { tierText(buttonTier, &l1, &l2); snprintf(key, sizeof(key), "H|%u|%u|%u", buttonTier, triggerMode, extraShotsEnabled ? 1 : 0); }
    else { l1 = buttonMsg1; l2 = buttonMsg2; snprintf(key, sizeof(key), "M|%s|%s", buttonMsg1, buttonMsg2); }
    displayDirty = false;
    if (strcmp(key, lastDisplayKey) == 0) return;
    strcpy(lastDisplayKey, key);
    lastDisplayDraw = now;
    drawTwoLines(l1, l2);
    return;
  }
  if (now - lastDisplayDraw < DISPLAY_REFRESH_MS && !displayDirty) return;
  // never start a ~25 ms redraw in the middle of a short shutter pulse
  if (triggerPhase == PHASE_PULSE_ON && currentPulseDurationMs() < 100) return;
  char txt[12];
  bool valid = sensorOk && lunaValid;
  if (valid) snprintf(txt, sizeof(txt), "%u cm", lunaCm);
  else strcpy(txt, "---");
  bool flash = (long)(flashUntil - now) > 0;
  char key[48];
  snprintf(key, sizeof(key), "%s|%u|%u|%u|%u|%u|%u|%d", txt, triggerMode, senderPairWindow ? 1 : 0,
           flash ? 1 : 0, minCm, maxCm, triggerDurationMs, batteryOk() ? batteryPercent() : -1);
  displayDirty = false;
  if (strcmp(key, lastDisplayKey) == 0) return;   // nothing changed -> no 25 ms redraw
  strcpy(lastDisplayKey, key);
  lastDisplayDraw = now;
  drawDisplay(txt, flash);
}

// =====================================================================
// Web page + handlers
// =====================================================================
#include "lidarpage.h"

void handleRoot() {
  server.send_P(200, "text/html", PAGE_HTML);
}

void handleCaptivePortalRedirect() {
  server.sendHeader("Location", "http://" + apIP.toString() + "/", true);
  server.send(302, "text/plain", "");
}

// Text for the JSON answers: quotes / backslashes / control characters dropped.
String jsonEsc(const char* in) {
  String o;
  for (const char* c = in; *c; c++) {
    if (*c == '"' || *c == '\\' || (uint8_t)*c < 0x20) continue;
    o += *c;
  }
  return o;
}

String configJson() {
  long timerSec = timerRemainingSecondsSigned();
  long delaySec = startDelayRemainingSecondsSigned();

  String j;
  j.reserve(900);
  j = "{\"minCm\":" + String(minCm);
  j += ",\"maxCm\":" + String(maxCm);
  j += ",\"rangeCm\":" + String(LUNA_RANGE_CM);
  j += ",\"minLimitCm\":" + String(MIN_LIMIT_CM);
  j += ",\"filter\":" + String(filterCount);
  j += ",\"lunaRate\":" + String(lunaRate);
  j += ",\"lunaMinAmp\":" + String(lunaMinAmp);
  j += ",\"mode\":" + String(triggerMode);
  j += ",\"moveFilter\":" + String(moveFilterCm);
  j += ",\"minSpanCm\":" + String(minSpanCm());
  j += ",\"triggerDuration\":" + String(triggerDurationMs);
  j += ",\"triggerDelay\":" + String(triggerDelayMs);
  j += ",\"extraCount\":" + String(extraShotsCount);
  j += ",\"extraGap\":" + String(extraGapMs);
  j += ",\"extraEnabled\":" + String(extraShotsEnabled ? "true" : "false");
  j += ",\"interval\":" + String(intervalMs);
  j += ",\"intervalEnabled\":" + String(intervalEnabled ? "true" : "false");
  j += ",\"intervalForced\":" + String(intervalForced ? "true" : "false");
  j += ",\"present\":" + String(objectPresent ? "true" : "false");
  j += ",\"timerActive\":" + String(timerArmed() ? "true" : "false");
  j += ",\"timerRemainingSeconds\":" + String(timerSec >= 0 ? timerSec : 0);
  j += ",\"startDelayActive\":" + String(startDelayActive ? "true" : "false");
  j += ",\"startDelayRemainingSeconds\":" + String(delaySec >= 0 ? delaySec : 0);
  j += ",\"txPowerIndex\":" + String(txPowerIndex);
  j += ",\"keepAlive\":" + String(keepAppAlive ? "true" : "false");
  j += ",\"passwordSet\":" + String(apPassword.length() >= 8 ? "true" : "false");
  j += ",\"protectedNow\":" + String(apCurrentlyProtected ? "true" : "false");
  j += ",\"pairedReceiverCount\":" + String(pairedReceiverCount);
  j += ",\"pairedSenderCount\":" + String(pairedSenderCount);
  j += ",\"senderPairWindow\":" + String(senderPairWindow ? "true" : "false");
  j += ",\"beacon\":" + String(beaconActive ? "true" : "false");
  j += ",\"unsaved\":" + String(settingsDirty ? "true" : "false");
  j += ",\"ssid\":\"" + String(apSSID) + "\"";
  j += ",\"version\":\"" FIRMWARE_VERSION "\"";
  j += ",\"wifiSsid\":\"" + jsonEsc(settings.wifiSsid) + "\"";
  j += ",\"wifiPassSet\":" + String(settings.wifiPass[0] ? "true" : "false");
  const char* us[] = { "idle", "joining", "checking", "installing", "uptodate", "error" };
  j += ",\"upd\":\"" + String(us[updState]) + "\"";
  j += ",\"remoteVersion\":\"" + jsonEsc(updRemoteVersion.c_str()) + "\"";
  j += ",\"updError\":\"" + jsonEsc(updError.c_str()) + "\"";
  j += ",\"progress\":" + String(updProgress) + "}";
  return j;
}

void handleGetConfig() {
  server.send(200, "application/json", configJson());
}

void handlePostConfig() {
  // ---- trigger range + filters: applied now, stored by "Save Settings" ----
  if (server.hasArg("mode")) {
    setTriggerMode((uint8_t)server.arg("mode").toInt());
  }
  if (server.hasArg("moveFilter")) {
    long v = server.arg("moveFilter").toInt();
    v = constrain(v, (long)MIN_MOVE_FILTER_CM, (long)MAX_MOVE_FILTER_CM);
    moveFilterCm = (uint8_t)(((v + 10) / 20) * 20);   // 20 cm steps
    settingsDirty = true;
  }
  if (server.hasArg("minCm") || server.hasArg("maxCm")) {
    if (server.hasArg("minCm")) minCm = (uint16_t)constrain(server.arg("minCm").toInt(), (long)MIN_LIMIT_CM, (long)LUNA_RANGE_CM);
    if (server.hasArg("maxCm")) maxCm = (uint16_t)constrain(server.arg("maxCm").toInt(), (long)MIN_LIMIT_CM, (long)LUNA_RANGE_CM);
    enforceSpan(server.hasArg("minCm"));       // the value being moved wins; the other one follows
    resetDetection();
    settingsDirty = true;
  }
  if (server.hasArg("filter")) {
    filterCount = (uint8_t)constrain(server.arg("filter").toInt(), (long)MIN_FILTER, (long)MAX_FILTER);
    settingsDirty = true;
  }
  if (server.hasArg("lunaRate")) {
    lunaRate = snapTo(LUNA_RATES, sizeof(LUNA_RATES) / 2, server.arg("lunaRate").toInt());
    settingsDirty = true;
    scheduleLunaConfig(0);
  }
  if (server.hasArg("lunaMinAmp")) {
    lunaMinAmp = snapTo(LUNA_AMPS, sizeof(LUNA_AMPS) / 2, server.arg("lunaMinAmp").toInt());
    settingsDirty = true;
    scheduleLunaConfig(0);
  }
  if (server.hasArg("sunSafe")) {             // = the "Bright" preset (kept for older pages)
    filterCount = DEFAULT_FILTER;
    lunaRate = DEFAULT_LUNA_RATE;
    lunaMinAmp = DEFAULT_LUNA_MIN_AMP;
    settingsDirty = true;
    scheduleLunaConfig(0);
  }

  // ---- local camera output: applied now, stored by "Save Settings" (extra on/off never) ----
  if (server.hasArg("triggerDuration")) {
    triggerDurationMs = (uint16_t)constrain(server.arg("triggerDuration").toInt(), (long)MIN_DURATION_MS, (long)MAX_DURATION_MS);
    settingsDirty = true;
  }
  if (server.hasArg("triggerDelay")) {
    triggerDelayMs = (uint16_t)constrain(server.arg("triggerDelay").toInt(), 0L, (long)MAX_TRIGGER_DELAY_MS);
    settingsDirty = true;
  }
  if (server.hasArg("extraCount")) {
    extraShotsCount = (uint16_t)constrain(server.arg("extraCount").toInt(), 0L, (long)MAX_EXTRA_SHOTS_TYPED);
    settingsDirty = true;
  }
  if (server.hasArg("extraGap")) {
    extraGapMs = (uint16_t)constrain(server.arg("extraGap").toInt(), (long)MIN_EXTRA_GAP_MS, (long)MAX_EXTRA_GAP_MS);
    settingsDirty = true;
  }
  if (server.hasArg("extraEnabled")) {
    extraShotsEnabled = server.arg("extraEnabled").toInt() != 0;
  }

  // ---- interval (RAM only) ----
  if (server.hasArg("interval")) {
    intervalMs = (unsigned long)constrain(server.arg("interval").toInt(), (long)MIN_INTERVAL_MS, (long)MAX_INTERVAL_MS);
  }
  if (server.hasArg("startDelayMinutes")) {     // before Timer, so a Timer sent with it is held as pending
    long m = server.arg("startDelayMinutes").toInt();
    setStartDelayMinutes((unsigned long)(m < 0 ? 0 : m));
  }
  if (server.hasArg("timerMinutes")) {
    long m = server.arg("timerMinutes").toInt();
    setTimerMinutes((unsigned long)(m < 0 ? 0 : m));
  }
  if (server.hasArg("intervalForced")) {
    intervalForced = server.arg("intervalForced").toInt() != 0;
  }
  if (server.hasArg("intervalEnabled")) {
    bool on = server.arg("intervalEnabled").toInt() != 0;
    if (on && startDelayActive) {
      finishStartDelay();                       // switching on during Delayed Start = start now
    } else {
      intervalEnabled = on;
      if (on) lastIntervalTime = millis();
    }
  }

  // ---- radio + app ----
  if (server.hasArg("txPowerIndex")) {
    txPowerIndex = (uint8_t)constrain(server.arg("txPowerIndex").toInt(), 0L, 10L);
    applyTxPower();
    settingsDirty = true;
  }
  if (server.hasArg("keepAlive")) {
    keepAppAlive = server.arg("keepAlive").toInt() != 0;
    apLastActivity = millis();
  }
  if (server.hasArg("password")) {
    String newPass = server.arg("password");
    if (newPass.length() == 0 || newPass.length() >= 8) {
      apPassword = newPass;
      strlcpy(settings.password, apPassword.c_str(), sizeof(settings.password));
      settings.passwordSaved = 1;
      saveSettings();
      applyAPConfiguration();
    }
  }
  if (server.hasArg("ssid")) {
    String newSsid = server.arg("ssid");
    newSsid.trim();
    newSsid.replace("\"", "");
    newSsid.replace("\\", "");
    if (newSsid.length() > 32) newSsid = newSsid.substring(0, 32);
    strlcpy(settings.ssidCustom, newSsid.c_str(), sizeof(settings.ssidCustom));
    saveSettings();
    rebuildSSID();
    server.send(200, "application/json", configJson());
    delay(100);
    applySSIDChangeNow();                       // restarts the AP under the new name
    return;
  }

  server.send(200, "application/json", configJson());
}

// Live sensor state for the ruler (polled ~8x per second by the page)
void handleLive() {
  char buf[260];
  snprintf(buf, sizeof(buf),
           "{\"ok\":%s,\"v\":%s,\"d\":%u,\"a\":%u,\"t\":%d,\"in\":%s,\"rps\":%lu,\"n\":%lu,\"r\":%lu,\"m\":%u,\"pw\":%s,"
           "\"bp\":%d,\"bv\":%.2f,\"bs\":\"%s\"}",
           sensorOk ? "true" : "false", (sensorOk && lunaValid) ? "true" : "false",
           lunaCm, lunaAmp, lunaTempC, objectPresent ? "true" : "false",
           (unsigned long)readingsPerSec, (unsigned long)signalsSent, (unsigned long)signalsReceived,
           triggerMode, senderPairWindow ? "true" : "false",
           batteryPercent(), batVolts, batteryState());
  server.send(200, "application/json", buf);
}

void handleHistogram() {
  unsigned long currentMinute = (millis() - histogramStartMillis) / 60000UL;
  if (currentMinute >= MAX_HISTORY_MINUTES) currentMinute = MAX_HISTORY_MINUTES - 1;

  server.setContentLength(CONTENT_LENGTH_UNKNOWN);
  server.send(200, "application/json", "");
  String chunk = "{\"first\":0,\"total\":" + String(sentSignalTotal) + ",\"contacts\":[";
  chunk.reserve(700);
  for (unsigned long i = 0; i <= currentMinute; i++) {
    chunk += String(minuteCountsContact[i]);
    if (i < currentMinute) chunk += ",";
    if (chunk.length() > 500) { server.sendContent(chunk); chunk = ""; }
  }
  chunk += "],\"interval\":[";
  for (unsigned long i = 0; i <= currentMinute; i++) {
    chunk += String(minuteCountsInterval[i]);
    if (i < currentMinute) chunk += ",";
    if (chunk.length() > 500) { server.sendContent(chunk); chunk = ""; }
  }
  chunk += "],\"received\":[";
  for (unsigned long i = 0; i <= currentMinute; i++) {
    chunk += String(minuteCountsReceived[i]);
    if (i < currentMinute) chunk += ",";
    if (chunk.length() > 500) { server.sendContent(chunk); chunk = ""; }
  }
  chunk += "]}";
  server.sendContent(chunk);
  server.sendContent("");
}

void handleResetHistogram() {
  resetHistory();
  server.send(200, "application/json", "{\"ok\":true}");
}

void handleIntervalStatus() {
  long timerSec = timerRemainingSecondsSigned();
  long delaySec = startDelayRemainingSecondsSigned();
  String json = "{\"counter\":" + String(intervalSendCounter) +
                ",\"enabled\":" + (intervalEnabled ? "true" : "false") +
                ",\"forced\":" + (intervalForced ? "true" : "false") +
                ",\"present\":" + (objectPresent ? "true" : "false") +
                ",\"timerActive\":" + (timerArmed() ? "true" : "false") +
                ",\"timerRemainingSeconds\":" + String(timerSec >= 0 ? timerSec : 0) +
                ",\"startDelayActive\":" + (startDelayActive ? "true" : "false") +
                ",\"startDelayRemainingSeconds\":" + String(delaySec >= 0 ? delaySec : 0) +
                ",\"pairedReceiverCount\":" + String(pairedReceiverCount) + "}";
  server.send(200, "application/json", json);
}

// TEST: a full signal -- local camera (with duration/extras/delay) + all paired receivers
void handleTrigger() {
  fireSignal(PACKET_TYPE_TRIGGER);
  server.send(200, "application/json", "{\"ok\":true}");
}

void handlePairBeacon() {
  startBeaconBurst();
  server.send(200, "application/json", configJson());
}

void handleClearPairings() {
  clearPairingsRequested = true;
  server.send(200, "application/json", "{\"ok\":true}");
}

void handlePairSender() {
  openSenderPairWindow();
  server.send(200, "application/json", configJson());
}

void handleClearSenders() {
  clearSendersRequested = true;
  server.send(200, "application/json", "{\"ok\":true}");
}

// ---------------- Paired Devices ----------------
String macToString(const uint8_t* m) {
  char buf[18];
  snprintf(buf, sizeof(buf), "%02X:%02X:%02X:%02X:%02X:%02X", m[0], m[1], m[2], m[3], m[4], m[5]);
  return String(buf);
}

bool macFromArg(const String& str, uint8_t* mac) {
  unsigned int b[6];
  if (sscanf(str.c_str(), "%x:%x:%x:%x:%x:%x", &b[0], &b[1], &b[2], &b[3], &b[4], &b[5]) != 6) return false;
  for (uint8_t i = 0; i < 6; i++) mac[i] = (uint8_t)b[i];
  return true;
}

// {"senders":[{"mac","name","active"},...],"triggers":[...]} -- same as the trigger / Octocam BT.
// "name" is empty for devices paired before names existed; the page shows a fallback then.
String devicesJson() {
  String j = "{\"senders\":[";
  for (uint8_t i = 0; i < pairedSenderCount; i++) {
    if (i > 0) j += ",";
    j += "{\"mac\":\"" + macToString(pairedSenderMacs[i]) + "\",\"name\":\"" + String(pairedSenderNames[i]) +
         "\",\"active\":" + (senderIsActive(i) ? "true" : "false") + "}";
  }
  j += "],\"triggers\":[";
  for (uint8_t i = 0; i < pairedReceiverCount; i++) {
    if (i > 0) j += ",";
    j += "{\"mac\":\"" + macToString(pairedReceiverMacs[i]) + "\",\"name\":\"" + String(pairedReceiverNames[i]) +
         "\",\"active\":" + (receiverIsActive(i) ? "true" : "false") + "}";
  }
  j += "]}";
  return j;
}

void handleGetDevices() {
  server.send(200, "application/json", devicesJson());
}

// POST mac=..&active=0|1 -- by MAC, so a tap can't hit the wrong device if the list changed
void handleSenderActive() {
  uint8_t mac[6];
  if (server.hasArg("mac") && server.hasArg("active") && macFromArg(server.arg("mac"), mac)) {
    int idx = findPairedSender(mac);
    if (idx >= 0) setSenderActive((uint8_t)idx, server.arg("active").toInt() != 0);
  }
  server.send(200, "application/json", devicesJson());
}
void handleSenderForget() {
  uint8_t mac[6];
  if (server.hasArg("mac") && macFromArg(server.arg("mac"), mac)) {
    int idx = findPairedSender(mac);
    if (idx >= 0) forgetSender((uint8_t)idx);
  }
  server.send(200, "application/json", devicesJson());
}
void handleTargetActive() {
  uint8_t mac[6];
  if (server.hasArg("mac") && server.hasArg("active") && macFromArg(server.arg("mac"), mac)) {
    int idx = findPairedReceiver(mac);
    if (idx >= 0) setReceiverActive((uint8_t)idx, server.arg("active").toInt() != 0);
  }
  server.send(200, "application/json", devicesJson());
}
void handleTargetForget() {
  uint8_t mac[6];
  if (server.hasArg("mac") && macFromArg(server.arg("mac"), mac)) {
    int idx = findPairedReceiver(mac);
    if (idx >= 0) forgetReceiver((uint8_t)idx);
  }
  server.send(200, "application/json", devicesJson());
}

void handleShutdownWebApp() {
  manualShutdownRequested = true;
  server.send(200, "application/json", "{\"ok\":true}");
}

// "Save Settings": copies the live settings into flash.
void copyLiveToSettings() {
  settings.minCm = minCm;
  settings.maxCm = maxCm;
  settings.mode = triggerMode;
  settings.moveFilter = moveFilterCm;
  settings.filter = filterCount;
  settings.lunaRate = lunaRate;
  settings.lunaMinAmp = lunaMinAmp;
  settings.triggerDuration = triggerDurationMs;
  settings.extraCount = extraShotsCount;
  settings.extraGap = extraGapMs;
  settings.triggerDelay = triggerDelayMs;
  settings.txpower = txPowerIndex;
}

void handleSaveSettings() {
  copyLiveToSettings();
  saveSettings();
  settingsDirty = false;
  server.send(200, "application/json", configJson());
}

// Resets the range (0.3-8 m), movement filter, trigger duration, extra
// triggers, gap, delay and interval to the sketch defaults -- live only,
// "Save Settings" stores them. Trigger range, filter,
// signal strength, password, SSID and pairings are untouched.
void handleResetDefaults() {
  minCm = DEFAULT_MIN_CM;
  maxCm = DEFAULT_MAX_CM;
  enforceSpan(true);
  moveFilterCm = DEFAULT_MOVE_FILTER_CM;
  resetDetection();
  triggerDurationMs = DEFAULT_TRIGGER_DURATION_MS;
  triggerDelayMs = DEFAULT_TRIGGER_DELAY_MS;
  extraShotsCount = DEFAULT_EXTRA_SHOTS_COUNT;
  extraShotsEnabled = false;
  extraGapMs = DEFAULT_EXTRA_GAP_MS;
  settingsDirty = true;

  intervalEnabled = true;
  intervalForced = false;
  intervalMs = DEFAULT_INTERVAL_MS;
  lastIntervalTime = millis();
  timerActive = false;
  timerPendingSeconds = 0;
  startDelayActive = false;

  server.send(200, "application/json", configJson());
}

// ---------------- Firmware update from GitHub ----------------
void handleGetFirmware() {
  server.send(200, "application/json", "{\"version\":\"" FIRMWARE_VERSION "\"}");
}

// Home WiFi name + password: stored right away, used only by Update Firmware.
// An empty password keeps the saved one (the page never gets it back).
void handleWifiSave() {
  String ssid = server.arg("ssid"), pass = server.arg("pass");
  ssid.trim();
  if (ssid.length() == 0 || ssid.length() > 32 || pass.length() > 64 || (pass.length() > 0 && pass.length() < 8)) {
    server.send(400, "application/json", "{\"ok\":false,\"error\":\"WiFi name 1-32 characters, password 8-64 characters\"}");
    return;
  }
  strlcpy(settings.wifiSsid, ssid.c_str(), sizeof(settings.wifiSsid));
  if (pass.length()) strlcpy(settings.wifiPass, pass.c_str(), sizeof(settings.wifiPass));
  saveSettings();
  server.send(200, "application/json", configJson());
}

void handleUpdate() {
  if (settings.wifiPass[0] == 0 || settings.wifiSsid[0] == 0) {
    updState = UPD_ERROR;
    updError = "Save your WiFi name and password first.";
  } else if (!updateRequested) {
    updateRequested = true;                    // runs from loop() once the answer is out
    updState = UPD_JOINING;
    updError = "";
  }
  server.send(200, "application/json", configJson());
}

void showUpdateStep() {
  char l2[24];
  switch (updState) {
    case UPD_JOINING:    snprintf(l2, sizeof(l2), "joining WiFi"); break;
    case UPD_CHECKING:   snprintf(l2, sizeof(l2), "checking"); break;
    case UPD_INSTALLING: snprintf(l2, sizeof(l2), "installing %d%%", updProgress); break;
    default:             l2[0] = 0;
  }
  drawTwoLines("Update", l2);
}

bool doUpdateCheck() {
  WiFiClientSecure client;
  client.setInsecure();                 // GitHub over HTTPS; the image itself is checked by the updater
  HTTPClient http;
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  http.setTimeout(8000);
  bool ok = false;
  if (!http.begin(client, UPDATE_BASE_URL UPDATE_VERSION_FILE)) { updError = "can't reach GitHub"; return false; }
  int code = http.GET();
  if (code == 200) {
    updRemoteVersion = http.getString();
    updRemoteVersion.trim();
    if (updRemoteVersion.length() == 0 || updRemoteVersion.length() > 40) updError = "version.txt on GitHub looks wrong";
    else ok = true;
  } else if (code == 404) {
    updError = "no firmware published on GitHub yet";
  } else {
    updError = "GitHub answered " + String(code) + " " + http.errorToString(code);
  }
  http.end();
  return ok;
}

void doUpdateInstall() {
  WiFiClientSecure client;
  client.setInsecure();
  httpUpdate.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  httpUpdate.rebootOnUpdate(true);
  httpUpdate.setLedPin(-1);
  httpUpdate.onProgress([](int done, int total) {
    int p = total > 0 ? (int)((long long)done * 100 / total) : 0;
    if (p != updProgress) { updProgress = p; if (p % 5 == 0) showUpdateStep(); }
  });
  HTTPUpdateResult res = httpUpdate.update(client, UPDATE_BASE_URL UPDATE_BIN_FILE);
  // only gets here if it failed (success restarts the Lidar)
  updError = (res == HTTP_UPDATE_NO_UPDATES) ? String("no update") : httpUpdate.getLastErrorString();
}

// "Update Firmware": leave the own network, join the home WiFi, check GitHub, install if the
// version differs (restarts), otherwise back to the own network and ESP-NOW on channel 11.
void runUpdate() {
  delay(300);                                  // let the answer reach the phone
  setOpto(false);
  triggerPhase = PHASE_IDLE;
  triggerPending = false;
  server.stop();
  dnsServer.stop();
  WiFi.softAPdisconnect(true);
  apActive = false;
  updState = UPD_JOINING;
  updProgress = 0;
  updRemoteVersion = "";
  updError = "";
  showUpdateStep();
  WiFi.mode(WIFI_STA);
  WiFi.begin(settings.wifiSsid, settings.wifiPass);
  unsigned long t0 = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - t0 < WIFI_JOIN_TIMEOUT_MS) delay(50);

  if (WiFi.status() != WL_CONNECTED) {
    updState = UPD_ERROR;
    updError = "Couldn't join " + String(settings.wifiSsid) + " (wrong password or out of range?)";
  } else {
    updState = UPD_CHECKING;
    showUpdateStep();
    if (!doUpdateCheck()) updState = UPD_ERROR;
    else if (updRemoteVersion == FIRMWARE_VERSION) updState = UPD_UPTODATE;
    else {
      updState = UPD_INSTALLING;
      showUpdateStep();
      doUpdateInstall();                       // restarts on success
      updState = UPD_ERROR;
    }
  }
  WiFi.disconnect(false, true);                // forget the home network, stay off it
  startWebApp();                               // own network + ESP-NOW on channel 11 again
  showButtonMessage(updState == UPD_UPTODATE ? "Up to date" : "Update", updState == UPD_UPTODATE ? "" : "failed");
}

// =====================================================================
// SSID + access point
// =====================================================================
void rebuildSSID() {
  if (strlen(settings.ssidCustom) > 0) {
    strlcpy(apSSID, settings.ssidCustom, sizeof(apSSID));
  } else {
    // last THREE bytes of the station MAC in hex (e.g. ...:3A:7F:12 -> "OctocamLidar3A7F12")
    uint8_t* mac = factoryStaMac;
    snprintf(apSSID, sizeof(apSSID), "OctocamLidar%02X%02X%02X", mac[3], mac[4], mac[5]);
  }
}

void applyAPNow(bool shouldBeProtected) {
  if (shouldBeProtected) WiFi.softAP(apSSID, apPassword.c_str(), WIFI_CHANNEL, 0, 1);
  else                   WiFi.softAP(apSSID, NULL, WIFI_CHANNEL, 0, 1);
  // The last argument is the DNS server DHCP hands out; core 3.x only offers it when passed
  // explicitly, and without it the "sign in to network" notification never appears.
  WiFi.softAPConfig(apIP, apIP, apSubnet, IPAddress(0, 0, 0, 0), apIP);
  apCurrentlyProtected = shouldBeProtected;
  apConfigApplied = true;
}

void applyAPConfiguration() {
  bool shouldBeProtected = (millis() >= PASSWORD_GRACE_MS) && (apPassword.length() >= 8);
  if (apConfigApplied && shouldBeProtected == apCurrentlyProtected) return;
  if (apConfigApplied && WiFi.softAPgetStationNum() > 0) return;   // wait until nobody is connected
  applyAPNow(shouldBeProtected);
}

void applySSIDChangeNow() {
  bool shouldBeProtected = (millis() >= PASSWORD_GRACE_MS) && (apPassword.length() >= 8);
  applyAPNow(shouldBeProtected);
}

void stopWebApp(bool manual) {
  server.stop();
  dnsServer.stop();
  WiFi.softAPdisconnect(true);            // station stays up for ESP-NOW
  WiFi.mode(WIFI_STA);
  esp_wifi_set_channel(WIFI_CHANNEL, WIFI_SECOND_CHAN_NONE);   // keep ESP-NOW on channel 11
  apActive = false;
  apConfigApplied = false;
  apCurrentlyProtected = false;
  displayDirty = true;
}

void startWebApp() {
  WiFi.mode(WIFI_AP_STA);
  esp_wifi_set_channel(WIFI_CHANNEL, WIFI_SECOND_CHAN_NONE);   // fix the channel before the AP starts
  esp_wifi_set_protocol(WIFI_IF_STA, WIFI_PROTOCOL_11B | WIFI_PROTOCOL_11G | WIFI_PROTOCOL_11N | WIFI_PROTOCOL_LR);
  apConfigApplied = false;
  apCurrentlyProtected = false;
  applyAPConfiguration();
  dnsServer.start(53, "*", apIP);
  server.begin();
  esp_wifi_set_channel(WIFI_CHANNEL, WIFI_SECOND_CHAN_NONE);
  apActive = true;
  apLastActivity = millis();
}

void updateWebAppLifecycle() {
  unsigned long now = millis();
  if (WiFi.softAPgetStationNum() > 0) { apLastActivity = now; return; }
  if (keepAppAlive) return;
  if (now - apLastActivity >= WEBAPP_IDLE_OFF_MS) stopWebApp(false);
}

// BOOT button (GPIO0) = button 1, same tiers as the trigger. Decided on RELEASE by
// the total hold time; while held, the OLED shows what a release would do.
uint8_t holdTier(unsigned long held) {
  if (held < SHORT_PRESS_UPPER_MS) return 0;
  if (held < WEBAPP_PRESS_UPPER_MS) return 1;
  if (held < MEDIUM_PRESS_UPPER_MS) return 2;
  if (held < PAIRING_PRESS_UPPER_MS) return 3;
  if (held < CLEAR_PAIR_UPPER_MS) return 4;
  return 5;
}

void runButtonAction(uint8_t tier) {
  switch (tier) {
    case 0:
      setTriggerMode(triggerMode == MODE_MOVEMENT ? MODE_BARRIER : MODE_MOVEMENT);
      showButtonMessage(triggerMode == MODE_MOVEMENT ? "Movement" : "Light-Barrier", "");
      break;
    case 1:
      if (!apActive) startWebApp();
      else apLastActivity = millis();
      showButtonMessage("Web App", "on");
      break;
    case 2:
      extraShotsEnabled = !extraShotsEnabled;
      showButtonMessage("Extra Triggers", extraShotsEnabled ? "on" : "off");
      break;
    case 3:
      openSenderPairWindow();      // senders: hello-only, 8 s ("Pairing" top left meanwhile)
      startBeaconBurst();          // triggers: our hello for 10 s
      showButtonMessage("Pairing", "started");
      break;
    case 4:
      clearPairedSenders();
      clearPairedReceivers();
      showButtonMessage("Pairings", "cleared");
      break;
    default:
      apPassword = DEFAULT_AP_PASSWORD;
      strlcpy(settings.password, apPassword.c_str(), sizeof(settings.password));
      settings.passwordSaved = 1;
      saveSettings();              // the struct only holds SAVED values, so nothing unsaved is stored
      if (apActive) applySSIDChangeNow();   // restart the network with the default password
      showButtonMessage("Password", "reset");
      break;
  }
}

void checkWakeButton() {
  static bool lastStable = HIGH, lastReading = HIGH;
  static unsigned long lastChange = 0, pressStart = 0;
  bool reading = digitalRead(WAKE_BUTTON_PIN);
  if (reading != lastReading) { lastChange = millis(); lastReading = reading; }
  if (millis() - lastChange > 40 && reading != lastStable) {
    lastStable = reading;
    if (reading == LOW) {
      pressStart = millis();
      buttonHeld = true;
      buttonTier = 0;
      buttonMsgUntil = 0;
      displayDirty = true;
    } else {
      buttonHeld = false;
      runButtonAction(holdTier(millis() - pressStart));
    }
  }
  if (buttonHeld && lastStable == LOW) {
    uint8_t t = holdTier(millis() - pressStart);
    if (t != buttonTier) { buttonTier = t; displayDirty = true; }
  }
}

// =====================================================================
// Setup / loop
// =====================================================================
void setup() {
  esp_read_mac(factoryStaMac, ESP_MAC_WIFI_STA);
  pinMode(OPTO_PIN, OUTPUT);
  setOpto(false);
  pinMode(WAKE_BUTTON_PIN, INPUT_PULLUP);
  rgbWrite(0, 0, 0);
  pinMode(LED_PIN, OUTPUT);
  ledWrite(true);                            // power-on blink
  Serial.begin(115200);                      // USB: debug only

  // TF-Luna on UART2: RX IO27 <- TXD, TX IO26 -> RXD
  Serial2.setRxBufferSize(1024);
  Serial2.begin(LUNA_BAUD, SERIAL_8N1, LUNA_RX_PIN, LUNA_TX_PIN);

  analogReadResolution(12);
  analogSetPinAttenuation(BAT_PIN, ADC_11db);
  measureBattery();
  checkBatteryAtBoot();                      // empty cell: back to deep sleep before WiFi starts
  delay(LED_BOOT_BLINK_MS);
  ledWrite(false);

  loadSettings();
  minCm = settings.minCm;
  maxCm = settings.maxCm;
  if (minCm < MIN_LIMIT_CM || maxCm > LUNA_RANGE_CM || maxCm < minCm + MIN_SPAN_CM) {
    minCm = DEFAULT_MIN_CM;
    maxCm = DEFAULT_MAX_CM;
  }
  // 1.1 settings (garbage / 0xFF from an older save -> defaults)
  bool fix = false;
  if (settings.mode > MODE_BARRIER) { settings.mode = DEFAULT_MODE; fix = true; }
  if (settings.moveFilter < MIN_MOVE_FILTER_CM || settings.moveFilter > MAX_MOVE_FILTER_CM || settings.moveFilter % 20) {
    settings.moveFilter = DEFAULT_MOVE_FILTER_CM; fix = true;
  }
  if (settings.triggerDuration < MIN_DURATION_MS || settings.triggerDuration > MAX_DURATION_MS) {
    settings.triggerDuration = DEFAULT_TRIGGER_DURATION_MS; fix = true;
  }
  if (settings.sndcnt > MAX_PAIRED_SENDERS) { settings.sndcnt = 0; fix = true; }
  triggerMode = settings.mode;
  moveFilterCm = settings.moveFilter;
  triggerDurationMs = settings.triggerDuration;
  enforceSpan(true);
  if (settings.minCm != minCm || settings.maxCm != maxCm) { settings.minCm = minCm; settings.maxCm = maxCm; fix = true; }
  if (fix) saveSettings();
  filterCount = constrain(settings.filter, MIN_FILTER, MAX_FILTER);
  lunaRate = snapTo(LUNA_RATES, sizeof(LUNA_RATES) / 2, settings.lunaRate);      // also repairs
  lunaMinAmp = snapTo(LUNA_AMPS, sizeof(LUNA_AMPS) / 2, settings.lunaMinAmp);    // garbage from older saves
  if (settings.lunaRate != lunaRate || settings.lunaMinAmp != lunaMinAmp) {
    if (settings.lunaRate == 0 || settings.lunaRate == 0xFFFF) lunaRate = DEFAULT_LUNA_RATE;
    if (settings.lunaMinAmp == 0 || settings.lunaMinAmp == 0xFFFF) lunaMinAmp = DEFAULT_LUNA_MIN_AMP;
    settings.lunaRate = lunaRate;
    settings.lunaMinAmp = lunaMinAmp;
    saveSettings();
  }
  scheduleLunaConfig(1000);                  // give the TF-Luna time to boot first
  extraShotsCount = settings.extraCount > MAX_EXTRA_SHOTS_TYPED ? MAX_EXTRA_SHOTS_TYPED : settings.extraCount;
  extraGapMs = constrain(settings.extraGap, MIN_EXTRA_GAP_MS, MAX_EXTRA_GAP_MS);
  triggerDelayMs = settings.triggerDelay > MAX_TRIGGER_DELAY_MS ? MAX_TRIGGER_DELAY_MS : settings.triggerDelay;
  txPowerIndex = settings.txpower > 10 ? DEFAULT_TX_POWER_INDEX : settings.txpower;
  apPassword = settings.passwordSaved ? String(settings.password) : String(DEFAULT_AP_PASSWORD);

  u8g2.begin();
  u8g2.setBusClock(400000);
  drawDisplay("---", false);

  histogramStartMillis = millis();
  rebuildSSID();

  server.on("/", handleRoot);
  server.on("/generate_204", handleCaptivePortalRedirect);
  server.on("/gen_204", handleCaptivePortalRedirect);
  server.on("/hotspot-detect.html", handleCaptivePortalRedirect);
  server.on("/ncsi.txt", handleCaptivePortalRedirect);
  server.on("/connecttest.txt", handleCaptivePortalRedirect);
  server.on("/library/test/success.html", handleCaptivePortalRedirect);
  server.on("/api/config", HTTP_GET, handleGetConfig);
  server.on("/api/config", HTTP_POST, handlePostConfig);
  server.on("/api/live", HTTP_GET, handleLive);
  server.on("/api/histogram", HTTP_GET, handleHistogram);
  server.on("/api/reset-histogram", HTTP_POST, handleResetHistogram);
  server.on("/api/interval-status", HTTP_GET, handleIntervalStatus);
  server.on("/api/trigger", HTTP_POST, handleTrigger);
  server.on("/api/pair", HTTP_POST, handlePairBeacon);
  server.on("/api/clear-pairings", HTTP_POST, handleClearPairings);
  server.on("/api/pair-sender", HTTP_POST, handlePairSender);
  server.on("/api/clear-senders", HTTP_POST, handleClearSenders);
  server.on("/api/devices", HTTP_GET, handleGetDevices);
  server.on("/api/sender-active", HTTP_POST, handleSenderActive);
  server.on("/api/sender-forget", HTTP_POST, handleSenderForget);
  server.on("/api/target-active", HTTP_POST, handleTargetActive);
  server.on("/api/target-forget", HTTP_POST, handleTargetForget);
  server.on("/api/shutdown-webapp", HTTP_POST, handleShutdownWebApp);
  server.on("/api/reset-defaults", HTTP_POST, handleResetDefaults);
  server.on("/api/save-settings", HTTP_POST, handleSaveSettings);
  server.on("/api/firmware", HTTP_GET, handleGetFirmware);
  server.on("/api/wifi", HTTP_POST, handleWifiSave);
  server.on("/api/update", HTTP_POST, handleUpdate);
  server.onNotFound(handleCaptivePortalRedirect);

  WiFi.persistent(false);
  WiFi.setAutoReconnect(false);            // the station only joins a network during an update
  WiFi.mode(WIFI_AP_STA);
  WiFi.disconnect();
  WiFi.setSleep(false);                    // lowest trigger latency; the sensor draws more anyway
  startWebApp();

  initEspNow();
  Serial.printf("Octocam Lidar %s  network %s  http://8.8.8.8\n", FIRMWARE_VERSION, apSSID);
  startBeaconBurst();                      // 10 s pairing window after every power-on
}

void loop() {
  readLuna();                // first, every pass: trigger decisions come from here
  processRxEvents();         // triggers from paired LS senders
  updatePresence();
  updateLunaConfig();
  checkInterval();
  checkIntervalTimers();
  updatePendingTrigger();
  updateTrigger();
  updateBeacon();
  processPendingPairAcks();
  checkWakeButton();
  updateBattery();
  checkBatteryEmpty();
  updateRgb();

  if (apActive) {
    dnsServer.processNextRequest();
    server.handleClient();
    if (manualShutdownRequested) { manualShutdownRequested = false; stopWebApp(true); }
    if (clearPairingsRequested) { clearPairingsRequested = false; clearPairedReceivers(); }
    if (clearSendersRequested) { clearSendersRequested = false; clearPairedSenders(); }
    if (updateRequested) { updateRequested = false; runUpdate(); return; }
    unsigned long now = millis();
    if (apActive && now - lastApHousekeeping >= 250) {
      lastApHousekeeping = now;
      applyAPConfiguration();
      updateWebAppLifecycle();
    }
  }

  readLuna();                // again before the (slow) display update
  processRxEvents();
  updateDisplay();
  delay(1);                  // let the WiFi / idle tasks run
}