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

#define WEATHER_INTERVAL_MS   60000UL     // 1분 (기온 ta)
#define WEATHER_WW_INTERVAL   300000UL    // 5분 (날씨코드 ww)
#define NTP_INTERVAL_MS       30000UL     // 30초 NTP 재동기
#define DHT_INTERVAL_MS       2500UL
#define CDS_INTERVAL_MS       500UL
#define WIFI_CHECK_INTERVAL   5000UL
#define FRAME_INTERVAL_MS     50UL
#define RSSI_THRESHOLD        (-75)       // dBm, 이 이하면 더 좋은 AP 탐색

// CDS raw 값이 이 이상이면 야간 모드 (주위 조명 꺼진 상태 → 시계만 표시)
#define CDS_NIGHT_THRESHOLD   960

#define CYCLE_MS          30000UL   // 전체 30초 주기
#define CLOCK_PHASE_MS    15000UL   // 0~15s  : 시계
                                    // 15~30s : 통합 스크롤 (날짜+온습도+날씨, 15s)

#define KMA_AUTH_KEY  "sQokQxRvTTiKJEMUb904bA"

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
float    g_lat         = 37.5665f;  // 위도 기본값(서울), ip-api.com으로 갱신
float    g_lon         = 126.9780f; // 경도 기본값(서울)

char     g_ssid2[33]   = {0};    // 2번째 AP SSID (EEPROM)
char     g_pass2[65]   = {0};    // 2번째 AP 비밀번호 (EEPROM)

// 공휴일 (date.nager.at, 최대 30개, 연초 자동 갱신)
struct HoliDay { uint8_t mon; uint8_t day; };
HoliDay  g_hols[30];
uint8_t  g_holCount    = 0;
int      g_holYear     = 0;      // 0 = 미취득

unsigned long tWeather   = 0;
unsigned long tWeatherWW = 0;
unsigned long tHoliday   = 0;   // 공휴일 체크 타이머 (매 루프 실행 방지)
unsigned long tNTP     = 0;
unsigned long tDHT     = 0;
unsigned long tCDS     = 0;
unsigned long tWiFi    = 0;
unsigned long tFrame   = 0;
bool     g_ntpSynced   = false;   // NTP 최소 1회 동기 완료 플래그

int      scrollX       = MATRIX_W;   // 통합 스크롤
bool     scrollDone    = false;       // 스크롤 완료 플래그

// --------------------------- 전방 선언 ---------------------------------------
void loadAP2();
void saveAP2();
void connectWiFi();
void fetchCityByIP();
void fetchWeatherWttr();        // wttr.in HTTP (SSL 불필요, 무료)
void fetchTempKMA();            // 기상청 KMA (auth key 있을 때 사용 가능)
void fetchCondKMA();
float fetchKMAObs(const char* obs, const char* stm1, const char* stm2);
float parseKMAValue(const String& body);
const char* wwToCondition(int ww);
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
// BOOT 화면 — "BOOT" + 오른쪽 황색 2×2 점 토글 (살아있음 표시)
// =============================================================================
static bool s_bootDot = false;
void showBoot() {
  s_bootDot = !s_bootDot;
  matrix.fillScreen(0);
  matrix.setTextColor(C_CYAN);
  matrix.setCursor(2, 0);
  matrix.print(F("BOOT"));
  if (s_bootDot) {
    // "BOOT" 끝(x≈26) 오른쪽에 2×2 황색 점
    matrix.drawPixel(28, 3, C_YELLOW);
    matrix.drawPixel(29, 3, C_YELLOW);
    matrix.drawPixel(28, 4, C_YELLOW);
    matrix.drawPixel(29, 4, C_YELLOW);
  }
  matrix.show();
}

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
  showBoot();   // BOOT + 황색 점 ON

  connectWiFi();
  showBoot();   // 점 OFF — WiFi 완료

  // NTP 시작 (configTime 은 내부적으로 SNTP 폴링)
  configTime(TZ_OFFSET_SEC, DST_OFFSET_SEC,
             "pool.ntp.org", "time.google.com", "kr.pool.ntp.org");

  if (g_wifiOK) {
    fetchCityByIP();

    // NTP 동기 대기 (최대 10초) — configTime() 직후 바로 조회하면 time()=0
    Serial.print(F("[NTP] 동기 대기"));
    unsigned long ntpWait = millis();
    while (time(nullptr) < 1577836800UL && millis() - ntpWait < 10000) {
      delay(200);
      Serial.print('.');
      showBoot();   // 점 토글로 살아있음 표시
    }
    Serial.println();

    if (time(nullptr) > 1577836800UL) {
      g_ntpSynced = true;
      Serial.println(F("[NTP] 동기 완료 (setup)"));
      fetchWeatherWttr();        // 기온+날씨 (wttr.in HTTP, SSL 없음)
    } else {
      Serial.println(F("[NTP] 타임아웃 — loop 에서 재시도"));
    }
    fetchHolidays();         // 한국 공휴일 취득
  }
  tWeather   = millis();
  tWeatherWW = millis();
}

// =============================================================================
// LOOP
// =============================================================================
void loop() {
  unsigned long now = millis();

  // WiFi 상태 확인: 연결된 경우 그냥 pass, 끊긴 경우만 재스캔
  // (RSSI 기반 능동 AP 전환 제거 → g_wifiMulti.run() 이 5초마다 블로킹하는 문제 해결)
  if (now - tWiFi >= WIFI_CHECK_INTERVAL) {
    tWiFi = now;
    bool prevOK = g_wifiOK;
    if (WiFi.status() == WL_CONNECTED) {
      g_wifiOK = true;
    } else {
      Serial.println(F("[WiFi] 끊김 → 재스캔"));
      g_wifiOK = (g_wifiMulti.run(8000) == WL_CONNECTED);
      if (g_wifiOK)
        Serial.printf("[WiFi] 재접속 → %s\n", WiFi.SSID().c_str());
      else
        Serial.println(F("[WiFi] 재접속 실패 → 내부 클록으로 시간 표시"));
    }
    // WiFi 복구 시에만 NTP 재동기 요청
    // (loop 안에서 configTime() 반복 호출 금지: SNTP 리셋으로 time()=0 이 되어 시계 멈춤 유발)
    if (g_wifiOK && !prevOK) {
      configTime(TZ_OFFSET_SEC, DST_OFFSET_SEC,
                 "pool.ntp.org", "time.google.com", "kr.pool.ntp.org");
      Serial.println(F("[NTP] WiFi 복구 → NTP 재요청"));
    }
  }

  // NTP 첫 동기 감지 → 즉시 날씨 취득
  if (!g_ntpSynced && time(nullptr) > 1577836800UL) {
    g_ntpSynced = true;
    Serial.println(F("[NTP] 첫 동기 완료 — 즉시 날씨 취득"));
    if (g_wifiOK) {
      fetchWeatherWttr();
      tWeatherWW = millis();
    }
  }

  // 10초마다 핵심 상태 진단 출력 (KMA 미작동 원인 추적용)
  static unsigned long tDbg = 0;
  if (now - tDbg >= 10000) {
    tDbg = now;
    Serial.printf("[STATUS] wifiOK=%d ntpSync=%d wValid=%d heap=%u  tWW=%lus/%lus\n",
                  g_wifiOK, g_ntpSynced, g_w.valid, ESP.getFreeHeap(),
                  (now - tWeatherWW) / 1000,
                  (g_w.valid ? WEATHER_WW_INTERVAL : WEATHER_INTERVAL_MS) / 1000);
  }

  if (now - tDHT >= DHT_INTERVAL_MS) {
    tDHT = now;
    readDHT();
  }

  if (now - tCDS >= CDS_INTERVAL_MS) {
    tCDS = now;
    updateBrightness();
  }

  // Open-Meteo 날씨 갱신
  //   wValid=false 동안 : 60초 재시도 (빠른 복구)
  //   wValid=true  이후 : 5분 주기 갱신
  {
    unsigned long wIv = g_w.valid ? WEATHER_WW_INTERVAL : WEATHER_INTERVAL_MS;
    if (g_wifiOK && g_ntpSynced && (now - tWeatherWW >= wIv)) {
      tWeatherWW = now;
      fetchWeatherWttr();
    }
  }

  // 공휴일: 1시간 주기로 연도 변경 감지 (매 루프 실행 방지)
  if (g_wifiOK && g_ntpSynced && (now - tHoliday >= 3600000UL)) {
    tHoliday = now;
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
// IP → 도시 이름 + 위도/경도  (ip-api.com, HTTP, 키 불필요)
// =============================================================================
void fetchCityByIP() {
  WiFiClient client;
  HTTPClient http;
  if (!http.begin(client, "http://ip-api.com/json/?fields=status,city,lat,lon")) return;
  http.setTimeout(8000);
  int code = http.GET();
  if (code == 200) {
    StaticJsonDocument<256> doc;
    if (!deserializeJson(doc, http.getStream())) {
      const char* st = doc["status"] | "";
      const char* ct = doc["city"]   | "";
      if (strcmp(st, "success") == 0) {
        if (ct[0]) strlcpy(g_w.city, ct, sizeof(g_w.city));
        g_lat = doc["lat"] | g_lat;
        g_lon = doc["lon"] | g_lon;
        Serial.printf("[IP] city=%s  lat=%.4f  lon=%.4f\n", g_w.city, g_lat, g_lon);
      }
    }
  } else {
    Serial.printf("[IP] http=%d\n", code);
  }
  http.end();
}

// =============================================================================
// ww 현천코드 → 날씨 조건 문자열  (WMO 코드 기준)
// =============================================================================
const char* wwToCondition(int ww) {
  if (ww <  0)  return "Cloud";
  if (ww <= 2)  return "Sunny";   // 맑음
  if (ww <= 9)  return "Cloud";   // 구름많음~흐림
  if (ww <= 19) return "Fog";     // 박무·안개·번개
  if (ww <= 49) return "Fog";     // 안개류
  if (ww <= 69) return "Rain";    // 이슬비·비
  if (ww <= 79) return "Snow";    // 눈
  if (ww <= 89) return "Rain";    // 소나기
  return "Storm";                  // 뇌우
}

// =============================================================================
// KMA 응답 파싱: 마지막 데이터 행의 마지막 컬럼 값 추출
//   형식: "#START7777 ... # 헤더 ... YYYYMMDDHHMM STNID VALUE #END7777"
// =============================================================================
float parseKMAValue(const String& body) {
  float val = NAN;
  int pos = 0;
  while (pos < (int)body.length()) {
    int nl = body.indexOf('\n', pos);
    if (nl < 0) nl = body.length();
    String line = body.substring(pos, nl);
    pos = nl + 1;
    line.trim();
    if (line.length() == 0 || line.startsWith("#")) continue;
    // 공백 정규화 후 마지막 토큰 추출
    while (line.indexOf("  ") >= 0) line.replace("  ", " ");
    int sp = line.lastIndexOf(' ');
    String vs = (sp >= 0) ? line.substring(sp + 1) : line;
    vs.trim();
    // 숫자 형식 검증: 숫자 또는 '-' 로 시작해야 유효
    if (vs.length() == 0) continue;
    char c0 = vs.charAt(0);
    if (!isdigit((unsigned char)c0) && c0 != '-') continue;
    float v = vs.toFloat();
    if (v > -999.0f) val = v;   // -9999: 결측값 제외
  }
  return val;
}

// =============================================================================
// 기상청 API Hub 단일 관측 요소 취득
//   obs  : "ta"(기온), "hm"(습도), "ww"(현천코드)
//   stm1 : 시작시각 YYYYMMDDHHMM (KST)
//   stm2 : 종료시각 YYYYMMDDHHMM (KST)
// =============================================================================
float fetchKMAObs(const char* obs, const char* stm1, const char* stm2) {
  Serial.printf("\n[KMA>>>] obs=%s  heap=%u\n", obs, ESP.getFreeHeap());

  WiFiClientSecure client;
  client.setInsecure();
  // 4096: KMA 정부 인증서 체인(보통 3~5KB) SSL 핸드셰이크 수용
  client.setBufferSizes(4096, 1024);

  HTTPClient http;
  String url = String("https://apihub.kma.go.kr/api/typ01/cgi-bin/url/nph-sfc_obs_nc_pt_api?obs=") +
               obs +
               "&tm1=" + stm1 + "&tm2=" + stm2 +
               "&itv=10" +
               "&lon=" + String(g_lon, 4) +
               "&lat=" + String(g_lat, 4) +
               "&authKey=" + KMA_AUTH_KEY;

  Serial.printf("[KMA] URL: %s\n", url.c_str());

  if (!http.begin(client, url)) {
    Serial.println(F("[KMA] http.begin FAIL"));
    return NAN;
  }
  http.setTimeout(15000);
  http.setUserAgent("curl/7.68");

  float result = NAN;
  int code = http.GET();
  Serial.printf("[KMA/%s] http=%d\n", obs, code);
  if (code == 200) {
    String body = http.getString();
    Serial.printf("[KMA/%s] body(%d): %s\n", obs, body.length(), body.c_str());
    result = parseKMAValue(body);
    Serial.printf("[KMA/%s] value=%.1f\n", obs, result);
  } else {
    Serial.printf("[KMA/%s] http=%d\n", obs, code);
  }
  http.end();
  client.stop();
  delay(200);   // SSL 버퍼 해제 대기
  return result;
}

// =============================================================================
// 기상청 날씨 취득 — 기온 (ta, 1분 주기)
//   HTTPS 요청 1회만 → 최대 8초 블로킹, 콜론 애니메이션 영향 최소화
// =============================================================================
static void makeKMAWindow(char* stm1, char* stm2, size_t sz) {
  time_t now_t = time(nullptr);
  struct tm lt2, lt1;
  localtime_r(&now_t,        &lt2);
  time_t t1 = now_t - 7200;
  localtime_r(&t1,           &lt1);
  strftime(stm2, sz, "%Y%m%d%H%M", &lt2);
  strftime(stm1, sz, "%Y%m%d%H%M", &lt1);
}

void fetchTempKMA() {
  time_t now_t = time(nullptr);
  if (now_t < 100000UL) { Serial.println(F("[KMA] NTP not ready")); return; }

  char stm1[13], stm2[13];
  makeKMAWindow(stm1, stm2, sizeof(stm1));
  Serial.printf("[KMA/ta] window %s ~ %s\n", stm1, stm2);

  float ta = fetchKMAObs("ta", stm1, stm2);
  if (!isnan(ta)) {
    g_w.tempC = (int)roundf(ta);
    g_w.valid = true;
    Serial.printf("[KMA/ta] %s  %d'C\n", g_w.city, g_w.tempC);
  } else {
    Serial.println(F("[KMA/ta] missing, keep previous"));
  }
}

// =============================================================================
// 기상청 날씨 취득 — 날씨코드 (ww, 5분 주기)
// =============================================================================
void fetchCondKMA() {
  time_t now_t = time(nullptr);
  if (now_t < 100000UL) { Serial.println(F("[KMA] NTP not ready")); return; }

  char stm1[13], stm2[13];
  makeKMAWindow(stm1, stm2, sizeof(stm1));
  Serial.printf("[KMA/ww] window %s ~ %s\n", stm1, stm2);

  float ww = fetchKMAObs("ww", stm1, stm2);
  strlcpy(g_w.condition, wwToCondition(isnan(ww) ? -1 : (int)ww), sizeof(g_w.condition));
  Serial.printf("[KMA/ww] code=%d  -> %s\n", isnan(ww)?-1:(int)ww, g_w.condition);
}

// =============================================================================
// wttr.in 날씨 취득 (HTTP, SSL 없음, 무료, 인증키 불필요)
//   API: http://wttr.in/{lat},{lon}?format=j1
//   응답: {"current_condition":[{"temp_C":"15","weatherDesc":[{"value":"Partly cloudy"}]}]}
// =============================================================================
void fetchWeatherWttr() {
  char locStr[32];
  snprintf(locStr, sizeof(locStr), "%.4f,%.4f", g_lat, g_lon);
  Serial.printf("[WTTR] loc=%s  heap=%u\n", locStr, ESP.getFreeHeap());

  WiFiClient client;   // HTTP — SSL 불필요
  HTTPClient http;
  String url = String("http://wttr.in/") + locStr + "?format=j1";
  Serial.printf("[WTTR] %s\n", url.c_str());

  if (!http.begin(client, url)) {
    Serial.println(F("[WTTR] begin FAIL"));
    return;
  }
  http.setTimeout(10000);
  http.setUserAgent("curl/7.68");
  http.setFollowRedirects(HTTPC_FORCE_FOLLOW_REDIRECTS);

  int code = http.GET();
  Serial.printf("[WTTR] http=%d\n", code);
  if (code == 200) {
    StaticJsonDocument<128> filter;
    filter["current_condition"][0]["temp_C"]                    = true;
    filter["current_condition"][0]["weatherDesc"][0]["value"]   = true;
    DynamicJsonDocument doc(512);
    DeserializationError err = deserializeJson(doc, http.getStream(),
                                               DeserializationOption::Filter(filter));
    if (!err) {
      const char* tStr = doc["current_condition"][0]["temp_C"] | "0";
      const char* desc = doc["current_condition"][0]["weatherDesc"][0]["value"] | "";
      g_w.tempC = atoi(tStr);
      strlcpy(g_w.condition, descToCondition(String(desc)), sizeof(g_w.condition));
      g_w.valid = true;
      Serial.printf("[WTTR] OK  %s  %d'C  %s  (raw:%s)\n",
                    g_w.city, g_w.tempC, g_w.condition, desc);
    } else {
      Serial.printf("[WTTR] parse err: %s\n", err.c_str());
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
  client.setBufferSizes(4096, 1024);

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

  // WiFi 없고 NTP 동기도 안 된 경우 → "WiFi ?" 표시
  // NTP 동기 완료 후에는 WiFi 없어도 내부 클록으로 정상 표시
  if (!g_wifiOK && !g_ntpSynced) {
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
    // 0~29s: 위쪽 CW 노랑 1개 회전 / 아래쪽 전체 하늘색 (점 유지)
    int yi = CW[s % 4];
    for (int i = 0; i < 4; i++) {
      matrix.drawPixel(UX[i], UY[i], (i == yi) ? C_YELLOW : C_CYAN);
      matrix.drawPixel(LX[i], LY[i], C_CYAN);
    }
  } else {
    // 30~59s: 아래쪽 CCW 노랑 1개 회전 / 위쪽 전체 하늘색 (점 유지)
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
    snprintf(prefix, sizeof(prefix), "%d'C %d%%  ", g_w.tempC, humi);
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
  static const char msg[] = "WiFi failed - connect phone to IoTClock-Setup   ";
  static int sx = MATRIX_W;

  // 1픽셀씩 스크롤 (drawFrame 50ms 주기에 의해 자동 호출)
  const int msgPx = (int)(sizeof(msg) - 1) * 6;  // 6px per char (5+1)
  if (--sx < -msgPx) sx = MATRIX_W;              // 끝까지 가면 처음으로

  matrix.setTextColor(C_RED);
  matrix.setCursor(sx, 0);
  matrix.print(msg);
}
