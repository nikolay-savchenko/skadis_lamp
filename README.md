# skadis_lamp

A tunable-white desk/wall lamp built from an Arduino Nano, a rotary encoder with a push button and a WS2812B addressable LED strip. One knob controls both the color temperature and the brightness of the lamp.

## Features

- **Color temperature** from very warm (~1700 K, candle-like) to very cool (~9000 K, bluish daylight).
- **Brightness** in 2% steps, dimmed in two stages: even LEDs fade out first, then odd LEDs.
- **Two ways to turn off**, each with its own power-on behavior:
  - dim all the way down to 0% → the next click turns the lamp on at **70%**;
  - hold the button for **2 s** → the next click restores the **brightness it was turned off at**.
- **Rotation acceleration**: faster turning gives bigger steps (×2 / ×4).
- **Power limiting** through FastLED, so the strip never draws more current than your power supply can deliver.

## Hardware

| Part | Notes |
|---|---|
| Arduino Nano | ATmega328P |
| Rotary encoder with push button | e.g. KY-040 |
| WS2812B LED strip, ~70 cm | 30 / 60 / 144 LEDs per meter |
| 5 V power supply | 3 A or more is recommended for 42 LEDs |
| 330 Ω resistor | in series with the strip data line |
| 1000 µF capacitor (≥ 6.3 V) | across the strip power input |

## Wiring

| From | To |
|---|---|
| Encoder CLK | D2 |
| Encoder DT | D3 |
| Encoder SW | D4 |
| Encoder + | 5V |
| Encoder GND | GND |
| Strip DIN | D6 (through the 330 Ω resistor) |
| Strip +5V | PSU +5V |
| Strip GND | PSU GND **and** Arduino GND |

> ⚠️ Do not power the strip from the Arduino's USB or 5V pin: at full white, 42 LEDs can draw up to ~2.5 A. Power the strip directly from the PSU and connect all grounds together.

D2 and D3 are used for the encoder because they are the Nano's hardware interrupt pins.

## Controls

| Lamp state | Action | Result |
|---|---|---|
| On, temperature mode | Rotate | Warmer ↔ cooler |
| On, brightness mode | Rotate | Dimmer ↔ brighter |
| On | Click | Toggle between temperature and brightness mode (1 blink = temperature, 2 blinks = brightness) |
| On | Hold > 2 s | Turn off and remember the current brightness |
| On, brightness mode | Dim down to 0% | Turn off |
| Off (dimmed to 0%) | Click | Turn on at 70% |
| Off (held > 2 s) | Click | Turn on at the remembered brightness |
| Off | Rotate | Ignored |

After any power-on, the lamp starts in temperature mode.

### How dimming works

```
100% ──────────── 50% ──────────── 0%
 even LEDs fade out │ odd LEDs fade out
 odd LEDs at full   │ even LEDs off
```

Each half uses a quadratic curve, so brightness changes look even to the eye.

## Installation

1. Install the [Arduino IDE](https://www.arduino.cc/en/software).
2. Open **Library Manager** and install **FastLED**.
3. Open `lamp.ino`.
4. Select **Board: Arduino Nano**. Some clones need **Processor: ATmega328P (Old Bootloader)**.
5. Adjust the settings below if needed, then upload.

## Configuration

All settings are `#define`s at the top of `lamp.ino`.

| Setting | Default | Meaning |
|---|---|---|
| `NUM_LEDS` | `42` | Number of LEDs on your strip. 70 cm is 21 at 30/m, 42 at 60/m, or ~100 at 144/m |
| `MAX_MILLIAMPS` | `2000` | Current limit in mA; set it slightly below your PSU rating |
| `LED_PIN` | `6` | Strip data pin |
| `ENC_A_PIN`, `ENC_B_PIN`, `ENC_BTN_PIN` | `2`, `3`, `4` | Encoder pins |
| `TEMP_MIN` / `TEMP_MAX` | `1700` / `9000` | Color temperature range, K |
| `TEMP_START` | `3000` | Color temperature after power-up, K |
| `TEMP_STEP` | `100` | K per encoder click |
| `BRI_STEP` | `2` | % per encoder click |
| `BRI_ON_DEFAULT` | `70` | Brightness after power-on from 0%, % |
| `HOLD_MS` | `2000` | Hold time to turn off, ms |
| `ENC_STEPS_PER_DETENT` | `4` | Encoder pulses per click; set to `2` if one click gives two steps |
| `ENC_REVERSE` | `false` | Set to `true` if the rotation direction is reversed |
| `FADE_FIRST_PARITY` | `1` | `1`: even LEDs (2nd, 4th, …) fade first. `0`: odd LEDs fade first |

## How it works

- **Color temperature → RGB.** WS2812B has no dedicated white LEDs, so white is mixed from RGB using Tanner Helland's blackbody approximation. FastLED's `TypicalLEDStrip` color correction is applied on top of it.
- **Encoder.** Both encoder pins trigger interrupts and are decoded with a quadrature state table, so no steps are missed even during strip updates.
- **Button.** The button is debounced in software (30 ms). A long press fires as soon as 2 s have passed, without waiting for release.

## Limitations / ideas

- Settings are not saved across power cycles; storing temperature and brightness in EEPROM would fix that.
- RGB-mixed white has a lower CRI than real white LEDs. For better light quality, consider an SK6812 RGBW strip.
