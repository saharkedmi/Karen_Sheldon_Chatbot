# Server Architecture & Deployment Notes

This document describes the production proxy server that bridges the ESP32 firmware to Gemini and a neural TTS engine. It covers the data flow, the server code, and the operational commands used to manage it on a CasaOS/Docker host.

---

## 1. System Architecture & Data Flow

The server acts as a smart proxy and media processor between the ESP32 hardware, the cloud LLM, and the neural speech engine.

1. **Ingress:** The ESP32 sends an `HTTP POST` request to the `/ask` route containing a raw audio buffer (Raw PCM, 16kHz, 16-bit, mono).
2. **Input processing:** The server wraps the raw audio into a standard `WAV` container, base64-encodes it, and sends it to the Gemini API.
3. **Model inference (LLM):** The `Gemini 2.5 Flash` model processes the audio together with a system prompt and returns a focused text answer in Hebrew.
4. **Speech synthesis (TTS):** The `edge-tts` library reads the text and generates a natural-sounding voice clip in `MP3` format using Microsoft's neural TTS backend.
5. **Output processing & gain:** The `pydub` library (backed by `ffmpeg`) opens the MP3, applies a configurable digital gain boost (`VOLUME_BOOST_DB`), and re-encodes it to `WAV 16kHz, mono, 16-bit`.
6. **Egress:** The server streams the final WAV file back to the ESP32 in real time.

---

## 2. Main Server File (`app_new.py`)

- **Path on the host:** `/data/AppData/tars-server/app_new.py`
- **Runtime dependencies inside the container:** `ffmpeg`, `flask`, `requests`, `pydub`, `edge-tts`.

> The code includes a self-healing mechanism that automatically installs any missing Linux/Python dependencies if the container starts without them.

```python
import io
import wave
import os
import sys
import base64
import asyncio

try:
    from flask import Flask, request, send_file
    import requests
    from pydub import AudioSegment
    import edge_tts
except ImportError:
    print("Installing system dependencies and requirements...")
    os.system("apt-get update && apt-get install -y ffmpeg")
    os.system("pip install --no-cache-dir flask requests pydub edge-tts")
    os.execv(sys.executable, ['python'] + sys.argv)

app = Flask(__name__)

# =====================================================================
# Main configuration
# =====================================================================
GENAI_API_KEY = "YOUR_API_KEY"       # set your own Gemini API key here (or load from env)
CURRENT_VOICE = "he-IL-AvriNeural"   # options: he-IL-AvriNeural (male) or he-IL-HilaNeural (female)
VOLUME_BOOST_DB = 12                 # recommended range: 0 to 15 dB
# =====================================================================

@app.route('/ask', methods=['POST'])
def ask_keren():
    print("Received voice message from Keren...")
    raw_audio_data = request.data
    if not raw_audio_data:
        return "No audio data", 400

    temp_mp3_path = "temp_response.mp3"
    try:
        # 1. Wrap the raw ESP32 audio into an in-memory WAV file
        wav_buffer = io.BytesIO()
        with wave.open(wav_buffer, 'wb') as wav_file:
            wav_file.setnchannels(1)
            wav_file.setsampwidth(2)
            wav_file.setframerate(16000)
            wav_file.writeframes(raw_audio_data)
        wav_buffer.seek(0)
        audio_bytes = wav_buffer.read()
        audio_b64_input = base64.b64encode(audio_bytes).decode('utf-8')

        # 2. Call Gemini to get a focused text answer
        url = f"https://generativelanguage.googleapis.com/v1beta/models/gemini-2.5-flash:generateContent?key={GENAI_API_KEY}"
        headers = {'Content-Type': 'application/json'}

        payload = {
            "contents": [{
                "parts": [
                    {
                        "text": (
                            "Context & Instructions:\n"
                            "You are Keren, an intellectually stimulating, articulate, and engaging AI "
                            "assistant designed for two highly intelligent 10-year-old boys.\n"
                            "Address them directly and strictly in the plural male form (Hebrew grammar: "
                            "second-person plural, masculine).\n"
                            "CRITICAL: Never use slashes (like '/' or '\\'), parentheses, or gender "
                            "ambiguity punctuation in your output text, since these break TTS pronunciation.\n"
                            "PROHIBITED PHRASE: do not use the Hebrew phrase for 'young researcher(s)' "
                            "under any circumstances.\n"
                            "Your tone must be deeply respectful, encouraging, and scientific.\n"
                            "When explaining concepts, use real terminology but keep the explanation clear, "
                            "logical, and deeply engaging.\n"
                            "CRITICAL FOR HARDWARE LIMITS: Keep responses highly concise and focused "
                            "(strictly 2-4 sentences). Give maximum information in minimum words.\n\n"
                            "Task: answer the question you heard in the audio file, following these "
                            "instructions only."
                        )
                    },
                    {
                        "inlineData": {
                            "mimeType": "audio/wav",
                            "data": audio_b64_input
                        }
                    }
                ]
            }]
        }

        response = requests.post(url, headers=headers, json=payload)
        res_json = response.json()

        if response.status_code != 200:
            print(f"!!! Google API Error: {res_json}")
            return f"Google API Error: {res_json}", response.status_code

        try:
            answer_text = res_json['candidates'][0]['content']['parts'][0]['text']
        except (KeyError, IndexError):
            answer_text = "Sorry, I couldn't understand the audio. Please try again."

        print(f"Keren Reply Text: {answer_text}")

        # 3. Generate natural neural speech using the configured voice
        communicate = edge_tts.Communicate(answer_text, CURRENT_VOICE)
        asyncio.run(communicate.save(temp_mp3_path))

        # 4. Convert the neural MP3 and apply the configured gain
        sound = AudioSegment.from_file(temp_mp3_path, format="mp3")
        sound = sound + VOLUME_BOOST_DB
        sound = sound.set_frame_rate(16000).set_channels(1).set_sample_width(2)

        wav_output_buffer = io.BytesIO()
        sound.export(wav_output_buffer, format="wav")
        wav_output_buffer.seek(0)

        if os.path.exists(temp_mp3_path):
            os.remove(temp_mp3_path)

        print("Successfully generated high-quality neural voice response. Sending to ESP32...")
        return send_file(wav_output_buffer, mimetype="audio/wav")

    except Exception as e:
        print(f"!!! ERROR: {str(e)}")
        if os.path.exists(temp_mp3_path):
            os.remove(temp_mp3_path)
        return str(e), 500

if __name__ == '__main__':
    app.run(host='0.0.0.0', port=5000, debug=True)
```

---

## 3. Prompt Engineering & Persona Settings

The system prompt is fully tuned for a cognitively engaging experience for **fast-thinking 10-year-old kids**:

- **Assistant name:** Keren.
- **Gender & grammatical register:** consistently addressed in Hebrew second-person plural, masculine form, matching the two boys.
- **Character sanitization:** slashes (`/`) and parentheses are avoided entirely, since they break TTS pronunciation (the engine reads out the punctuation's name instead of the intended word).
- **Banned phrase:** the (Hebrew) phrase for "young researcher(s)" was fully removed after overuse.
- **Conciseness constraint:** hard limit of **2 to 4 focused sentences** per response. This constraint prevents overflowing the ESP32's PSRAM buffer, shortens network round-trip time, and keeps the kids' attention.

---

## 4. Operations Cheat Sheet

All commands below are run directly from the CasaOS terminal (as the `root` user).

### Edit the code

Open the source file directly in a text editor:

```bash
nano /data/AppData/tars-server/app_new.py
```

*(To save and exit: `Ctrl+O` -> `Enter` -> `Ctrl+X`)*

### Restart & apply changes

Must be run after any config change in the code (API key, volume gain, voice, etc.):

```bash
docker restart TARS-Server
```

### Check container status

Check the server's running state and exposed ports:

```bash
docker ps -a
```

### Live logs & troubleshooting

Show the last lines printed by the server, useful for spotting status codes (`200`, `429`, `400`) or Python tracebacks:

```bash
docker logs --tail 20 TARS-Server
```

Follow logs live in real time (recommended while testing a touch interaction):

```bash
docker logs -f TARS-Server
```

### Test / simulate a request (`curl`)

Bypass the ESP32 and send a dummy request directly to the local server to verify connectivity to Google:

```bash
curl -X POST http://127.0.0.1:5000/ask -H "Content-Type: application/octet-stream" --data "1234567890"
```
