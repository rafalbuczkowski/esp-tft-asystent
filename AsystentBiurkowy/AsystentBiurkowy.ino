// Asystent biurkowy dla TTGO T-Display (ESP32, ST7789 135x240).
// Po podłączeniu zasilania od razu liczy czas pracy (tryb "siedzę").
//
// Lewy przycisk  (GPIO0):
//   krótko  - następny tryb: siedzę -> stoję -> przerwa -> siedzę ...
//   długo   - przełącza ekran: główny <-> podsumowanie dnia
// Prawy przycisk (GPIO35):
//   krótko, gdy jest alert  - potwierdza (tabletka / szklanka wody) albo zamyka alert
//   krótko, bez alertu      - ręcznie zaznacza tabletkę (ekran główny)
//   długo, na ekranie głównym - cofa ostatnią tabletkę / szklankę wody
//   długo, na podsumowaniu  - nowy dzień (zeruje wszystkie liczniki)
//
// Przypomnienia: wstań (siedzenie), usiądź (stanie), woda, tabletki, oczy 20-20-20.
// Woda i oczy liczą się tylko przy biurku (nie w Przerwie). Tabletki przypominają zawsze.
// Kilka zignorowanych alertów "WSTAN" bez dotknięcia przycisku = automatyczna Przerwa.
// 10 s po zmianie trybu (siedzę / stoję) losowe ćwiczenie. Alerty mają priorytety, ważniejszy zastępuje słabszy.
//
// Zapis w pamięci: liczniki, tabletki, woda i oczy są zapisywane we flash (co minutę i po każdym
// zdarzeniu), więc po odłączeniu zasilania wracają. Czas, gdy urządzenie było wyłączone, się nie liczy.
// Nowy dzień: długo prawy przycisk na podsumowaniu albo trzymaj prawy przycisk podczas włączania.
//
// Uwaga: nie trzymaj lewego przycisku podczas podłączania USB / resetu - wejdzie w tryb wgrywania.
#include <Arduino.h>
#include <TFT_eSPI.h>
#include <Preferences.h>

// --- ustawienia ------------------------------------------------------------
#define SIT_LIMIT_MIN         45    // ciągłe siedzenie: po ilu minutach "WSTAN"
#define REMIND_REPEAT_MIN     10    // ...i co ile powtarzać
#define STAND_LIMIT_MIN       30    // ciągłe stanie: po ilu minutach "USIADZ"
#define STAND_REPEAT_MIN      10
#define STAND_DAILY_LIMIT_MIN 240   // łączne stanie w ciągu dnia: ostrzeżenie "DOSYC"
#define DAILY_REPEAT_MIN      30
#define WATER_EVERY_MIN       45    // co ile przypominać o wodzie
#define WATER_REPEAT_MIN      5     // jeśli nie potwierdzisz - ponów po tylu minutach
#define EYES_EVERY_MIN        20    // reguła 20-20-20
#define EYES_BREAK_S          20
#define PILL_REPEAT_MIN       5
#define MAX_PILLS             3
// kiedy przypomnieć o tabletkach: minuty od uruchomienia urządzenia (bez zegara czasu rzeczywistego)
static const uint16_t PILL_AT_MIN[MAX_PILLS] = {60, 240, 420};

#define ALERT_SHOW_MS    20000      // jak długo widać alert (czas rzeczywisty)
#define EXERCISE_SHOW_MS 15000
#define EXERCISE_DELAY_MS 10000     // ćwiczenie pokazuje się dopiero tyle po ostatniej zmianie trybu
#define LONG_PRESS_MS    1500
#define AUTO_AWAY_AFTER  3          // po tylu zignorowanych "WSTAN" z rzędu - automatyczna Przerwa (0 = wyłącz)
#define UNDO_DEPTH       8          // ile ostatnich tabletek / szklanek można cofnąć
#define TOAST_MS         1500       // jak długo widać krótki komunikat
#define TIME_SCALE       1          // do testów: 60 = każda sekunda liczy się jako minuta

// --- zasilanie ---------------------------------------------------------------
#define CPU_MHZ          80         // 80 MHz wystarcza do animacji, mniej prądu i ciepła niż 240 MHz
#define BL_NORMAL        150        // jasność podświetlenia 0-255
#define BL_DIM           30         // jasność po bezczynności
#define BL_ALERT         255        // jasność podczas alertu
#define BL_PULSE_MIN     50         // alert pulsuje między tą jasnością a BL_ALERT
#define BL_PULSE_MS      1000       // okres pulsowania (ms); 0 = bez pulsowania
#define DIM_AFTER_MIN    5         // po ilu minutach bez przycisków przyciemnić (czas rzeczywisty)
#define BL_PWM_FREQ      5000
#define BL_CHANNEL       0          // kanał LEDC (tylko starszy rdzeń ESP32 2.x)
#ifndef TFT_BL
#define TFT_BL           4          // podświetlenie T-Display
#endif

#define SAVE_EVERY_MS    60000      // zapis okresowy do pamięci flash
#define SAVE_DEBOUNCE_MS 2000       // zapis po zdarzeniu (nie częściej niż co tyle)

#define MIN_MS(m) ((uint64_t)(m) * 60000ULL)

#define RGB565(r, g, b) ((((r) & 0xF8) << 8) | (((g) & 0xFC) << 3) | ((b) >> 3))

static const uint16_t COL_BG     = RGB565(26, 25, 23);
static const uint16_t COL_ORANGE = RGB565(217, 119, 87);
static const uint16_t COL_DARK   = RGB565(20, 20, 19);
static const uint16_t COL_PANEL  = RGB565(48, 46, 43);
static const uint16_t COL_TEXT   = RGB565(250, 249, 245);
static const uint16_t COL_DIM    = RGB565(130, 126, 118);
static const uint16_t COL_TEAL   = RGB565(88, 170, 140);
static const uint16_t COL_BLUE   = RGB565(130, 150, 200);

static const int SCR_W = 240;
static const int SCR_H = 135;
static const int BTN_MODE = 0;
static const int BTN_PILL = 35;

enum Mode { SIT = 0, STAND = 1, AWAY = 2 };
static const char *LABELS[3] = {"Siedze", "Stoje", "Przerwa"};
static const uint16_t ACCENT[3] = {COL_ORANGE, COL_TEAL, COL_BLUE};

enum Kind : uint8_t { K_NONE, K_SIT, K_STAND, K_DAILY, K_PILL, K_WATER, K_EYES, K_EXERCISE };
static const uint8_t PRIO[] = {0, 3, 3, 3, 5, 2, 1, 0};  // wyższy = ważniejszy

static const char *EXERCISES[] = {
  "Obroty szyi", "Nadgarstki x10", "10 przysiadow", "Na palce x15",
  "Krazenie ramion", "Skret tulowia", "Rozciagnij plecy",
};
static const int EX_COUNT = sizeof(EXERCISES) / sizeof(EXERCISES[0]);

TFT_eSPI tft;
TFT_eSprite spr(&tft);

Mode mode = SIT;
int screen = 0;                 // 0 - główny, 1 - podsumowanie
uint64_t acc[3] = {0, 0, 0};    // ms w każdym trybie
uint64_t sitRun = 0, standRun = 0, eyeRun = 0;
uint64_t nextRemind = MIN_MS(SIT_LIMIT_MIN);
uint64_t nextStandRemind = MIN_MS(STAND_LIMIT_MIN);
uint64_t nextDaily = MIN_MS(STAND_DAILY_LIMIT_MIN);
uint64_t waterNext = MIN_MS(WATER_EVERY_MIN);
uint64_t pillNext = 0;
int pills = 0, glasses = 0, eyeBreaks = 0;
uint32_t pillFlashUntil = 0;

Kind alertKind = K_NONE;
uint32_t alertStart = 0, alertUntil = 0;
int exIdx = 0;
Preferences prefs;
bool dirty = false;             // zmiana stanu czeka na zapis
uint32_t lastSave = 0;
uint32_t lastInput = 0;         // ostatnie naciśnięcie przycisku
int blLevel = -1;

Kind undoStack[UNDO_DEPTH];     // ostatnie tabletki / szklanki (tylko w RAM)
int undoCount = 0;
int ignoredSit = 0;             // zignorowane "WSTAN" z rzędu
uint64_t sitMark = 0;           // acc[SIT] przy pierwszym zignorowanym "WSTAN"
uint32_t exerciseAt = 0;        // kiedy pokazać ćwiczenie (0 = nie zaplanowane)
const char *toastText = nullptr;
uint32_t toastUntil = 0;

struct Btn {
  uint8_t pin;
  bool down;
  uint32_t t0;
  bool longDone;
};
Btn bMode = {BTN_MODE, false, 0, false};
Btn bPill = {BTN_PILL, false, 0, false};

// 0 - nic, 1 - krótkie kliknięcie, 2 - długie przytrzymanie
int pollButton(Btn &b, uint32_t now) {
  bool pressed = digitalRead(b.pin) == LOW;
  int ev = 0;
  if (pressed && !b.down) {
    b.down = true;
    b.t0 = now;
    b.longDone = false;
  } else if (pressed && b.down && !b.longDone && now - b.t0 >= LONG_PRESS_MS) {
    b.longDone = true;
    ev = 2;
  } else if (!pressed && b.down) {
    b.down = false;
    if (!b.longDone && now - b.t0 >= 30) ev = 1;
  }
  return ev;
}

// --- logika -----------------------------------------------------------------
uint64_t totalMs() { return acc[0] + acc[1] + acc[2]; }
uint64_t activeMs() { return acc[SIT] + acc[STAND]; }  // czas przy biurku (bez przerw)

void toast(const char *text, uint32_t now) {
  toastText = text;
  toastUntil = now + TOAST_MS;
}

void pushUndo(Kind k) {
  if (undoCount == UNDO_DEPTH) {  // pełny - wyrzuć najstarszy
    for (int i = 1; i < UNDO_DEPTH; i++) undoStack[i - 1] = undoStack[i];
    undoCount--;
  }
  undoStack[undoCount++] = k;
}

int pillsDue() {
  uint64_t total = totalMs();
  int n = 0;
  for (int i = 0; i < MAX_PILLS; i++)
    if (total >= MIN_MS(PILL_AT_MIN[i])) n++;
  return n;
}

// Pokazuje alert, jeśli nie ma aktywnego albo nowy jest ważniejszy.
bool raise(Kind k, uint32_t now, uint32_t durMs) {
  if (alertKind != K_NONE && PRIO[k] <= PRIO[alertKind]) return false;
  alertKind = k;
  alertStart = now;
  alertUntil = now + durMs;
  return true;
}

void setMode(Mode m);

// Alert zniknął sam (minął czas, nikt nie kliknął).
void endAlert(uint32_t now) {
  if (alertKind == K_EYES) {  // przerwa odbyta w całości
    eyeBreaks++;
    dirty = true;
  }
  bool sitIgnored = (alertKind == K_SIT);
  alertKind = K_NONE;

  if (sitIgnored && AUTO_AWAY_AFTER > 0 && ++ignoredSit >= AUTO_AWAY_AFTER) {
    // Nikogo nie ma przy biurku: czas od pierwszego zignorowanego "WSTAN" to przerwa.
    uint64_t moved = acc[SIT] > sitMark ? acc[SIT] - sitMark : 0;
    acc[SIT] -= moved;
    acc[AWAY] += moved;
    setMode(AWAY);
    toast("Auto przerwa", now);
  }
}

void confirmAlert(uint32_t now) {
  switch (alertKind) {
    case K_PILL:
      if (pills < MAX_PILLS) {
        pills++;
        pushUndo(K_PILL);
      }
      pillNext = totalMs() + MIN_MS(1);
      pillFlashUntil = now + 700;
      break;
    case K_WATER:
      glasses++;
      pushUndo(K_WATER);
      waterNext = activeMs() + MIN_MS(WATER_EVERY_MIN);
      break;
    default:
      break;
  }
  dirty = true;
  alertKind = K_NONE;
}

void setMode(Mode m) {
  mode = m;
  dirty = true;
  sitRun = 0;
  standRun = 0;
  nextRemind = MIN_MS(SIT_LIMIT_MIN);
  nextStandRemind = MIN_MS(STAND_LIMIT_MIN);
  ignoredSit = 0;
  if (m == AWAY) eyeRun = 0;  // przerwa od biurka to też odpoczynek dla oczu
  if (alertKind >= K_SIT && alertKind <= K_DAILY) alertKind = K_NONE;
  if (m == AWAY && (alertKind == K_WATER || alertKind == K_EYES)) alertKind = K_NONE;
}

void undoLast(uint32_t now) {
  if (undoCount == 0) {
    toast("Nic do cofniecia", now);
    return;
  }
  Kind k = undoStack[--undoCount];
  if (k == K_PILL && pills > 0) {
    pills--;
    toast("Cofnieto tabletke", now);
  } else if (k == K_WATER && glasses > 0) {
    glasses--;
    waterNext = activeMs() + MIN_MS(WATER_REPEAT_MIN);
    toast("Cofnieto wode", now);
  }
  dirty = true;
}

void showExercise(uint32_t now) {
  exIdx = random(EX_COUNT);
  raise(K_EXERCISE, now, EXERCISE_SHOW_MS);
}

void resetDay() {
  for (int i = 0; i < 3; i++) acc[i] = 0;
  pills = glasses = eyeBreaks = 0;
  eyeRun = 0;
  waterNext = MIN_MS(WATER_EVERY_MIN);
  pillNext = 0;
  nextDaily = MIN_MS(STAND_DAILY_LIMIT_MIN);
  alertKind = K_NONE;
  screen = 0;
  undoCount = 0;
  exerciseAt = 0;
  setMode(SIT);
}

// --- zapis w pamięci flash ---------------------------------------------------
struct SavedState {
  uint32_t magic;
  uint64_t acc[3];
  uint64_t eyeRun, waterNext, pillNext, nextDaily;
  int32_t pills, glasses, eyeBreaks;
  uint8_t mode;
};
static const uint32_t SAVE_MAGIC = 0xC1A0D002;  // zmienione: woda liczy się od czasu przy biurku

void saveState(uint32_t now) {
  SavedState s;
  memset(&s, 0, sizeof(s));
  s.magic = SAVE_MAGIC;
  for (int i = 0; i < 3; i++) s.acc[i] = acc[i];
  s.eyeRun = eyeRun;
  s.waterNext = waterNext;
  s.pillNext = pillNext;
  s.nextDaily = nextDaily;
  s.pills = pills;
  s.glasses = glasses;
  s.eyeBreaks = eyeBreaks;
  s.mode = (uint8_t)mode;
  prefs.putBytes("state", &s, sizeof(s));
  dirty = false;
  lastSave = now;
}

bool loadState() {
  SavedState s;
  if (prefs.getBytesLength("state") != sizeof(s)) return false;
  prefs.getBytes("state", &s, sizeof(s));
  if (s.magic != SAVE_MAGIC || s.mode > 2) return false;
  if (s.pills < 0 || s.pills > MAX_PILLS || s.glasses < 0 || s.eyeBreaks < 0) return false;
  for (int i = 0; i < 3; i++) acc[i] = s.acc[i];
  eyeRun = s.eyeRun;
  waterNext = s.waterNext;
  pillNext = s.pillNext;
  nextDaily = s.nextDaily;
  pills = s.pills;
  glasses = s.glasses;
  eyeBreaks = s.eyeBreaks;
  setMode((Mode)s.mode);  // ciągłe serie (siedzenie/stanie) zaczynają się od zera
  return true;
}

void tick(uint32_t now) {
  static uint32_t last = 0;
  if (last == 0) last = now;
  uint64_t dt = (uint64_t)(now - last) * TIME_SCALE;
  last = now;

  acc[mode] += dt;
  uint64_t total = totalMs();

  if (mode != AWAY) eyeRun += dt;

  if (mode == SIT) {
    sitRun += dt;
    if (sitRun >= nextRemind && raise(K_SIT, now, ALERT_SHOW_MS)) {
      nextRemind += MIN_MS(REMIND_REPEAT_MIN);
      if (ignoredSit == 0) sitMark = acc[SIT];  // od tego momentu ewentualnie liczymy auto-przerwę
    }
  }
  if (mode == STAND) {
    standRun += dt;
    if (acc[STAND] >= nextDaily) {  // ostrzeżenie dzienne ma pierwszeństwo
      if (raise(K_DAILY, now, ALERT_SHOW_MS)) {
        nextDaily += MIN_MS(DAILY_REPEAT_MIN);
        if (standRun >= nextStandRemind) nextStandRemind += MIN_MS(STAND_REPEAT_MIN);
      }
    } else if (standRun >= nextStandRemind && raise(K_STAND, now, ALERT_SHOW_MS)) {
      nextStandRemind += MIN_MS(STAND_REPEAT_MIN);
    }
  }

  if (pills < pillsDue() && total >= pillNext && raise(K_PILL, now, ALERT_SHOW_MS)) pillNext = total + MIN_MS(PILL_REPEAT_MIN);
  uint64_t active = activeMs();
  if (mode != AWAY && active >= waterNext && raise(K_WATER, now, ALERT_SHOW_MS)) waterNext = active + MIN_MS(WATER_REPEAT_MIN);
  if (mode != AWAY && eyeRun >= MIN_MS(EYES_EVERY_MIN) && raise(K_EYES, now, EYES_BREAK_S * 1000UL)) eyeRun = 0;
}

// --- rysowanie --------------------------------------------------------------
// eyes: 0 - otwarte, 1 - zamknięte; look: -1..1 - kierunek patrzenia
void drawClawd(int cx, int top, int u, int eyes, int armL, int armR, int look = 0) {
  const int bw = 9 * u, bh = 5 * u, l = cx - bw / 2;
  spr.fillRect(l - 2 * u, top + 2 * u - armL * u, 2 * u, 2 * u, COL_ORANGE);
  spr.fillRect(l + bw, top + 2 * u - armR * u, 2 * u, 2 * u, COL_ORANGE);
  spr.fillRect(l, top, bw, bh, COL_ORANGE);
  for (int i = 0; i < 4; i++) spr.fillRect(l + (1 + 2 * i) * u, top + bh, u, 2 * u, COL_ORANGE);
  int eh = eyes ? max(1, u / 2) : 2 * u;
  int ey = top + 2 * u + (eyes ? u : 0);
  int sh = look * (u / 2);
  spr.fillRect(l + 2 * u + sh, ey, u, eh, COL_DARK);
  spr.fillRect(l + 6 * u + sh, ey, u, eh, COL_DARK);
}

void drawPill(int x, int y, bool taken, uint16_t outline) {
  if (taken) {
    spr.fillRoundRect(x, y, 16, 8, 4, COL_TEXT);
    spr.fillRoundRect(x, y, 9, 8, 4, COL_ORANGE);
    spr.fillRect(x + 4, y, 5, 8, COL_ORANGE);
  } else {
    spr.drawRoundRect(x, y, 16, 8, 4, outline);
  }
}

void drawPillBig(int x, int y) {
  spr.fillRoundRect(x, y, 28, 14, 7, COL_TEXT);
  spr.fillRoundRect(x, y, 15, 14, 7, COL_ORANGE);
  spr.fillRect(x + 7, y, 8, 14, COL_ORANGE);
}

void drawGlass(int x, int y, int level) {  // level 0..4
  spr.drawRect(x, y, 12, 16, COL_TEXT);
  if (level > 0) spr.fillRect(x + 1, y + 15 - level * 3, 10, level * 3, COL_BLUE);
}

void formatTime(uint64_t ms, char *buf, size_t n) {
  uint32_t s = (uint32_t)(ms / 1000ULL);
  uint32_t h = s / 3600;
  if (h > 99) h = 99;
  snprintf(buf, n, "%02lu:%02lu:%02lu", (unsigned long)h, (unsigned long)((s / 60) % 60), (unsigned long)(s % 60));
}

void formatHM(uint64_t ms, char *buf, size_t n) {
  uint32_t m = (uint32_t)(ms / 60000ULL);
  snprintf(buf, n, "%luh %02lum", (unsigned long)(m / 60), (unsigned long)(m % 60));
}

void drawHeader(uint32_t now) {
  bool blink = (now % 3200) < 140;
  int eyes = (mode == AWAY || blink) ? 1 : 0;
  int arms = (mode == STAND) ? 1 : 0;
  drawClawd(20, 6, 2, eyes, arms, arms);

  spr.setTextDatum(TL_DATUM);
  spr.setTextColor(COL_DIM);
  spr.drawString("Asystent", 40, 5, 2);

  // woda
  spr.drawRect(112, 7, 8, 11, COL_BLUE);
  spr.fillRect(113, 11, 6, 6, COL_BLUE);
  char buf[8];
  snprintf(buf, sizeof(buf), "%d", glasses);
  spr.drawString(buf, 123, 5, 2);

  // tabletki; najbliższa oczekująca miga na pomarańczowo
  bool pending = pills < pillsDue();
  for (int i = 0; i < MAX_PILLS; i++) {
    uint16_t outline = (pending && i == pills && (now / 500) % 2 == 0) ? COL_ORANGE : COL_DIM;
    drawPill(176 + i * 20, 8, i < pills, outline);
  }
  if (now < pillFlashUntil) spr.drawRoundRect(172, 4, 64, 16, 6, COL_ORANGE);
}

void drawRows() {
  const int y0 = 26, rowH = 36;
  char buf[12];
  for (int i = 0; i < 3; i++) {
    int y = y0 + i * rowH;
    bool active = (i == (int)mode);
    if (active) {
      spr.fillRoundRect(4, y + 1, 232, rowH - 3, 5, COL_PANEL);
      spr.fillRect(4, y + 3, 4, rowH - 7, ACCENT[i]);
    }
    spr.setTextColor(active ? ACCENT[i] : COL_DIM);
    spr.setTextDatum(TL_DATUM);
    spr.drawString(LABELS[i], 16, y + 10, 2);
    formatTime(acc[i], buf, sizeof(buf));
    spr.setTextColor(active ? COL_TEXT : COL_DIM);
    spr.setTextDatum(TR_DATUM);
    spr.drawString(buf, 230, y + 5, 4);
  }
}

void drawSummary() {
  char buf[16];
  spr.setTextDatum(TL_DATUM);
  spr.setTextColor(COL_TEXT);
  spr.drawString("Podsumowanie", 8, 5, 2);
  formatHM(totalMs(), buf, sizeof(buf));
  spr.setTextDatum(TR_DATUM);
  spr.setTextColor(COL_DIM);
  spr.drawString(buf, 232, 5, 2);

  for (int i = 0; i < 3; i++) {
    int y = 26 + i * 20;
    spr.setTextDatum(TL_DATUM);
    spr.setTextColor(ACCENT[i]);
    spr.drawString(LABELS[i], 10, y + 2, 2);
    formatHM(acc[i], buf, sizeof(buf));
    spr.setTextDatum(TR_DATUM);
    spr.setTextColor(COL_TEXT);
    spr.drawString(buf, 230, y + 2, 2);
  }

  const char *names[3] = {"tabletki", "woda", "oczy"};
  char vals[3][8];
  snprintf(vals[0], 8, "%d/%d", pills, MAX_PILLS);
  snprintf(vals[1], 8, "%d", glasses);
  snprintf(vals[2], 8, "%d", eyeBreaks);
  for (int i = 0; i < 3; i++) {
    int x = 4 + i * 80;
    spr.fillRoundRect(x, 89, 72, 45, 6, COL_PANEL);
    spr.setTextDatum(TC_DATUM);
    spr.setTextColor(COL_TEXT);
    spr.drawString(vals[i], x + 36, 92, 4);
    spr.setTextColor(COL_DIM);
    spr.drawString(names[i], x + 36, 117, 2);
  }
}

void drawAlert(uint32_t now) {
  bool flash = (now / 500) % 2 == 0;
  uint16_t col = flash ? COL_ORANGE : COL_TEXT;
  spr.fillRoundRect(6, 28, 228, 100, 8, COL_BG);
  spr.drawRoundRect(6, 28, 228, 100, 8, col);
  spr.drawRoundRect(7, 29, 226, 98, 8, col);

  int a = (now / 250) % 2;
  int armL = a, armR = 1 - a, look = 0;
  const char *title = "";
  const char *hint = "";
  char line[32] = "";

  switch (alertKind) {
    case K_STAND:
      title = "USIADZ!";
      hint = "lewy = przerwa";
      snprintf(line, sizeof(line), "Stoisz %lu min", (unsigned long)(standRun / 60000ULL));
      break;
    case K_DAILY:
      title = "DOSYC!";
      hint = "odpocznij nogi";
      snprintf(line, sizeof(line), "Stania: %lu min", (unsigned long)(acc[STAND] / 60000ULL));
      break;
    case K_PILL:
      title = "TABLETKA";
      hint = "prawy = wzialem";
      snprintf(line, sizeof(line), "Nr %d z %d", pills + 1, MAX_PILLS);
      armL = 0;
      armR = 1;
      break;
    case K_WATER:
      title = "WODA!";
      hint = "prawy = wypilem";
      snprintf(line, sizeof(line), "Szklanka nr %d", glasses + 1);
      armL = 0;
      armR = 1;
      break;
    case K_EYES:
      title = "W DAL!";
      hint = "patrz ok. 6 m";
      snprintf(line, sizeof(line), "Jeszcze %lu s", (unsigned long)((alertUntil - now + 999) / 1000));
      armL = armR = 0;
      look = (int)((now / 900) % 3) - 1;
      break;
    case K_EXERCISE:
      title = "RUSZ SIE";
      hint = "prawy = ok";
      snprintf(line, sizeof(line), "%s", EXERCISES[exIdx]);
      break;
    default:  // K_SIT
      title = "WSTAN!";
      hint = "lewy = ok";
      snprintf(line, sizeof(line), "Siedzisz %lu min", (unsigned long)(sitRun / 60000ULL));
      break;
  }

  drawClawd(52, 58, 5, 0, armL, armR, look);
  if (alertKind == K_PILL) drawPillBig(88, 68);
  if (alertKind == K_WATER) drawGlass(90, 62, 4 - (int)((now / 400) % 5));

  spr.setTextDatum(TL_DATUM);
  spr.setTextColor(col);
  spr.drawString(title, 112, 52, 4);
  spr.setTextColor(COL_TEXT);
  spr.drawString(line, 112, 84, 2);
  spr.setTextColor(COL_DIM);
  spr.drawString(hint, 112, 102, 2);

  if (alertKind == K_EYES) {
    float p = (float)(now - alertStart) / (float)(alertUntil - alertStart);
    spr.fillRect(16, 121, (int)(208 * constrain(p, 0.0f, 1.0f)), 3, COL_ORANGE);
  }
}

// --- podświetlenie (PWM) -----------------------------------------------------
void backlightInit() {
#if ESP_ARDUINO_VERSION_MAJOR >= 3
  ledcAttach(TFT_BL, BL_PWM_FREQ, 8);
#else
  ledcSetup(BL_CHANNEL, BL_PWM_FREQ, 8);
  ledcAttachPin(TFT_BL, BL_CHANNEL);
#endif
}

void setBacklight(int level) {
  if (level == blLevel) return;
  blLevel = level;
#if ESP_ARDUINO_VERSION_MAJOR >= 3
  ledcWrite(TFT_BL, level);
#else
  ledcWrite(BL_CHANNEL, level);
#endif
}

void updateBacklight(uint32_t now) {
  bool pulse = BL_PULSE_MS > 0 && alertKind != K_NONE && alertKind != K_EYES && alertKind != K_EXERCISE;
  if (pulse) {  // trójkąt: BL_PULSE_MIN -> BL_ALERT -> BL_PULSE_MIN
    uint32_t half = BL_PULSE_MS / 2;
    uint32_t ph = (now - alertStart) % BL_PULSE_MS;
    uint32_t tri = ph < half ? ph : BL_PULSE_MS - ph;
    setBacklight(BL_PULSE_MIN + (int)((BL_ALERT - BL_PULSE_MIN) * tri / half));
  } else if (alertKind != K_NONE) setBacklight(BL_ALERT);
  else if (now - lastInput >= DIM_AFTER_MIN * 60000UL) setBacklight(BL_DIM);
  else setBacklight(BL_NORMAL);
}

void setup() {
  setCpuFrequencyMhz(CPU_MHZ);
  btStop();  // WiFi i Bluetooth nie są używane; WiFi startuje tylko na żądanie, więc wystarczy to
  Serial.begin(115200);
  pinMode(BTN_MODE, INPUT_PULLUP);
  pinMode(BTN_PILL, INPUT);  // GPIO35 - tylko wejście, pull-up jest na płytce
  randomSeed(esp_random());

  tft.init();
  tft.setRotation(1);
  tft.fillScreen(COL_BG);
  if (spr.createSprite(SCR_W, SCR_H) == nullptr) Serial.println("Brak pamieci na sprite!");

  backlightInit();
  setBacklight(BL_NORMAL);
  lastInput = millis();

  prefs.begin("asystent", false);
  bool restored = loadState();
  if (digitalRead(BTN_PILL) == LOW) {  // prawy przycisk trzymany przy starcie = nowy dzień
    resetDay();
    restored = false;
    tft.setTextDatum(MC_DATUM);
    tft.setTextColor(COL_TEXT, COL_BG);
    tft.drawString("Nowy dzien", SCR_W / 2, SCR_H / 2, 4);
    delay(1000);
  }
  lastSave = millis();
  Serial.println(restored ? "Wczytano zapisany stan" : "Start z zerowymi licznikami");
}

void loop() {
  static uint32_t lastFrame = 0;
  uint32_t now = millis();

  int evL = pollButton(bMode, now);
  int evR = pollButton(bPill, now);

  if (evL == 1) {
    setMode((Mode)((mode + 1) % 3));
    // ćwiczenie po chwili, żeby najpierw było widać wybrany tryb; kolejne kliknięcie odlicza od nowa.
    // W Przerwie bez ćwiczenia - i tak nikt go nie zobaczy.
    exerciseAt = (mode == AWAY) ? 0 : (now + EXERCISE_DELAY_MS) | 1;
  } else if (evL == 2) {
    screen ^= 1;
  }

  if (evR == 1) {
    if (alertKind != K_NONE) {
      confirmAlert(now);
    } else if (screen == 0 && pills < MAX_PILLS) {
      pills++;
      pushUndo(K_PILL);
      dirty = true;
      pillFlashUntil = now + 700;
    }
  } else if (evR == 2) {
    if (screen == 1) resetDay();
    else undoLast(now);
  }

  if (bMode.down || bPill.down) ignoredSit = 0;  // ktoś jest przy biurku

  if (exerciseAt && (int32_t)(now - exerciseAt) >= 0) {
    exerciseAt = 0;
    if (mode != AWAY) showExercise(now);
  }

  tick(now);
  if (alertKind != K_NONE && now >= alertUntil) endAlert(now);

  if ((dirty && now - lastSave >= SAVE_DEBOUNCE_MS) || now - lastSave >= SAVE_EVERY_MS) saveState(now);

  if (bMode.down || bPill.down) lastInput = now;
  updateBacklight(now);

  if (now - lastFrame < 50) {  // ~20 FPS; między klatkami procesor odpoczywa
    delay(5);
    return;
  }
  lastFrame = now;

  spr.fillSprite(COL_BG);
  if (screen == 0) {
    drawHeader(now);
    drawRows();
  } else {
    drawSummary();
  }
  if (alertKind != K_NONE) drawAlert(now);
  if (toastText && now < toastUntil) {
    spr.fillRoundRect(30, 54, 180, 28, 6, COL_PANEL);
    spr.drawRoundRect(30, 54, 180, 28, 6, COL_ORANGE);
    spr.setTextDatum(MC_DATUM);
    spr.setTextColor(COL_TEXT);
    spr.drawString(toastText, SCR_W / 2, 68, 2);
  }
  spr.pushSprite(0, 0);
}
