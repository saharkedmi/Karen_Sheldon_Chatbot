QBOT - ESP32-S3 Gemini Client

Overview
- ESP32-S3 (N16R8) with 0.66" OLED, I2S microphone, and I2S speaker/DAC.
- ESP captures short audio clips, sends to a local proxy server.
- Proxy performs Speech-To-Text (VOSK or other), calls Gemini via Google Generative API, returns TTS audio.

Why a proxy
- Authenticating to Google Cloud and running STT/TTS from the device is difficult on ESP; the proxy handles credentials and heavy compute.

Contents
- `esp/src/main.cpp` - firmware for the ESP32-S3
- `esp/src/config.example.h` - copy to `esp/src/config.h` and fill in your WiFi credentials (this file is gitignored)
- `server/app.py` - Python Flask proxy that does STT, calls Gemini, and provides TTS
- `server/requirements.txt` - Python dependencies
- `server/README.md` - setup and usage instructions

Hardware wiring notes
- I2S microphone (e.g. INMP441): connect to ESP32-S3 I2S pins (WS, SD, SCK)
- I2S DAC / amplifier (e.g. MAX98357A) for speaker output
- SSD1306 64x48/64x32 OLED for status

See `server/README.md` for how to configure Gemini credentials and run the server.
