// =============================================================================
// IoT Wall Clock (ESP8266 + WS2812B 8x32)
// -----------------------------------------------------------------------------
// HW
//   WS2812B DIN : D1 (GPIO5)
//   DHT11  DATA : D2 (GPIO4)   + 10k pull-up to 3V3
//   CDS    A0   : A0 - CDS - GND, A0 - 10k - 3V3  (어두울수록 A0 전압 ↑)
//   WS2812B VCC : 5V (전용 전원 권장. 8x32=256LED, 밝기 제한 시 2A로 충분)
//
// Required Libraries (Library Manager 에서 설치)
//   - WiFiManager           by tzapu
//   - ArduinoJson           by Benoit Blanchon
//   - Adafruit GFX Library
//   - Adafruit NeoPixel
//   - Adafruit NeoMatrix
//   - DHT sensor library    by Adafruit  (+ Adafruit Unified Sensor)
//
// Board : NodeMCU 1.0 (ESP-12E Module) / Generic ESP8266
// =============================================================================

#include <ESP8266WiFi.h>
#include <ESP8266WiFiMulti.h>
#include <ESP8266HTTPClient.h>
#include <WiFiClient.h>
#include <WiFiClientSecure.h>
#include <WiFiManager.h>
#include <EEPROM.h>
#include <ArduinoJson.h>
#include <Adafruit_GFX.h>
#include <Adafruit_NeoMatrix.h>
#include <Adafruit_NeoPixel.h>
#include <DHT.h>
#include <time.h>

// --------------------------- 2nd AP (EEPROM) ----------------------------------
#define EEPROM_SIZE       96
#define EEPROM_SSID2_OFS   0   // 32 bytes
#define EEPROM_PASS2_OFS  32   // 64 bytes

// --------------------------- 핀 / 상수 ---------------------------------------
#define LED_PIN     D1
#define DHT_PIN     D2
#define CDS_PIN     A0

#define MATRIX_W    32
#define MATRIX_H    8
#define DHT_TYPE    DHT11

#define TZ_OFFSET_SEC   (9 * 3600)   // KST
#define DST_OFFSET_SEC  0

#define WEATHER_INTERVAL_MS   60000UL     // 1분
#define DHT_INTERVAL_MS       2500UL
#define CDS_INTERVAL_MS       500UL
#define WIFI_CHECK_INTERVAL   5000UL
#define FRAME_INTERVAL_MS     50UL

// CDS raw 값이 이 이상이면 야간 모드 (주위 조명 꺼진 상태 → 시계만 표시)
#define CDS_NIGHT_THRESHOLD   960

#define CYCLE_MS          30000UL   // 전체 30초 주기
#define CLOCK_PHASE_MS    15000UL   // 0~15s  : 시계
                                    // 15~30s : 통합 스크롤 (날짜+온습도+날씨, 15s)

// 패널 배치 플래그 — 실제 모듈에 맞춰 수정 필요 (아래 주석 참고)
#define MATRIX_FLAGS  (NEO_MATRIX_TOP + NEO_MATRIX_LEFT + \
                       NEO_MATRIX_COLUMNS + NEO_MATRIX_ZIGZAG)

// RGB565 color
#define C_CYAN    0x07FF
#define C_YELLOW  0xFFE0
#define C_ORANGE  0xFC00
#define C_RED     0xF800
#define C_WHITE   0xFFFF
#define C_GREEN   0x07E0

// --------------------------- 전역 객체 ---------------------------------------
Adafruit_NeoMatrix matrix(MATRIX_W, MATRIX_H, LED_PIN,
                          MATRIX_FLAGS, NEO_GRB + NEO_KHZ800);
DHT dht(DHT_PIN, DHT_TYPE);
ESP8266WiFiMulti g_wifiMulti;

struct WeatherInfo {
  char city[32];
  char condition[12];   // "Sunny" / "Rain" / "Cloud" / "Snow" / "Fog" / "Storm"
  int  tempC;
  int  humiPct;         // 외부 습도 % (wttr.in %h)
  bool valid;
};

WeatherInfo g_w = { "Seoul", "---", 0, 0, false };

float    g_humi        = NAN;    // DHT11 실내 습도 (%)
uint8_t  g_brightness  = 20;
bool     g_wifiOK      = false;
bool     g_nightMode   = false;   // 야간 모드: CDS 감지 어두우면 시계만 표시

char     g_ssid2[33]   = {0};    // 2번째 AP SSID (EEPROM)
char     g_pass2[65]   = {0};    // 2번째 AP 비밀번호 (EEPROM)

// 공휴일 (date.nager.at, 최대 30개, 연초 자동 갱신)
struct HoliDay { uint8_t mon; uint8_t day; };
HoliDay  g_hols[30];
uint8_t  g_holCount    = 0;
int      g_holYear     = 0;      // 0 = 미취득

unsigned long tWeather = 0;
unsigned long tDHT     = 0;
unsigned long tCDS     = 0;
unsigned long tWiFi    = 0;
unsigned long tFrame   = 0;

int      scrollX       = MATRIX_W;   // 통합 스크롤
bool     scrollDone    = false;       // 스크롤 완료 플래그

// --------------------------- 전방 선언 ---------------------------------------
void loadAP2();
void saveAP2();
void connectWiFi();
void fetchCityByIP();
void fetchWeather();
void fetchHolidays();
bool isHoliday(int mon, int day);
const char* descToCondition(const String& d);
const uint8_t* iconBitmap(const char* cond);
uint16_t iconColor(const char* cond);
void drawIcon8(int x, int y, const uint8_t* bmp, uint16_t color);
void drawDigit(int x, int y, int d, uint16_t color);
void readDHT();
void updateBrightness();
void drawFrame();
void drawClock(const struct tm* lt);
void drawScrollInfo(const struct tm* lt);
void drawWiFiQuestion();

// =============================================================================
// SETUP
// =============================================================================
void setup() {
  Serial.begin(115200);
  delay(100);
  Serial.println();
  Serial.println(F("[IoT WallClock] boot"));

  pinMode(CDS_PIN, INPUT);
  dht.begin();

  matrix.begin();
  matrix.setTextWrap(false);
  matrix.setBrightness(g_brightness);
  matrix.setTextColor(C_CYAN);
  matrix.fillScreen(0);
  matrix.setCursor(2, 0);
  matrix.print(F("BOOT"));
  matrix.show();

  connectWiFi();

  // NTP 시작 (configTime 은 내부적으로 SNTP 폴링)
  configTime(TZ_OFFSET_SEC, DST_OFFSET_SEC,
             "pool.ntp.org", "time.google.com", "kr.pool.ntp.org");

  if (g_wifiOK) {
    fetchCityByIP();
    fetchWeather();
    fetchHolidays();      // 한국 공휴일 취득 (date.nager.at)
  }
  tWeather = millis();
}

// =============================================================================
// LOOP
// =============================================================================
void loop() {
  unsigned long now = millis();

  if (now - tWiFi >= WIFI_CHECK_INTERVAL) {
    tWiFi = now;
    if (WiFi.status() == WL_CONNECTED) {
      g_wifiOK = true;
    } else {
      // 연결 끊김 → WiFiMulti로 재스캔 (신호 강한 AP 자동 선택)
      Serial.println(F("[WiFi] disconnected, scanning..."));
      g_wifiOK = (g_wifiMulti.run(8000) == WL_CONNECTED);
      if (g_wifiOK) {
        Serial.printf("[WiFi] reconnected → %s\n", WiFi.SSID().c_str());
      }
    }
  }

  if (now - tDHT >= DHT_INTERVAL_MS) {
    tDHT = now;
    readDHT();
  }

  if (now - tCDS >= CDS_INTERVAL_MS) {
    tCDS = now;
    updateBrightness();
  }

  if (g_wifiOK && (now - tWeather >= WEATHER_INTERVAL_MS)) {
    tWeather = now;
    fetchWeather();
  }

  // 공휴일: 연초 자동 갱신 (연도 바뀌면 재취득)
  if (g_wifiOK) {
    time_t t = time(nullptr);
    struct tm lt2;
    localtime_r(&t, &lt2);
    int yr = lt2.tm_year + 1900;
    if (yr >= 2024 && yr != g_holYear) fetchHolidays();
  }

  if (now - tFrame >= FRAME_INTERVAL_MS) {
    tFrame = now;
    drawFrame();
  }
}

// =============================================================================
// EEPROM : 2번째 AP 저장 / 로드
// =============================================================================
void loadAP2() {
  EEPROM.begin(EEPROM_SIZE);
  for (int i = 0; i < 32; i++) g_ssid2[i] = (char)EEPROM.read(EEPROM_SSID2_OFS + i);
  for (int i = 0; i < 64; i++) g_pass2[i] = (char)EEPROM.read(EEPROM_PASS2_OFS + i);
  EEPROM.end();
  g_ssid2[32] = '\0';
  g_pass2[64] = '\0';
  // 비정상 데이터(첫 부팅) 방어
  for (int i = 0; i < 32; i++) {
    char c = g_ssid2[i];
    if (c != 0 && (c < 0x20 || c > 0x7E)) {
      memset(g_ssid2, 0, sizeof(g_ssid2));
      memset(g_pass2, 0, sizeof(g_pass2));
      break;
    }
  }
  if (g_ssid2[0]) Serial.printf("[EEPROM] AP2 loaded: '%s'\n", g_ssid2);
}

void saveAP2() {
  EEPROM.begin(EEPROM_SIZE);
  for (int i = 0; i < 32; i++) EEPROM.write(EEPROM_SSID2_OFS + i, (uint8_t)g_ssid2[i]);
  for (int i = 0; i < 64; i++) EEPROM.write(EEPROM_PASS2_OFS + i, (uint8_t)g_pass2[i]);
  EEPROM.commit();
  EEPROM.end();
  Serial.printf("[EEPROM] AP2 saved: '%s'\n", g_ssid2);
}

// =============================================================================
// WiFi / Captive Portal  (1번 AP: WiFiManager, 2번 AP: 커스텀 파라미터)
// =============================================================================
void connectWiFi() {
  loadAP2();

  // WiFiManager 커스텀 파라미터 (2번째 AP 입력 필드)
  WiFiManagerParameter p_ssid2("ssid2", "2nd WiFi SSID (선택)", g_ssid2, 32);
  WiFiManagerParameter p_pass2("pass2", "2nd WiFi Password", g_pass2, 64);

  bool needSave = false;
  WiFiManager wm;
  wm.addParameter(&p_ssid2);
  wm.addParameter(&p_pass2);
  wm.setSaveConfigCallback([&needSave]() { needSave = true; });
  wm.setConfigPortalTimeout(180);
  wm.setConnectTimeout(20);

  if (!wm.autoConnect("IoTClock-Setup")) {
    Serial.println(F("[WiFi] portal timeout, restart"));
    delay(500);
    ESP.restart();
  }

  // 포털에서 저장 버튼 눌렸으면 2번 AP 정보 기록
  if (needSave) {
    strncpy(g_ssid2, p_ssid2.getValue(), 32);  g_ssid2[32] = '\0';
    strncpy(g_pass2, p_pass2.getValue(), 64);  g_pass2[64] = '\0';
    saveAP2();
  }

  // WiFiMulti에 1번+2번 AP 등록
  String ssid1 = WiFi.SSID();
  String psk1  = WiFi.psk();
  if (ssid1.length() > 0) {
    g_wifiMulti.addAP(ssid1.c_str(), psk1.c_str());
    Serial.printf("[WiFi] AP1: '%s'\n", ssid1.c_str());
  }
  if (g_ssid2[0]) {
    g_wifiMulti.addAP(g_ssid2, g_pass2);
    Serial.printf("[WiFi] AP2: '%s'\n", g_ssid2);
  }

  g_wifiOK = (WiFi.status() == WL_CONNECTED);
  Serial.printf("[WiFi] connected → %s  IP=%s\n",
                WiFi.SSID().c_str(), WiFi.localIP().toString().c_str());
}

// =============================================================================
// IP → 도시 이름  (ip-api.com, HTTP, 키 불필요)
// =============================================================================
void fetchCityByIP() {
  WiFiClient client;
  HTTPClient http;
  if (!http.begin(client, "http://ip-api.com/json/?fields=status,city")) return;
  http.setTimeout(8000);
  int code = http.GET();
  if (code == 200) {
    StaticJsonDocument<256> doc;
    if (!deserializeJson(doc, http.getStream())) {
      const char* st = doc["status"] | "";
      const char* ct = doc["city"]   | "";
      if (strcmp(st, "success") == 0 && ct[0]) {
        strlcpy(g_w.city, ct, sizeof(g_w.city));
        Serial.print(F("[IP] city="));
        Serial.println(g_w.city);
      }
    }
  } else {
    Serial.printf("[IP] http=%d\n", code);
  }
  http.end();
}

// =============================================================================
// 날씨  (wttr.in, JSON 크므로 필터 파싱)
// =============================================================================
void fetchWeather() {
  WiFiClientSecure client;
  client.setInsecure();
  client.setBufferSizes(512, 512);           // 짧은 응답이라 512 바이트로 충분

  HTTPClient http;
  // 포맷 : "<desc>/<temp>/<humi>"  예) "Sunny/+22°C/55%"
  String url = String("https://wttr.in/") + g_w.city + "?format=%C/%t/%h";
  Serial.print(F("[Weather] GET "));
  Serial.println(url);

  if (!http.begin(client, url)) {
    Serial.println(F("[Weather] http.begin failed"));
    return;
  }
  http.setTimeout(15000);
  http.setUserAgent("curl/7.68");
  http.setFollowRedirects(HTTPC_FORCE_FOLLOW_REDIRECTS);
  http.setReuse(false);

  int code = http.GET();
  Serial.printf("[Weather] http=%d  heap=%u\n", code, ESP.getFreeHeap());

  if (code == 200) {
    String body = http.getString();
    body.trim();
    Serial.print(F("[Weather] body: "));
    Serial.println(body);

    int p1 = body.indexOf('/');
    int p2 = body.lastIndexOf('/');
    if (p1 > 0 && p2 > p1) {
      String desc = body.substring(0, p1);
      String tstr = body.substring(p1 + 1, p2);   // "+22°C"
      String hstr = body.substring(p2 + 1);       // "55%"

      // 온도 파싱: 부호+숫자만 (°, C 등은 무시)
      int sign = 1, t = 0, i = 0;
      if (tstr.length() && tstr.charAt(0) == '+') i = 1;
      else if (tstr.length() && tstr.charAt(0) == '-') { sign = -1; i = 1; }
      while (i < (int)tstr.length() &&
             tstr.charAt(i) >= '0' && tstr.charAt(i) <= '9') {
        t = t * 10 + (tstr.charAt(i) - '0');
        i++;
      }
      g_w.tempC = t * sign;

      // 습도 파싱: "55%" → 55
      g_w.humiPct = hstr.toInt();   // toInt() 는 숫자 앞부분만 파싱

      strlcpy(g_w.condition, descToCondition(desc), sizeof(g_w.condition));
      g_w.valid = true;
      Serial.printf("[Weather] %s %dC %d%% desc=\"%s\" -> %s\n",
                    g_w.city, g_w.tempC, g_w.humiPct, desc.c_str(), g_w.condition);
    } else {
      Serial.println(F("[Weather] parse fail"));
    }
  }
  http.end();
}

// =============================================================================
// 한국 공휴일 취득  (date.nager.at/api/v3/PublicHolidays/{year}/KR)
//   설날·추석·부처님오신날 등 음력 기반 공휴일도 그레고리력으로 자동 변환
// =============================================================================
void fetchHolidays() {
  time_t t = time(nullptr);
  struct tm lt2;
  localtime_r(&t, &lt2);
  int year = lt2.tm_year + 1900;
  if (year < 2024 || year == g_holYear) return;   // 이미 취득 완료

  WiFiClientSecure client;
  client.setInsecure();
  client.setBufferSizes(2048, 512);

  HTTPClient http;
  String url = "https://date.nager.at/api/v3/PublicHolidays/" + String(year) + "/KR";
  Serial.print(F("[Holiday] GET ")); Serial.println(url);

  if (!http.begin(client, url)) { Serial.println(F("[Holiday] begin fail")); return; }
  http.setTimeout(15000);
  http.setUserAgent("curl/7.68");
  http.setFollowRedirects(HTTPC_FORCE_FOLLOW_REDIRECTS);

  int code = http.GET();
  Serial.printf("[Holiday] http=%d\n", code);

  if (code == 200) {
    // "date" 필드만 추출해 메모리 절약
    StaticJsonDocument<64> filter;
    filter[0]["date"] = true;
    DynamicJsonDocument doc(2048);
    DeserializationError err = deserializeJson(doc, http.getStream(),
                                               DeserializationOption::Filter(filter));
    if (!err) {
      g_holCount = 0;
      for (JsonObject obj : doc.as<JsonArray>()) {
        const char* ds = obj["date"] | "";   // "YYYY-MM-DD"
        if (strlen(ds) >= 10 && g_holCount < 30) {
          g_hols[g_holCount].mon = (ds[5]-'0')*10 + (ds[6]-'0');
          g_hols[g_holCount].day = (ds[8]-'0')*10 + (ds[9]-'0');
          g_holCount++;
        }
      }
      g_holYear = year;
      Serial.printf("[Holiday] %d holidays loaded for %d\n", g_holCount, year);
    } else {
      Serial.printf("[Holiday] parse err: %s\n", err.c_str());
    }
  }
  http.end();
}

bool isHoliday(int mon, int day) {
  for (int i = 0; i < g_holCount; i++) {
    if (g_hols[i].mon == mon && g_hols[i].day == day) return true;
  }
  return false;
}

const char* descToCondition(const String& d) {
  if (d.indexOf("Thunder") >= 0 || d.indexOf("storm") >= 0) return "Storm";
  if (d.indexOf("Snow") >= 0 || d.indexOf("Sleet")  >= 0 ||
      d.indexOf("Blizzard") >= 0)                           return "Snow";
  if (d.indexOf("Rain") >= 0 || d.indexOf("Drizzle") >= 0 ||
      d.indexOf("Shower") >= 0)                             return "Rain";
  if (d.indexOf("Fog") >= 0 || d.indexOf("Mist") >= 0)      return "Fog";
  if (d.indexOf("Partly") >= 0)                             return "PCloud";
  if (d.indexOf("Cloud") >= 0 || d.indexOf("Overcast") >= 0)return "Cloud";
  if (d.indexOf("Sun") >= 0 || d.indexOf("Clear") >= 0)     return "Sunny";
  return "---";
}

// --------------------------- 날씨 아이콘 8x8 (bit7 = 왼쪽) -------------------
const uint8_t PROGMEM ICO_SUN[8] = {
  0b00011000,
  0b10011001,
  0b00111100,
  0b01111110,
  0b01111110,
  0b00111100,
  0b10011001,
  0b00011000
};
const uint8_t PROGMEM ICO_CLOUD[8] = {
  0b00000000,
  0b00111000,
  0b01111100,
  0b01111110,
  0b11111111,
  0b11111111,
  0b11111111,
  0b00000000
};
const uint8_t PROGMEM ICO_PCLOUD[8] = {
  0b01000000,
  0b11100000,
  0b01011100,
  0b00111110,
  0b01111111,
  0b11111111,
  0b11111111,
  0b00000000
};
const uint8_t PROGMEM ICO_RAIN[8] = {
  0b00111000,
  0b01111110,
  0b11111111,
  0b11111111,
  0b00000000,
  0b01010101,
  0b10101010,
  0b01010100
};
const uint8_t PROGMEM ICO_SNOW[8] = {
  0b00111000,
  0b01111110,
  0b11111111,
  0b00000000,
  0b01001001,
  0b10010010,
  0b01001001,
  0b10010010
};
const uint8_t PROGMEM ICO_FOG[8] = {
  0b00000000,
  0b11111111,
  0b00000000,
  0b11111111,
  0b00000000,
  0b11111111,
  0b00000000,
  0b11111111
};
const uint8_t PROGMEM ICO_STORM[8] = {
  0b00111000,
  0b01111110,
  0b11111111,
  0b11111111,
  0b00001000,
  0b00011000,
  0b00110000,
  0b00100000
};

const uint8_t* iconBitmap(const char* cond) {
  if (!strcmp(cond, "Sunny"))  return ICO_SUN;
  if (!strcmp(cond, "Cloud"))  return ICO_CLOUD;
  if (!strcmp(cond, "PCloud")) return ICO_PCLOUD;
  if (!strcmp(cond, "Rain"))   return ICO_RAIN;
  if (!strcmp(cond, "Snow"))   return ICO_SNOW;
  if (!strcmp(cond, "Fog"))    return ICO_FOG;
  if (!strcmp(cond, "Storm"))  return ICO_STORM;
  return ICO_CLOUD;
}

uint16_t iconColor(const char* cond) {
  if (!strcmp(cond, "Sunny"))  return C_YELLOW;
  if (!strcmp(cond, "Storm"))  return C_YELLOW;
  if (!strcmp(cond, "Rain"))   return matrix.Color( 80, 140, 255);
  if (!strcmp(cond, "Snow"))   return C_WHITE;
  if (!strcmp(cond, "Fog"))    return matrix.Color(120, 120, 120);
  if (!strcmp(cond, "PCloud")) return C_WHITE;
  return C_WHITE;   // Cloud
}

void drawIcon8(int x, int y, const uint8_t* bmp, uint16_t color) {
  for (int r = 0; r < 8; r++) {
    uint8_t row = pgm_read_byte(&bmp[r]);
    for (int c = 0; c < 8; c++) {
      if (row & (1 << (7 - c))) matrix.drawPixel(x + c, y + r, color);
    }
  }
}

// =============================================================================
// DHT11 — 습도만 사용 (온도는 wttr.in 외부값 사용)
// =============================================================================
void readDHT() {
  float h = dht.readHumidity();
  if (!isnan(h)) {
    g_humi = h;
    Serial.printf("[DHT] humi=%.0f%%\n", h);
  } else {
    Serial.println(F("[DHT] read fail"));
  }
}

// =============================================================================
// CDS → 자동 밝기 + 야간 모드 감지
//   회로: A0-CDS-GND, A0-10k-3V3
//   어두움 → CDS 저항 ↑ → A0 전압 ↑ → adc 값 ↑ → LED 밝기 ↓
// =============================================================================
void updateBrightness() {
  int raw = analogRead(CDS_PIN);    // 0..1023

  // 밝기 구간
  //   일반 (raw 350 ~ THRESHOLD): 150 → 5  (주위 밝기에 비례)
  //   야간 (raw THRESHOLD ~ 1023): 4  → 1  (조명 꺼진 상태, 극저)
  g_nightMode = (raw > CDS_NIGHT_THRESHOLD);

  int b;
  if (g_nightMode) {
    b = map(raw, CDS_NIGHT_THRESHOLD, 1023, 4, 1);
    b = constrain(b, 1, 4);
  } else {
    b = map(raw, 350, CDS_NIGHT_THRESHOLD, 150, 5);
    b = constrain(b, 5, 150);
  }

  // 1차 IIR 필터 (깜빡임 방지) 3:1
  g_brightness = (uint8_t)((g_brightness * 3 + b) / 4);
  matrix.setBrightness(g_brightness);

  static unsigned long tLog = 0;
  if (millis() - tLog > 1000) {
    tLog = millis();
    Serial.printf("[CDS] raw=%d -> target=%d  bright=%d  night=%d\n",
                  raw, b, g_brightness, g_nightMode);
  }
}

// =============================================================================
// Display
// =============================================================================
void drawFrame() {
  matrix.fillScreen(0);

  if (!g_wifiOK) {
    drawWiFiQuestion();
    matrix.show();
    return;
  }

  unsigned long phase = millis() % CYCLE_MS;
  time_t nowT = time(nullptr);
  struct tm lt;
  localtime_r(&nowT, &lt);

  if (g_nightMode || phase < CLOCK_PHASE_MS || scrollDone) {
    // 야간 모드 또는 시계 구간 또는 스크롤 완료 → 시계 표시
    drawClock(&lt);
    if (!g_nightMode && phase < CLOCK_PHASE_MS) { // 시계 구간 진입 시 초기화
      scrollX    = MATRIX_W;
      scrollDone = false;
    }
  } else {                                   // 날짜 + 외부 온습도 + 날씨 통합 스크롤
    drawScrollInfo(&lt);
  }
  matrix.show();
}

// =============================================================================
// 커스텀 5×7 픽셀 폰트 (숫자 0~9)
//   각 행은 5비트 (bit4 = 왼쪽, bit0 = 오른쪽)
//   기존 Adafruit 기본 폰트보다 굵고 가독성 높은 디자인
//   7: 계단형, 6/9: 둥근 마감, 1: 세리프 받침
// =============================================================================
// 출발안내판(Departure Board) 스타일 5×7 LED 폰트
// bit4=왼쪽, bit0=오른쪽 (5비트 유효)
//
//  0:.###.  1:..#..  2:.###.  3:.###.  4:#..#.
//    #...#    .##..    #...#    #...#    #..#.
//    #...#    ..#..    ....#    ....#    #..#.
//    #...#    ..#..    ..##.    ..##.    #####
//    #...#    ..#..    .#...    ....#    ...#.
//    #...#    ..#..    #....    #...#    ...#.
//    .###.    .###.    #####    .###.    ...#.
//
//  5:#####  6:.###.  7:#####  8:.###.  9:.###.
//    #....    #....    ....#    #...#    #...#
//    #....    #....    ...#.    #...#    #...#
//    ####.    ####.    ...#.    .###.    .####
//    ....#    #...#    ..#..    #...#    ....#
//    #...#    #...#    ..#..    #...#    #...#
//    .###.    .###.    ..#..    .###.    .###.
const uint8_t PROGMEM DIGIT_BMP[10][7] = {
  { 0x0E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E }, // 0
  { 0x04, 0x0C, 0x04, 0x04, 0x04, 0x04, 0x0E }, // 1
  { 0x0E, 0x11, 0x01, 0x06, 0x08, 0x10, 0x1F }, // 2
  { 0x0E, 0x11, 0x01, 0x06, 0x01, 0x11, 0x0E }, // 3
  { 0x02, 0x06, 0x0A, 0x12, 0x1F, 0x02, 0x02 }, // 4
  { 0x1F, 0x10, 0x1E, 0x01, 0x01, 0x11, 0x0E }, // 5
  { 0x0E, 0x10, 0x10, 0x1E, 0x11, 0x11, 0x0E }, // 6
  { 0x1F, 0x01, 0x01, 0x02, 0x04, 0x04, 0x04 }, // 7  ← 대각선 하강
  { 0x0E, 0x11, 0x11, 0x0E, 0x11, 0x11, 0x0E }, // 8
  { 0x0E, 0x11, 0x11, 0x0F, 0x01, 0x11, 0x0E }, // 9
};

// 커스텀 폰트로 한 자리 숫자를 그린다 (5×7)
void drawDigit(int x, int y, int d, uint16_t color) {
  if (d < 0 || d > 9) return;
  for (int r = 0; r < 7; r++) {
    uint8_t row = pgm_read_byte(&DIGIT_BMP[d][r]);
    for (int c = 0; c < 5; c++) {
      if (row & (1 << (4 - c))) matrix.drawPixel(x + c, y + r, color);
    }
  }
}

void drawClock(const struct tm* lt) {
  // NTP 동기 전이면 "--:--"
  if (lt->tm_year < (2020 - 1900)) {
    matrix.setTextColor(C_WHITE);
    matrix.setCursor(1, 0);
    matrix.print(F("--:--"));
    return;
  }

  // 24h → 12h 변환
  int h = lt->tm_hour % 12;
  if (h == 0) h = 12;
  int m = lt->tm_min;

  // 커스텀 5×7 비트맵 폰트로 "HH:MM" 표시
  if (h >= 10) drawDigit(1, 0, h / 10, C_CYAN);
  drawDigit(7,  0, h % 10, C_CYAN);
  drawDigit(19, 0, m / 10, C_CYAN);
  drawDigit(25, 0, m % 10, C_CYAN);

  // ── 콜론 애니메이션 ──────────────────────────────────────────────────────
  // 상단 2×2 = 4픽셀:  [0(14,1)][1(15,1)]
  //                    [2(14,2)][3(15,2)]
  // 하단 2×2 = 4픽셀:  [0(14,4)][1(15,4)]
  //                    [2(14,5)][3(15,5)]
  //
  //  0~29s : 상단 4점 활성 — 시계방향(CW)  0→1→3→2 로 노랑 1개 이동
  // 30~59s : 하단 4점 활성 — 반시계(CCW)  0→2→3→1 로 노랑 1개 이동
  // (비활성 그룹은 소등)
  static const uint8_t CW[4]  = {0, 1, 3, 2}; // 시계방향 순서
  static const uint8_t CCW[4] = {0, 2, 3, 1}; // 반시계방향 순서
  // 상단/하단 각 점의 (x, y)
  static const uint8_t UX[4] = {14, 15, 14, 15};
  static const uint8_t UY[4] = { 1,  1,  2,  2};
  static const uint8_t LX[4] = {14, 15, 14, 15};
  static const uint8_t LY[4] = { 4,  4,  5,  5};

  int s = lt->tm_sec;
  matrix.fillRect(14, 0, 2, 8, 0);   // 콜론 영역 초기화

  if (s < 30) {
    // 상단: 시계방향 노랑 1개 회전 / 하단: 전체 하늘색
    int yi = CW[s % 4];
    for (int i = 0; i < 4; i++) {
      matrix.drawPixel(UX[i], UY[i], (i == yi) ? C_YELLOW : C_CYAN);
      matrix.drawPixel(LX[i], LY[i], C_CYAN);
    }
  } else {
    // 하단: 반시계방향 노랑 1개 회전 / 상단: 전체 하늘색
    int yi = CCW[(s - 30) % 4];
    for (int i = 0; i < 4; i++) {
      matrix.drawPixel(UX[i], UY[i], C_CYAN);
      matrix.drawPixel(LX[i], LY[i], (i == yi) ? C_YELLOW : C_CYAN);
    }
  }
}

void drawScrollInfo(const struct tm* lt) {
  // ---------- 1) 날짜 "2026.04.18 " (녹색) + 요일 "Sat  " (토·일 적색) ----------
  char datePart[20];   // "2026.04.18 "
  char dayPart[8];     // "Sat  "
  static const char* const kWd[] = { "Sun","Mon","Tue","Wed","Thu","Fri","Sat" };
  bool dateValid = (lt->tm_year >= (2020 - 1900));
  if (dateValid) {
    snprintf(datePart, sizeof(datePart), "%04d.%02d.%02d ",
             lt->tm_year + 1900, lt->tm_mon + 1, lt->tm_mday);
    snprintf(dayPart,  sizeof(dayPart),  "%s  ", kWd[lt->tm_wday]);
  } else {
    snprintf(datePart, sizeof(datePart), "----  ");
    dayPart[0] = '\0';
  }
  // 토(6)·일(0) → 적색, 평일 → 녹색
  bool weekend   = dateValid && (lt->tm_wday == 0 || lt->tm_wday == 6);
  bool holiday   = dateValid && isHoliday(lt->tm_mon + 1, lt->tm_mday);
  uint16_t colWeekday = (weekend || holiday) ? C_RED : C_GREEN;

  // ---------- 2) 외부온도(wttr.in) + 실내습도(DHT11) "22C 49%  " ----------
  char prefix[24];
  if (g_w.valid) {
    int humi = isnan(g_humi) ? g_w.humiPct : (int)g_humi;  // DHT 실패 시 wttr.in 습도 fallback
    snprintf(prefix, sizeof(prefix), "%dC %d%%  ", g_w.tempC, humi);
  } else {
    snprintf(prefix, sizeof(prefix), "-- --  ");
  }

  // ---------- 3) 날씨 단어 + 온도태그 (노랑 / 비 적색) "Cloud Warm " ----------
  const char* cond = g_w.valid ? g_w.condition : "---";
  float tRef = (float)g_w.tempC;
  const char* tag = "";
  if (tRef >= 25.0f)       tag = " Warm";
  else if (tRef <= 5.0f)   tag = " Cold";
  char suffix[24];
  snprintf(suffix, sizeof(suffix), "%s%s ", cond, tag);

  bool rainy = (strcmp(cond, "Rain") == 0);
  uint16_t colPrefix = C_YELLOW;
  uint16_t colSuffix = rainy ? C_RED : C_YELLOW;
  uint16_t colIcon   = rainy ? C_RED : iconColor(cond);

  int pxDatePart = (int)strlen(datePart) * 6;
  int pxDayPart  = (int)strlen(dayPart)  * 6;
  int pxDate     = pxDatePart + pxDayPart;   // 스크롤 계산용 합계
  int pxPrefix   = (int)strlen(prefix)   * 6;
  int pxSuffix   = (int)strlen(suffix)   * 6;
  int pxIcon     = g_w.valid ? 8 : 0;
  int totalPx    = pxDate + pxPrefix + pxSuffix + pxIcon;

  int x = scrollX;

  // 날짜 부분: 항상 녹색
  matrix.setTextColor(C_GREEN);
  matrix.setCursor(x, 0);  matrix.print(datePart);  x += pxDatePart;
  // 요일 부분: 토·일 적색 / 평일 녹색
  matrix.setTextColor(colWeekday);
  matrix.setCursor(x, 0);  matrix.print(dayPart);   x += pxDayPart;

  matrix.setTextColor(colPrefix);
  matrix.setCursor(x, 0);  matrix.print(prefix);    x += pxPrefix;

  matrix.setTextColor(colSuffix);
  matrix.setCursor(x, 0);  matrix.print(suffix);    x += pxSuffix;

  if (g_w.valid) {
    drawIcon8(x, 0, iconBitmap(g_w.condition), colIcon);
  }

  // 스크롤 완료 즉시 플래그 세팅 → drawFrame 에서 시계로 바로 전환
  if (scrollX > -totalPx) scrollX--;
  else scrollDone = true;
}

void drawWiFiQuestion() {
  // 1 Hz on/off  (500ms on, 500ms off)
  if ((millis() / 500) & 1) return;
  matrix.setTextColor(C_RED);
  matrix.setCursor(2, 0);
  matrix.print(F("WiFi ?"));
}
