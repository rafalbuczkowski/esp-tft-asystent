# ESP TFT asystent

**English** | [Polski](README.pl.md)

Arduino sketches for the **LilyGO TTGO T-Display** board (ESP32, 1.14" ST7789 135×240 screen, buttons on GPIO0 and GPIO35).

| Sketch | What it does |
|---|---|
| [`ClawdTDisplay`](ClawdTDisplay/ClawdTDisplay.ino) | Display test: an animated Clawd mascot with typed messages |
| [`AsystentBiurkowy`](AsystentBiurkowy/AsystentBiurkowy.ino) | Desk assistant: tracks sitting / standing / break time and reminds you to move, drink water, take pills and rest your eyes |

> The on-screen text is in Polish (ASCII only, because the built-in TFT fonts have no Polish characters). Translations are given in the tables below.

## Requirements (Arduino IDE)
1. Boards: install the **esp32 by Espressif Systems** package (Boards Manager) and select **ESP32 Dev Module**.
2. Library: **TFT_eSPI** (Bodmer) from the Library Manager.
3. In `Documents\Arduino\libraries\TFT_eSPI\User_Setup_Select.h`:
   - comment out `#include <User_Setup.h>`
   - uncomment `#include <User_Setups/Setup25_TTGO_T_Display.h>`
4. Restart Arduino IDE, select the COM port and upload.
   - Upload Speed: **115200** or **460800** (921600 is often unreliable).
   - If the upload hangs on `Connecting...`: hold the left button (GPIO0), press and release RESET, then release the left button.

## ClawdTDisplay
- GPIO0 (left): stand still / walk around
- GPIO35 (right): next message

## AsystentBiurkowy (desk assistant)

### Buttons
| Button | Short press | Long press (1.5 s) |
|---|---|---|
| Left (GPIO0) | next mode: Sitting → Standing → Break | main screen ↔ daily summary |
| Right (GPIO35) | confirm / dismiss an alert; with no alert, mark a pill as taken | main screen: undo the last pill or glass of water; summary: start a new day |

- **Rotate the screen 180°:** hold **both buttons** for 1.5 s, so the USB-C port can be on the left or on the right. The setting is remembered. After rotating, the buttons swap roles so that "left" and "right" stay where they were on screen (`SWAP_BTNS_WHEN_FLIPPED 0` disables the swap).
- **New day:** hold GPIO35 while powering on (or pressing RESET), or long-press right on the summary screen.
- Do not hold the left button (GPIO0) while powering on: the board enters upload mode and the screen stays black. Pressing RESET on its own brings it back.

Screen modes: `Siedze` = sitting, `Stoje` = standing, `Przerwa` = break.

### Reminders
| Alert | Meaning | When |
|---|---|---|
| WSTAN! | stand up | after 45 min of continuous sitting, then every 10 min |
| USIADZ! | sit down | after 30 min of continuous standing, then every 10 min |
| DOSYC! | enough standing | after 4 h of standing in total during the day |
| WODA! | drink water | every 45 min at the desk (paused during a break) |
| TABLETKA | take a pill | after 1 h, 4 h and 7 h from the start of the day |
| W DAL! | look far away | every 20 min at the desk, look about 6 m away for 20 s (20-20-20 rule) |
| RUSZ SIE | move a bit | a random exercise 10 s after changing mode |

- Every 30 s the small Clawd in the top-left corner plays a calm 5-second animation: it walks a few steps back and forth, or "thinks" (raises a hand and slowly looks from side to side). `ANIM_EVERY_S 0` disables it.
- 3 ignored "WSTAN!" alerts in a row with no button press → automatic break. The time since the first ignored alert is moved from sitting to break.
- During alerts the backlight pulses. After 5 min without a button press the screen dims.
- The day's state is saved to flash (every minute and after each event), so it survives a power loss.
- All limits are at the top of the `.ino` file. For testing use `TIME_SCALE 60`, so each second counts as a minute.

### Power
CPU at 80 MHz, Bluetooth off, WiFi unused, ~20 FPS with idle time between frames, PWM backlight.
