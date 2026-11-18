// Test dla TTGO T-Display (ESP32, ST7789 135x240): animowana maskotka Claude Code ("Clawd").
// Przycisk GPIO0  (lewy)  - przełącza: stoi / spaceruje
// Przycisk GPIO35 (prawy) - następna wiadomość
#include <Arduino.h>
#include <TFT_eSPI.h>

#define CPU_MHZ 80  // 80 MHz wystarcza do animacji, mniej prądu i ciepła niż 240 MHz

#define RGB565(r, g, b) ((((r) & 0xF8) << 8) | (((g) & 0xFC) << 3) | ((b) >> 3))

static const uint16_t COL_BG     = RGB565(26, 25, 23);
static const uint16_t COL_ORANGE = RGB565(217, 119, 87);
static const uint16_t COL_DARK   = RGB565(20, 20, 19);
static const uint16_t COL_SHADOW = RGB565(48, 46, 43);
static const uint16_t COL_TEXT   = RGB565(250, 249, 245);
static const uint16_t COL_DIM    = RGB565(160, 155, 145);

static const int SCR_W = 240;
static const int SCR_H = 135;
static const int U = 8;  // rozmiar "piksela" maskotki

static const int BTN_MODE = 0;
static const int BTN_MSG  = 35;

static const char *MESSAGES[] = {
  "Thinking...",
  "Hello from ESP32!",
  "Clauding...",
  "Hi, I'm Claude :)",
};
static const int MSG_COUNT = sizeof(MESSAGES) / sizeof(MESSAGES[0]);

TFT_eSPI tft;
TFT_eSprite spr(&tft);

bool walking = false;
int msgIndex = 0;
int msgChars = 0;
uint32_t lastTypeMs = 0;
uint32_t msgDoneMs = 0;

float walkX = SCR_W / 2;
int walkDir = 1;

// --- Clawd ------------------------------------------------------------------
// cx - środek, top - górna krawędź ciała. Wymiary: ciało 9U x 5U, nogi 2U, ręce 2U x 2U.
void drawClawd(int cx, int top, int legStep, bool blink, int armUp, int lookDir) {
  const int bw = 9 * U, bh = 5 * U;
  const int left = cx - bw / 2;

  // ręce (armUp: 0 - lewa w górze, 1 - prawa w górze, -1 - obie na dole)
  int armL = top + 2 * U - (armUp == 0 ? U : 0);
  int armR = top + 2 * U - (armUp == 1 ? U : 0);
  spr.fillRect(left - 2 * U, armL, 2 * U, 2 * U, COL_ORANGE);
  spr.fillRect(left + bw,    armR, 2 * U, 2 * U, COL_ORANGE);

  // ciało
  spr.fillRect(left, top, bw, bh, COL_ORANGE);

  // nogi: 4 sztuki, w trybie chodzenia na przemian krótsze
  for (int i = 0; i < 4; i++) {
    int lx = left + (1 + 2 * i) * U;
    int lh = 2 * U;
    if (legStep >= 0) lh = ((i + legStep) % 2 == 0) ? 2 * U : U;
    spr.fillRect(lx, top + bh, U, lh, COL_ORANGE);
  }

  // oczy
  int eyeH = blink ? U / 2 : 2 * U;
  int eyeY = top + 2 * U + (blink ? U : 0);
  int shift = lookDir * (U / 2);
  spr.fillRect(left + 2 * U + shift, eyeY, U, eyeH, COL_DARK);
  spr.fillRect(left + 6 * U + shift, eyeY, U, eyeH, COL_DARK);
}

void drawSparkle(int x, int y, int phase) {
  // phase 0..3: pusty, mała kropka, plus, duży plus
  if (phase == 1) spr.drawPixel(x, y, COL_DIM);
  if (phase >= 2) {
    spr.drawFastHLine(x - 2, y, 5, COL_ORANGE);
    spr.drawFastVLine(x, y - 2, 5, COL_ORANGE);
  }
  if (phase == 3) {
    spr.drawPixel(x - 3, y - 3, COL_DIM);
    spr.drawPixel(x + 3, y + 3, COL_DIM);
  }
}

void nextMessage() {
  msgIndex = (msgIndex + 1) % MSG_COUNT;
  msgChars = 0;
  msgDoneMs = 0;
}

void handleButtons(uint32_t now) {
  static bool lastMode = HIGH, lastMsg = HIGH;
  static uint32_t lastPress = 0;
  bool m = digitalRead(BTN_MODE);
  bool g = digitalRead(BTN_MSG);
  if (now - lastPress > 200) {
    if (lastMode == HIGH && m == LOW) { walking = !walking; lastPress = now; }
    if (lastMsg == HIGH && g == LOW)  { nextMessage();      lastPress = now; }
  }
  lastMode = m;
  lastMsg = g;
}

void setup() {
  setCpuFrequencyMhz(CPU_MHZ);
  btStop();  // WiFi i Bluetooth nie są używane; WiFi startuje tylko na żądanie, więc wystarczy to
  Serial.begin(115200);
  pinMode(BTN_MODE, INPUT_PULLUP);
  pinMode(BTN_MSG, INPUT);  // GPIO35 jest tylko wejściem, ma zewnętrzny pull-up na płytce

  tft.init();
  tft.setRotation(1);  // poziomo, 240x135 (przyciski po prawej)
  tft.fillScreen(COL_BG);

  if (spr.createSprite(SCR_W, SCR_H) == nullptr) {
    Serial.println("Brak pamieci na sprite!");
  }
  spr.setTextDatum(TL_DATUM);
}

void loop() {
  static uint32_t lastFrame = 0;
  uint32_t now = millis();
  if (now - lastFrame < 33) {  // ~30 FPS; między klatkami procesor odpoczywa
    delay(5);
    return;
  }
  lastFrame = now;

  handleButtons(now);
  float t = now / 1000.0f;

  // --- stan animacji ---
  int bob = (int)(sinf(t * 4.0f) * (walking ? 3 : 2));
  bool blink = (now % 3200) < 140;
  int armUp = -1;
  int legStep = -1;
  int lookDir = 0;

  if (walking) {
    walkX += walkDir * 1.6f;
    if (walkX > SCR_W - 6 * U) walkDir = -1;
    if (walkX < 6 * U)         walkDir = 1;
    legStep = (now / 160) % 2;
    lookDir = walkDir;
  } else {
    walkX += ((SCR_W / 2) - walkX) * 0.1f;  // wróć na środek
    if ((now % 5000) > 3600) armUp = (now / 220) % 2;  // co jakiś czas macha
  }

  // --- rysowanie ---
  spr.fillSprite(COL_BG);

  // iskierki
  drawSparkle(28,  22, (now / 300) % 4);
  drawSparkle(212, 30, (now / 300 + 2) % 4);
  drawSparkle(190, 66, (now / 300 + 1) % 4);
  drawSparkle(40,  62, (now / 300 + 3) % 4);

  // cień
  int shW = 9 * U - bob * 3;
  spr.fillEllipse((int)walkX, 75, shW / 2, 3, COL_SHADOW);

  drawClawd((int)walkX, 14 + bob, legStep, blink, armUp, lookDir);

  // napis
  spr.setTextColor(COL_TEXT);
  spr.drawString("Claude", 8, 82, 4);

  // maszyna do pisania
  const char *msg = MESSAGES[msgIndex];
  int len = strlen(msg);
  if (msgChars < len) {
    if (now - lastTypeMs > 70) { msgChars++; lastTypeMs = now; if (msgChars == len) msgDoneMs = now; }
  } else if (now - msgDoneMs > 2500) {
    nextMessage();
  }
  char buf[40];
  strncpy(buf, msg, msgChars);
  buf[msgChars] = 0;
  spr.setTextColor(COL_DIM);
  int w = spr.drawString(buf, 8, 114, 2);
  if ((now / 400) % 2 == 0) spr.fillRect(8 + w + 2, 116, 7, 12, COL_ORANGE);  // kursor

  spr.pushSprite(0, 0);
}
