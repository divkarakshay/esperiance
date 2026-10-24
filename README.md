# Esperiance

ESP32 retro game console — a collection of classic arcade games on an ESP32 (DevKitC v4)
with a small display, buttons, and a buzzer. Built with ESP-IDF (C).

## Hardware

- ESP32 DevKitC v4 (4MB flash)
- Display (see `main/display.c`)
- Buttons (see `main/input.c`)
- Buzzer (see `main/buzzer.c`)

## Games

Snake, Snake 2, Pong, Breakout, Space, Pairs, Bantumi, Connect 4, Mines, Raindrops,
Bounce, Tic-Tac-Toe, Tetris, Munkiki, Bloc Bloc, Flan.

## Build

Requires [ESP-IDF](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/).

```bash
idf.py set-target esp32
idf.py build
idf.py flash monitor
```

## Layout

- `main/` — app source (games, menu, display, input, buzzer)
- `sdkconfig.defaults` — default ESP-IDF config (ESP32, 4MB flash)
