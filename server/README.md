QBOT Proxy Server

Setup
1. Create and activate a Python 3.11+ virtualenv.
2. Install dependencies:

```bash
pip install -r requirements.txt
```

3. Download a VOSK model and set `VOSK_MODEL_PATH` env var, or run with no model (STT will warn).
4. Optionally set `GOOGLE_API_KEY` env var with a valid OAuth2 access token for Generative API.

Run

```bash
export VOSK_MODEL_PATH=./model
export GOOGLE_API_KEY=ya29.xxx
python app.py
```

Endpoints
- `POST /stt` - body: `audio/wav` → returns recognized text
- `POST /generate` - JSON `{text}` → returns generated text
- `POST /tts` - JSON `{text}` → returns `audio/wav`
