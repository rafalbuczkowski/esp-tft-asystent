# ESP TFT asystent

Szkice Arduino dla płytki **LilyGO TTGO T-Display** (ESP32, ekran ST7789 1,14" 135×240, przyciski GPIO0 i GPIO35).

| Szkic | Co robi |
|---|---|
| [`ClawdTDisplay`](ClawdTDisplay/ClawdTDisplay.ino) | Test ekranu: animowana maskotka Clawd z napisami |
| [`AsystentBiurkowy`](AsystentBiurkowy/AsystentBiurkowy.ino) | Asystent przy biurku: czas siedzenia / stania / przerw, przypomnienia o ruchu, wodzie, tabletkach i oczach |

## Wymagania (Arduino IDE)
1. Płytki: pakiet **esp32 by Espressif Systems** (Menedżer płytek), wybierz **ESP32 Dev Module**.
2. Biblioteka **TFT_eSPI** (Bodmer) z Menedżera bibliotek.
3. W `Documents\Arduino\libraries\TFT_eSPI\User_Setup_Select.h`:
   - zakomentuj `#include <User_Setup.h>`
   - odkomentuj `#include <User_Setups/Setup25_TTGO_T_Display.h>`
4. Zrestartuj Arduino IDE, wybierz port COM i wgraj.
   - Upload Speed: **115200** lub **460800** (921600 bywa zawodne).
   - Jeśli wgrywanie wisi na `Connecting...`: przytrzymaj lewy przycisk (GPIO0), naciśnij i puść RESET, puść lewy.

## ClawdTDisplay
- GPIO0 (lewy): stoi / spaceruje
- GPIO35 (prawy): następna wiadomość

## AsystentBiurkowy

### Przyciski
| Przycisk | Krótko | Długo (1,5 s) |
|---|---|---|
| Lewy (GPIO0) | zmiana trybu: Siedzę → Stoję → Przerwa | ekran główny ↔ podsumowanie dnia |
| Prawy (GPIO35) | potwierdza / zamyka alert; bez alertu ręcznie zaznacza tabletkę | ekran główny: cofa ostatnią tabletkę lub szklankę; podsumowanie: nowy dzień |

- **Nowy dzień:** trzymaj prawy przycisk podczas włączania (lub RESET), albo długo prawy na podsumowaniu.
- Nie trzymaj lewego przycisku przy włączaniu – płytka wejdzie w tryb wgrywania (czarny ekran; pomaga sam RESET).

### Przypomnienia
| Alert | Kiedy |
|---|---|
| WSTAN! | po 45 min ciągłego siedzenia, potem co 10 min |
| USIADZ! | po 30 min ciągłego stania, potem co 10 min |
| DOSYC! | po łącznie 4 h stania w ciągu dnia |
| WODA! | co 45 min przy biurku (wstrzymane w Przerwie) |
| TABLETKA | po 1 h, 4 h i 7 h od początku dnia |
| W DAL! | co 20 min przy biurku – 20 s patrzenia w dal |
| RUSZ SIE | losowe ćwiczenie 10 s po zmianie trybu |

- 3 zignorowane „WSTAN” bez dotknięcia przycisku → automatyczna Przerwa.
- Przy alertach ekran pulsuje, po 5 min bez przycisków się przyciemnia.
- Stan dnia zapisuje się we flash (co minutę i po każdym zdarzeniu), więc przetrwa odłączenie zasilania.
- Wszystkie limity są na górze pliku `.ino` w sekcji „ustawienia”. Do testów: `TIME_SCALE 60` (sekunda liczy się jak minuta).

### Zasilanie
CPU 80 MHz, Bluetooth wyłączony, WiFi nieużywane, ~20 FPS z przerwami między klatkami, podświetlenie przez PWM.
