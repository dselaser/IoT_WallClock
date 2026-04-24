const pptxgen = require("pptxgenjs");
const pres = new pptxgen();
pres.layout = "LAYOUT_16x9";
pres.title = "IoT 스마트 벽시계";

const BK = "000000";
const DK = "222222";
const GY = "666666";
const LG = "CCCCCC";
const WH = "FFFFFF";

function line(s, x1, y, x2, w) {
  s.addShape(pres.shapes.LINE, {
    x: x1, y, w: x2 - x1, h: 0,
    line: { color: BK, width: w || 1.5, endArrowType: "triangle" }
  });
}
function hdr(s, txt) {
  s.addText(txt, { x: 0.5, y: 0.2, w: 9, h: 0.65, fontSize: 30, fontFace: "Arial Black", bold: true, color: BK });
  s.addShape(pres.shapes.RECTANGLE, { x: 0.5, y: 0.87, w: 9, h: 0.07, fill: { color: BK }, line: { color: BK, width: 0 } });
}
function box(s, x, y, w, h, lw) {
  s.addShape(pres.shapes.RECTANGLE, { x, y, w, h, fill: { color: WH }, line: { color: BK, width: lw || 1.5 } });
}

// ── Slide 1: Title ──────────────────────────────────────────────────────────
{
  const s = pres.addSlide();
  s.addShape(pres.shapes.RECTANGLE, { x: 0, y: 0, w: 10, h: 0.1, fill: { color: BK }, line: { color: BK, width: 0 } });
  s.addShape(pres.shapes.RECTANGLE, { x: 0, y: 5.52, w: 10, h: 0.1, fill: { color: BK }, line: { color: BK, width: 0 } });

  s.addText("IoT 스마트 벽시계", {
    x: 0.5, y: 1.1, w: 9, h: 1.3, fontSize: 48, fontFace: "Arial Black",
    bold: true, color: BK, align: "center"
  });
  s.addText("ESP8266  +  WS2812B 8×32 LED 매트릭스", {
    x: 0.5, y: 2.5, w: 9, h: 0.7, fontSize: 20, color: GY, align: "center"
  });

  // Feature tags
  const tags = ["시간 표시", "온습도", "wttr.in날씨", "자동밝기", "다중AP"];
  tags.forEach((t, i) => {
    const x = 0.5 + i * 1.82;
    s.addShape(pres.shapes.RECTANGLE, { x, y: 3.5, w: 1.65, h: 0.65, fill: { color: WH }, line: { color: BK, width: 1.5 } });
    s.addText(t, { x, y: 3.5, w: 1.65, h: 0.65, fontSize: 12, bold: true, color: BK, align: "center", valign: "middle" });
  });

  s.addText("Arduino .ino  ·  WiFiManager  ·  ESP8266WiFiMulti  ·  wttr.in  ·  NTP  ·  DHT11  ·  CDS  ·  date.nager.at", {
    x: 0.5, y: 4.5, w: 9, h: 0.5, fontSize: 11, color: LG, align: "center"
  });
}

// ── Slide 2: 하드웨어 구성 ─────────────────────────────────────────────────
{
  const s = pres.addSlide();
  hdr(s, "하드웨어 구성");

  // Center: ESP8266
  s.addShape(pres.shapes.RECTANGLE, { x: 3.6, y: 1.5, w: 2.8, h: 2.8, fill: { color: DK }, line: { color: BK, width: 2.5 } });
  s.addText([
    { text: "ESP8266", options: { breakLine: true } },
    { text: "(NodeMCU)", options: { breakLine: true } },
    { text: "" , options: { breakLine: true } },
    { text: "Arduino .ino", options: { breakLine: true } },
    { text: "비동기 상태 머신" }
  ], { x: 3.6, y: 1.5, w: 2.8, h: 2.8, fontSize: 14, bold: true, color: WH, align: "center", valign: "middle" });

  // Left devices
  const ldevs = [
    { label: "WS2812B\n8×32 LED 패널", pin: "D1 (GPIO5)", y: 1.5 },
    { label: "DHT11\n온습도 센서", pin: "D2 (GPIO4)", y: 2.65 },
    { label: "CDS 조도 센서\n+10k Pull-up", pin: "A0", y: 3.8 },
  ];
  ldevs.forEach(d => {
    box(s, 0.3, d.y, 2.6, 0.9, 1.5);
    s.addText(d.label, { x: 0.3, y: d.y, w: 2.6, h: 0.65, fontSize: 13, bold: true, color: BK, align: "center", valign: "middle" });
    s.addText(d.pin, { x: 0.3, y: d.y + 0.6, w: 2.6, h: 0.3, fontSize: 10, color: GY, align: "center" });
    line(s, 2.9, d.y + 0.45, 3.6, 1.2);
  });

  // Right: WiFi/Internet
  box(s, 7.1, 1.5, 2.5, 1.0, 1.5);
  s.addText("WiFi / 인터넷", { x: 7.1, y: 1.5, w: 2.5, h: 1.0, fontSize: 14, bold: true, color: BK, align: "center", valign: "middle" });

  box(s, 7.1, 2.8, 2.5, 1.5, 1.5);
  s.addText([
    { text: "NTP 서버", options: { breakLine: true } },
    { text: "ip-api.com", options: { breakLine: true } },
    { text: "wttr.in", options: { breakLine: true } },
    { text: "date.nager.at" }
  ], { x: 7.1, y: 2.8, w: 2.5, h: 1.5, fontSize: 12, color: BK, align: "center", valign: "middle" });

  line(s, 6.4, 2.0, 7.1, 1.2);
  line(s, 6.4, 3.0, 7.1, 1.2);

  // Pin labels on arrows
  s.addText("D1", { x: 2.9, y: 1.6, w: 0.5, h: 0.25, fontSize: 10, color: GY });
  s.addText("D2", { x: 2.9, y: 2.75, w: 0.5, h: 0.25, fontSize: 10, color: GY });
  s.addText("A0", { x: 2.9, y: 3.9, w: 0.5, h: 0.25, fontSize: 10, color: GY });

  // Power note
  box(s, 0.3, 5.0, 9.4, 0.45, 1);
  s.addText("WS2812B 전원: 5V 별도 공급 권장 (256 LED, 밝기 제한 시 ~1.5A)  ·  GND 공통 연결 필수", {
    x: 0.5, y: 5.05, w: 9.0, h: 0.35, fontSize: 11, color: GY, valign: "middle"
  });
}

// ── Slide 3: 주요 기능 ────────────────────────────────────────────────────
{
  const s = pres.addSlide();
  hdr(s, "주요 기능");

  const feats = [
    { n: "1", t: "WiFi 다중 AP 자동 전환", d: "Portal에서 1·2번 SSID 등록\nESP8266WiFiMulti → 신호 강한 AP 자동 접속" },
    { n: "2", t: "NTP 자동 시간 동기", d: "pool.ntp.org 접속, 한국 표준시(KST)\nUTC+9 자동 적용" },
    { n: "3", t: "IP 기반 위치 자동 감지", d: "ip-api.com → 도시명 + 위도/경도 취득\n격자 변환 없이 기상청 API 직접 활용" },
    { n: "4", t: "외부 날씨 수신 (wttr.in HTTPS)", d: "fetchWeatherWttr() — 실패시 60초/성공시 5분\n텍스트 포맷 (%t|%C) → 기온+날씨조건" },
    { n: "5", t: "온습도 혼합 표시", d: "온도: wttr.in 외부 기온 (1~5분 갱신)\n습도: DHT11 실내 센서 (2.5초 주기)" },
    { n: "6", t: "CDS 자동 밝기 + 야간 모드", d: "raw > 960 → 야간 모드 (시계만 + 밝기 1~4)\n일반: raw 350~960 → 밝기 150~5 비례 조절" },
    { n: "7", t: "공휴일 자동 표시", d: "date.nager.at API → 한국 공휴일 자동 취득\n토·일·공휴일 스크롤 날짜 적색 표시" },
  ];

  feats.forEach((f, i) => {
    const col = i < 4 ? 0 : 1;
    const row = i < 4 ? i : i - 4;
    const x = 0.4 + col * 4.9;
    const bh = 0.95, gap = 1.05;
    const y = 1.1 + row * gap;
    box(s, x, y, 4.6, bh, 1.5);
    s.addShape(pres.shapes.RECTANGLE, { x, y, w: 0.55, h: bh, fill: { color: DK }, line: { color: BK, width: 0 } });
    s.addText(f.n, { x, y, w: 0.55, h: bh, fontSize: 18, bold: true, color: WH, align: "center", valign: "middle" });
    s.addText(f.t, { x: x + 0.65, y: y + 0.08, w: 3.85, h: 0.35, fontSize: 13, bold: true, color: BK });
    s.addText(f.d, { x: x + 0.65, y: y + 0.43, w: 3.85, h: 0.47, fontSize: 10.5, color: GY });
  });
}

// ── Slide 4: WiFi 설정 + 다중AP ─────────────────────────────────────────
{
  const s = pres.addSlide();
  hdr(s, "WiFi 설정 흐름 (Captive Portal + 다중 AP)");

  const steps = [
    { t: "전원 ON", d: "AP 생성\n'IoTClock-Setup'" },
    { t: "스마트폰 연결", d: "WiFi 목록에서\nAP 선택" },
    { t: "설정 페이지", d: "브라우저 자동 팝업\n(192.168.4.1)" },
    { t: "SSID 입력", d: "1번+2번 WiFi\nSSID·비밀번호" },
    { t: "연결 완료", d: "WiFiMulti 등록\n강한 AP 자동 선택" },
  ];

  const bw = 1.5, bh = 1.5, gap = 1.85, sy = 1.3;
  steps.forEach((st, i) => {
    const x = 0.35 + i * gap;
    s.addShape(pres.shapes.OVAL, { x, y: sy, w: bw, h: bh, fill: { color: WH }, line: { color: BK, width: 2 } });
    s.addText(`${i + 1}`, { x, y: sy + 0.08, w: bw, h: 0.4, fontSize: 20, bold: true, color: BK, align: "center" });
    s.addText(st.t, { x, y: sy + 0.42, w: bw, h: 0.4, fontSize: 12, bold: true, color: BK, align: "center" });
    s.addText(st.d, { x, y: sy + 0.82, w: bw, h: 0.58, fontSize: 10, color: GY, align: "center" });
    if (i < steps.length - 1) line(s, x + bw, sy + bh / 2, x + gap, sy + bh / 2, 1.5);
  });

  // WiFiMulti 동작 설명
  box(s, 0.4, 3.1, 9.2, 1.0, 1.5);
  s.addShape(pres.shapes.RECTANGLE, { x: 0.4, y: 3.1, w: 9.2, h: 0.38, fill: { color: DK }, line: { color: BK, width: 0 } });
  s.addText("ESP8266WiFiMulti  자동 전환 동작", { x: 0.4, y: 3.1, w: 9.2, h: 0.38, fontSize: 13, bold: true, color: WH, align: "center", valign: "middle" });

  const multis = [
    { c: "AP1 신호 강함", a: "1번 AP 유지 접속" },
    { c: "AP1 신호 약화 / 끊김", a: "5초 이내 재스캔 → 신호 강한 AP 자동 전환" },
    { c: "2번 AP 미입력", a: "1번 AP만으로 정상 동작  (하위 호환)" },
  ];
  multis.forEach((m, i) => {
    const my = 3.52 + i * 0.2;
    s.addText("▶ " + m.c, { x: 0.6, y: my, w: 3.0, h: 0.2, fontSize: 11, bold: true, color: BK });
    s.addText("→  " + m.a, { x: 3.6, y: my, w: 5.8, h: 0.2, fontSize: 11, color: DK });
  });

  // 2번 AP EEPROM 저장 안내
  box(s, 0.4, 4.25, 9.2, 0.55, 1);
  s.addText("2번째 AP 정보는 EEPROM에 저장  |  전원 재시작 후에도 유지  |  Portal 재접속으로 변경 가능", {
    x: 0.6, y: 4.32, w: 8.8, h: 0.4, fontSize: 11.5, color: GY, valign: "middle"
  });

  box(s, 0.4, 4.9, 9.2, 0.5, 1.5);
  s.addText("LED 표시 →  WiFi ?  (적색 1Hz 깜빡임)   ←  WiFi 미접속 시", {
    x: 0.4, y: 4.9, w: 9.2, h: 0.5, fontSize: 13, color: BK, align: "center", valign: "middle"
  });
}

// ── Slide 5: 화면 표시 사이클 ─────────────────────────────────────────────
{
  const s = pres.addSlide();
  hdr(s, "화면 표시 사이클 (30초)");

  // Timeline
  const ty = 1.3, th = 0.85;
  s.addShape(pres.shapes.RECTANGLE, { x: 0.5, y: ty, w: 4.5, h: th, fill: { color: DK }, line: { color: BK, width: 0 } });
  s.addText("시 계  HH:MM  (0 ~ 15초)", { x: 0.5, y: ty, w: 4.5, h: th, fontSize: 14, bold: true, color: WH, align: "center", valign: "middle" });

  s.addShape(pres.shapes.RECTANGLE, { x: 5.0, y: ty, w: 4.5, h: th, fill: { color: GY }, line: { color: BK, width: 0 } });
  s.addText("통합 스크롤  (15 ~ 30초)", { x: 5.0, y: ty, w: 4.5, h: th, fontSize: 14, bold: true, color: WH, align: "center", valign: "middle" });

  ["0s", "15s", "30s"].forEach((t, i) => {
    const x = [0.4, 4.85, 9.35][i];
    s.addText(t, { x, y: ty + th + 0.08, w: 0.6, h: 0.3, fontSize: 11, color: GY });
  });

  // Left detail: 시계
  box(s, 0.4, 2.55, 4.3, 2.7, 2);
  s.addShape(pres.shapes.RECTANGLE, { x: 0.4, y: 2.55, w: 4.3, h: 0.45, fill: { color: DK }, line: { color: BK, width: 0 } });
  s.addText("⏱  시계 표시", { x: 0.4, y: 2.55, w: 4.3, h: 0.45, fontSize: 13, bold: true, color: WH, align: "center", valign: "middle" });
  s.addText([
    { text: "• 12시간제  HH:MM  형식", options: { breakLine: true } },
    { text: "• 출발안내판 스타일 커스텀 5×7 비트맵 폰트", options: { breakLine: true } },
    { text: "• 콜론 상단 4점 — 0~29초, 노랑 시계방향 회전", options: { breakLine: true } },
    { text: "• 콜론 하단 4점 — 30~59초, 노랑 반시계 회전", options: { breakLine: true } },
    { text: "• NTP 미동기 시:  --:--  표시", options: { breakLine: true } },
    { text: "• 야간 모드 시 시계만 표시 (스크롤 생략)" }
  ], { x: 0.6, y: 3.08, w: 3.95, h: 2.1, fontSize: 11, color: DK });

  // Right detail: 스크롤
  box(s, 5.2, 2.55, 4.3, 2.7, 2);
  s.addShape(pres.shapes.RECTANGLE, { x: 5.2, y: 2.55, w: 4.3, h: 0.45, fill: { color: GY }, line: { color: BK, width: 0 } });
  s.addText("📜  통합 스크롤 순서", { x: 5.2, y: 2.55, w: 4.3, h: 0.45, fontSize: 13, bold: true, color: WH, align: "center", valign: "middle" });

  const items = [
    { n: "①", t: "날짜 (녹색/적색)", ex: "평일→녹색  /  토·일·공휴일→적색" },
    { n: "②", t: "온도+습도 (노랑)", ex: "22C  49%  (외부기온+실내습도)" },
    { n: "③", t: "날씨 (노랑/비=적색)", ex: "Cloud  Sunny  Rain" },
    { n: "④", t: "날씨 아이콘 8×8", ex: "태양·구름·비·눈·안개·폭풍" },
  ];
  items.forEach((it, i) => {
    const iy = 3.1 + i * 0.55;
    s.addText(it.n, { x: 5.35, y: iy, w: 0.35, h: 0.45, fontSize: 13, bold: true, color: BK });
    s.addText(it.t, { x: 5.72, y: iy, w: 1.7, h: 0.45, fontSize: 12, bold: true, color: BK });
    s.addText(it.ex, { x: 7.44, y: iy, w: 1.9, h: 0.45, fontSize: 11, color: GY, italic: true });
  });
}

// ── Slide 6: 소프트웨어 구조 ────────────────────────────────────────────────
{
  const s = pres.addSlide();
  hdr(s, "소프트웨어 구조 (Arduino .ino)");

  // Main loop
  s.addShape(pres.shapes.RECTANGLE, { x: 3.3, y: 1.1, w: 3.4, h: 0.75, fill: { color: DK }, line: { color: BK, width: 2 } });
  s.addText("loop()   Arduino 메인 루프", { x: 3.3, y: 1.1, w: 3.4, h: 0.75, fontSize: 14, bold: true, color: WH, align: "center", valign: "middle" });

  // Horizontal bus
  s.addShape(pres.shapes.LINE, { x: 0.9, y: 1.85, w: 8.2, h: 0, line: { color: BK, width: 1.5 } });

  // Sub tasks — 6개를 10인치 슬라이드에 균등 배치 (박스폭 1.45, 간격 0.18)
  const tasks = [
    { l: "drawFrame()", d: "50 ms\n화면 갱신 (20fps)", x: 0.2 },
    { l: "readDHT()", d: "2.5 s\n실내 습도", x: 1.85 },
    { l: "updateBrightness()", d: "500 ms\nCDS 비례밝기", x: 3.5 },
    { l: "fetchWeatherWttr()", d: "1~5 분\n기온+날씨", x: 5.15 },
    { l: "fetchHolidays()", d: "1 시간\n공휴일 갱신", x: 6.8 },
    { l: "WiFi 체크", d: "5 s\nWiFiMulti\n재접속", x: 8.45 },
  ];

  const TW = 1.45; // 태스크 박스 폭
  tasks.forEach(t => {
    s.addShape(pres.shapes.LINE, { x: t.x + TW / 2, y: 1.85, w: 0, h: 0.4, line: { color: BK, width: 1.2 } });
    box(s, t.x, 2.25, TW, 1.1, 1.5);
    s.addText(t.l, { x: t.x + 0.04, y: 2.28, w: TW - 0.08, h: 0.45, fontSize: 8.5, bold: true, color: BK, align: "center" });
    s.addText(t.d, { x: t.x + 0.04, y: 2.73, w: TW - 0.08, h: 0.6, fontSize: 9.5, color: GY, align: "center" });
  });

  // 비블로킹 날씨 갱신 설명 배너
  box(s, 0.3, 3.45, 9.4, 0.5, 1);
  s.addShape(pres.shapes.RECTANGLE, { x: 0.3, y: 3.45, w: 2.8, h: 0.5, fill: { color: DK }, line: { color: BK, width: 0 } });
  s.addText("날씨 갱신 전략", { x: 0.3, y: 3.45, w: 2.8, h: 0.5, fontSize: 12, bold: true, color: WH, align: "center", valign: "middle" });
  s.addText("fetchWeatherWttr() — wValid=false 60초 재시도 / wValid=true 5분 정기 갱신  |  wttr.in HTTPS + 텍스트 포맷 (?format=%t|%C)  |  온도=wttr.in / 습도=DHT11 실내 센서", {
    x: 3.2, y: 3.47, w: 6.3, h: 0.45, fontSize: 10, color: DK, valign: "middle"
  });

  // drawFrame internals
  box(s, 0.3, 4.05, 9.4, 1.5, 2);
  s.addShape(pres.shapes.RECTANGLE, { x: 0.3, y: 4.05, w: 9.4, h: 0.38, fill: { color: DK }, line: { color: BK, width: 0 } });
  s.addText("drawFrame()  내부 분기 흐름", { x: 0.3, y: 4.05, w: 9.4, h: 0.38, fontSize: 12, bold: true, color: WH, align: "center", valign: "middle" });

  const flows = [
    { c: "WiFi 미접속?", a: "'WiFi ?' 적색 1Hz 깜빡임 → return" },
    { c: "야간 모드?", a: "CDS raw > 960 → 시계만 + 밝기 1~4 비례 (스크롤 생략)" },
    { c: "phase < 15s?", a: "시계 표시  HH:MM  (커스텀 폰트, 콜론 4점 회전)" },
    { c: "phase >= 15s?", a: "날짜→기온+습도→날씨+아이콘 통합 스크롤" },
    { c: "스크롤 완료?", a: "즉시 시계로 전환 (scrollDone 플래그)" },
  ];
  flows.forEach((f, i) => {
    const fy = 4.47 + i * 0.22;
    s.addText("▶ " + f.c, { x: 0.5, y: fy, w: 2.6, h: 0.24, fontSize: 10.5, bold: true, color: BK });
    s.addText("→  " + f.a, { x: 3.1, y: fy, w: 6.4, h: 0.24, fontSize: 10.5, color: DK });
  });
}

// ── Slide 7: 날씨 수신 경로 (wttr.in) ────────────────────────────────────
{
  const s = pres.addSlide();
  hdr(s, "날씨 정보 수신 경로  (wttr.in)");

  const nodes = [
    { l: "ESP8266", s: "전원 ON\n인터넷 접속", x: 0.3 },
    { l: "ip-api.com", s: "IP → 도시명\n(Seoul 등)", x: 2.35 },
    { l: "wttr.in\n(HTTPS)", s: "기온 + 날씨 설명\n텍스트 포맷", x: 4.4 },
    { l: "descToCondition()", s: "영문 설명\n→ 조건 매핑", x: 6.45 },
    { l: "LED 표시", s: "텍스트 + 아이콘\n스크롤", x: 8.5 },
  ];

  const bw = 1.8, bh = 1.5, ny = 1.6;
  nodes.forEach((n, i) => {
    box(s, n.x, ny, bw, bh, 2);
    s.addText(n.l, { x: n.x, y: ny + 0.1, w: bw, h: 0.55, fontSize: 12, bold: true, color: BK, align: "center", valign: "middle" });
    s.addText(n.s, { x: n.x, y: ny + 0.65, w: bw, h: 0.8, fontSize: 11, color: GY, align: "center" });
    if (i < nodes.length - 1) line(s, n.x + bw, ny + bh / 2, n.x + bw + 0.55, ny + bh / 2, 1.5);
  });

  // 조건 매핑 표
  const condRows = [
    "partly cloudy  → PCloud",
    "rain/drizzle   → Rain",
    "snow/sleet     → Snow",
    "fog/mist/haze  → Fog",
    "thunder/storm  → Storm",
    "sunny/clear    → Sunny",
  ];
  box(s, 6.45, ny + bh + 0.2, bw, 1.55, 1);
  condRows.forEach((r, i) => {
    s.addText(r, { x: 6.55, y: ny + bh + 0.28 + i * 0.24, w: 1.6, h: 0.22, fontSize: 9, color: DK, fontFace: "Consolas" });
  });

  // Tech details
  const techs = [
    { t: "HTTPS 방식", d: "WiFiClientSecure\n+ setInsecure()\nCloudflare MFLN\n→ 4096 RX 버퍼" },
    { t: "텍스트 포맷", d: "?format=%t|%C\n응답 ~30 bytes\n→ split '|'\nJSON 파싱 없음" },
    { t: "갱신 전략", d: "wValid=false:\n60초 재시도\nwValid=true:\n5분 정기 갱신" },
    { t: "온습도 분리", d: "온도:\nwttr.in 외부값\n습도:\nDHT11 실내 센서" },
  ];
  techs.forEach((t, i) => {
    const tx = 0.4 + i * 2.4;
    box(s, tx, 3.5, 2.2, 1.9, 1.5);
    s.addShape(pres.shapes.RECTANGLE, { x: tx, y: 3.5, w: 2.2, h: 0.4, fill: { color: DK }, line: { color: BK, width: 0 } });
    s.addText(t.t, { x: tx, y: 3.5, w: 2.2, h: 0.4, fontSize: 12, bold: true, color: WH, align: "center", valign: "middle" });
    s.addText(t.d, { x: tx + 0.1, y: 3.95, w: 2.0, h: 1.4, fontSize: 11, color: DK });
  });
}

pres.writeFile({ fileName: "D:/ESP32/ESP8266/IoT_WallClock_설명.pptx" })
  .then(() => console.log("Done: IoT_WallClock_설명.pptx"))
  .catch(e => console.error(e));
