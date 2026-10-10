/*
  Светильник: Arduino Nano + энкодер с кнопкой + WS2812B

  Управление:
  - Вращение (режим ТЕМПЕРАТУРА): от очень тёплого (~1700K) до очень холодного (~9000K)
  - Клик (лампа включена): переключение ТЕМПЕРАТУРА <-> ЯРКОСТЬ
      индикация: 1 мигание = температура, 2 мигания = яркость
  - Вращение (режим ЯРКОСТЬ): 100%..50% — гаснут чётные диоды, 50%..0% — нечётные
  - Яркость дошла до 0% -> лампа выключена; клик -> включение на 70%
  - Удержание > 2 с (лампа включена) -> выключение; клик -> включение с прежней яркостью

  Библиотека: FastLED (Менеджер библиотек -> "FastLED")
*/

#include <FastLED.h>

// ===================== ПИНЫ =====================
#define LED_PIN      6     // DIN ленты (через резистор 330 Ом)
#define ENC_A_PIN    2     // CLK энкодера (пин с прерыванием)
#define ENC_B_PIN    3     // DT энкодера  (пин с прерыванием)
#define ENC_BTN_PIN  4     // SW энкодера

// ===================== ЛЕНТА =====================
// 70 см: 30 LED/м -> 21, 60 LED/м -> 42, 144 LED/м -> 100. Посчитай диоды на своём куске.
#define NUM_LEDS       42
#define MAX_MILLIAMPS  2000   // ток блока питания с запасом (для БП 5В 3А ставь ~2500)

// ===================== НАСТРОЙКИ =====================
#define TEMP_MIN        1700   // K, самый тёплый
#define TEMP_MAX        9000   // K, самый холодный
#define TEMP_START      3000   // K, при подаче питания
#define TEMP_STEP       100    // K за один щелчок

#define BRI_STEP        2      // % за один щелчок
#define BRI_ON_DEFAULT  70     // % при включении после "докручивания до нуля"

#define HOLD_MS         2000   // удержание для выключения
#define DEBOUNCE_MS     30

// Сколько импульсов на один щелчок энкодера. У большинства KY-040 — 4.
// Если на один щелчок значение меняется на 2 шага — поставь 2.
#define ENC_STEPS_PER_DETENT 4
#define ENC_REVERSE     false  // true, если направление вращения перепутано

// Какие диоды гаснут первыми (считая диоды с 1):
// 1 -> первыми гаснут 2-й, 4-й, 6-й... (чётные), потом 1-й, 3-й, 5-й...
// 0 -> наоборот
#define FADE_FIRST_PARITY 1

// ===================== СОСТОЯНИЕ =====================
CRGB leds[NUM_LEDS];
CRGB baseColor;

enum Mode : uint8_t { MODE_TEMP, MODE_BRI };
Mode mode = MODE_TEMP;

bool isOn      = true;
bool offByHold = false;     // как выключили: удержанием (true) или докрутили до 0 (false)
int  tempK     = TEMP_START;
int  bri       = BRI_ON_DEFAULT;   // 0..100 %
int  savedBri  = BRI_ON_DEFAULT;

// ===================== ЭНКОДЕР (прерывания) =====================
volatile int16_t encCount = 0;
volatile uint8_t encPrev  = 0;
const int8_t ENC_TABLE[16] = { 0, -1,  1,  0,
                               1,  0,  0, -1,
                              -1,  0,  0,  1,
                               0,  1, -1,  0 };

void encoderISR() {
  uint8_t cur = (digitalRead(ENC_A_PIN) << 1) | digitalRead(ENC_B_PIN);
  encCount += ENC_TABLE[(encPrev << 2) | cur];
  encPrev = cur;
}

// ===================== ЦВЕТ =====================
// Перевод цветовой температуры в RGB (аппроксимация Tanner Helland)
CRGB kelvinToRGB(int kelvin) {
  float t = kelvin / 100.0;
  float r, g, b;

  if (t <= 66) {
    r = 255;
    g = 99.4708025861 * log(t) - 161.1195681661;
  } else {
    r = 329.698727446 * pow(t - 60, -0.1332047592);
    g = 288.1221695283 * pow(t - 60, -0.0755148492);
  }

  if (t >= 66)      b = 255;
  else if (t <= 19) b = 0;
  else              b = 138.5177312231 * log(t - 10) - 305.0447927307;

  return CRGB((uint8_t)constrain(r, 0, 255),
              (uint8_t)constrain(g, 0, 255),
              (uint8_t)constrain(b, 0, 255));
}

// Яркость одной группы диодов: x = 0..50 -> 0..255, с квадратичной кривой
// (чтобы на глаз яркость менялась равномерно)
uint8_t halfLevel(int x) {
  if (x <= 0) return 0;
  uint16_t v = (uint32_t)x * x * 255 / 2500;
  return v < 1 ? 1 : v;
}

void render() {
  if (!isOn) {
    FastLED.clear();
    FastLED.show();
    return;
  }

  uint8_t firstLvl, secondLvl;   // first — гаснут первыми, second — вторыми
  if (bri >= 50) {
    firstLvl  = halfLevel(bri - 50);
    secondLvl = 255;
  } else {
    firstLvl  = 0;
    secondLvl = halfLevel(bri);
  }

  for (int i = 0; i < NUM_LEDS; i++) {
    uint8_t lvl = (i % 2 == FADE_FIRST_PARITY) ? firstLvl : secondLvl;
    if (lvl == 0) {
      leds[i] = CRGB::Black;
    } else {
      leds[i] = baseColor;
      leds[i].nscale8_video(lvl);
    }
  }
  FastLED.show();
}

// Мигание для индикации режима
void blinkIndicate(uint8_t times) {
  for (uint8_t k = 0; k < times; k++) {
    FastLED.setBrightness(40);
    FastLED.show();
    delay(90);
    FastLED.setBrightness(255);
    FastLED.show();
    delay(90);
  }
}

// ===================== ДЕЙСТВИЯ =====================
void onClick() {
  if (!isOn) {
    bri  = offByHold ? savedBri : BRI_ON_DEFAULT;
    isOn = true;
    mode = MODE_TEMP;
    render();
  } else {
    mode = (mode == MODE_TEMP) ? MODE_BRI : MODE_TEMP;
    blinkIndicate(mode == MODE_TEMP ? 1 : 2);
  }
}

void onHold() {
  if (isOn) {
    savedBri  = bri;
    isOn      = false;
    offByHold = true;
    mode      = MODE_TEMP;
    render();
  }
}

// ===================== КНОПКА =====================
void handleButton() {
  static bool     lastRaw     = false;
  static bool     btnState    = false;
  static uint32_t lastChange  = 0;
  static uint32_t pressStart  = 0;
  static bool     holdHandled = false;

  uint32_t now = millis();
  bool raw = (digitalRead(ENC_BTN_PIN) == LOW);

  if (raw != lastRaw) {
    lastRaw = raw;
    lastChange = now;
  }

  if (now - lastChange > DEBOUNCE_MS && raw != btnState) {
    btnState = raw;
    if (btnState) {                 // нажали
      pressStart = now;
      holdHandled = false;
    } else {                        // отпустили
      if (!holdHandled) onClick();
    }
  }

  // удержание срабатывает сразу по истечении 2 с, не дожидаясь отпускания
  if (btnState && !holdHandled && now - pressStart >= HOLD_MS) {
    holdHandled = true;
    onHold();
  }
}

// ===================== ВРАЩЕНИЕ =====================
void handleEncoder() {
  static uint32_t lastStepMs = 0;

  int16_t det;
  noInterrupts();
  det = encCount / ENC_STEPS_PER_DETENT;
  encCount -= det * ENC_STEPS_PER_DETENT;
  interrupts();

  if (det == 0) return;
  if (ENC_REVERSE) det = -det;

  // ускорение при быстром вращении
  uint32_t now = millis();
  uint32_t dt = now - lastStepMs;
  int mult = (dt < 40) ? 4 : (dt < 100) ? 2 : 1;
  lastStepMs = now;

  if (!isOn) return;   // в выключенном состоянии вращение игнорируется

  if (mode == MODE_TEMP) {
    int nt = tempK + det * TEMP_STEP * mult;
    nt = constrain(nt, TEMP_MIN, TEMP_MAX);
    if (nt != tempK) {
      tempK = nt;
      baseColor = kelvinToRGB(tempK);
      render();
    }
  } else {
    int nb = bri + det * BRI_STEP * mult;
    nb = constrain(nb, 0, 100);
    if (nb != bri) {
      bri = nb;
      if (bri == 0) {
        isOn = false;
        offByHold = false;
        mode = MODE_TEMP;
      }
      render();
    }
  }
}

// ===================== SETUP / LOOP =====================
void setup() {
  pinMode(ENC_A_PIN, INPUT_PULLUP);
  pinMode(ENC_B_PIN, INPUT_PULLUP);
  pinMode(ENC_BTN_PIN, INPUT_PULLUP);

  encPrev = (digitalRead(ENC_A_PIN) << 1) | digitalRead(ENC_B_PIN);
  attachInterrupt(digitalPinToInterrupt(ENC_A_PIN), encoderISR, CHANGE);
  attachInterrupt(digitalPinToInterrupt(ENC_B_PIN), encoderISR, CHANGE);

  FastLED.addLeds<WS2812B, LED_PIN, GRB>(leds, NUM_LEDS).setCorrection(TypicalLEDStrip);
  FastLED.setMaxPowerInVoltsAndMilliamps(5, MAX_MILLIAMPS);
  FastLED.setBrightness(255);

  baseColor = kelvinToRGB(tempK);
  render();
}

void loop() {
  handleButton();
  handleEncoder();
}
