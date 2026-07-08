"""QBOT proxy server
- /stt : accepts audio/wav POST -> returns recognized text
- /generate : accepts JSON {text} -> calls Gemini API -> returns text
- /tts : accepts JSON {text} -> returns audio/wav (simple gTTS fallback)
"""
from flask import Flask, request, jsonify, send_file
from flask_cors import CORS
import os
import io
import wave
import json
import requests
from vosk import Model, KaldiRecognizer
import soundfile as sf
from gtts import gTTS

app = Flask(__name__)
CORS(app)

MODEL_PATH = os.environ.get('VOSK_MODEL_PATH','./model')

if not os.path.exists(MODEL_PATH):
    print("WARNING: VOSK model path does not exist:", MODEL_PATH)
else:
    model = Model(MODEL_PATH)

@app.route('/stt', methods=['POST'])
def stt():
    # Expect WAV file in body
    data = request.data
    if not data:
        return "", 400
    wf = wave.open(io.BytesIO(data))
    sr = wf.getframerate()
    rec = KaldiRecognizer(model, sr)
    rec.SetWords(True)
    while True:
        chunk = wf.readframes(4000)
        if len(chunk)==0:
            break
        if rec.AcceptWaveform(chunk):
            pass
    res = json.loads(rec.FinalResult())
    text = res.get('text','')
    return text

@app.route('/generate', methods=['POST'])
def generate():
    body = request.get_json(force=True)
    text = body.get('text','')
    if not text:
        return jsonify({'error':'no text'}), 400
    # Call Gemini via Google Gen AI API - user must set env vars
    api_key = os.environ.get('GOOGLE_API_KEY')
    if not api_key:
        # For demo, echo back
        return text
    url = 'https://generativeai.googleapis.com/v1beta2/models/text-bison-001:generate'
    headers = {'Authorization': f'Bearer {api_key}', 'Content-Type':'application/json'}
    payload = {"prompt": {"text": text}, "temperature":0.2}
    r = requests.post(url, headers=headers, json=payload)
    if r.status_code!=200:
        return jsonify({'error':'gen failed', 'detail': r.text}), 500
    j = r.json()
    # Navigate response to extract text
    out = j.get('candidates',[{}])[0].get('content','')
    return out

@app.route('/tts', methods=['POST'])
def tts():
    body = request.get_json(force=True)
    text = body.get('text','')
    if not text:
        return jsonify({'error':'no text'}), 400
    # Use gTTS to produce a simple MP3 then convert to WAV
    tts = gTTS(text)
    mp3_buf = io.BytesIO()
    tts.write_to_fp(mp3_buf)
    mp3_buf.seek(0)
    # Convert MP3 to WAV via pydub
    from pydub import AudioSegment
    audio = AudioSegment.from_file(mp3_buf, format='mp3')
    wav_buf = io.BytesIO()
    audio = audio.set_frame_rate(16000).set_channels(1)
    audio.export(wav_buf, format='wav')
    wav_buf.seek(0)
    return send_file(wav_buf, mimetype='audio/wav', as_attachment=False, download_name='tts.wav')

if __name__ == '__main__':
    app.run(host='0.0.0.0', port=5000)
