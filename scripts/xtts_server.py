#!/usr/bin/env python3
"""
xtts_server.py - Persistent XTTS TTS Server with Speaker Embedding Caching

Starts an HTTP server that keeps the model loaded for fast inference.
Speaker embedding is computed once at startup.

Usage:
    python3 xtts_server.py --reference models/tts/reference_voice.wav --port 5050
    
API:
    POST /synthesize
    Body: {"text": "Hello world"}
    Response: WAV file
"""

import sys
import os
import argparse
import io
import torch
import scipy.io.wavfile as wavfile
from http.server import HTTPServer, BaseHTTPRequestHandler
import json

# Global server instance
_xtts_server = None

class XTTSModel:
    def __init__(self, reference_audio: str, language: str = "pt"):
        self.language = language
        self.device = "cpu"
        self.sample_rate = 24000
        
        print(f"[XTTS] Loading model...", file=sys.stderr)
        from TTS.api import TTS
        self.tts = TTS("tts_models/multilingual/multi-dataset/xtts_v2").to(self.device)
        self.sample_rate = self.tts.synthesizer.output_sample_rate
        
        print(f"[XTTS] Computing speaker embedding from: {reference_audio}", file=sys.stderr)
        self.gpt_cond_latent, self.speaker_embedding = self._compute_speaker_embedding(reference_audio)
        
        print(f"[XTTS] Ready! Speaker embedding cached. Sample rate: {self.sample_rate}", file=sys.stderr)
    
    def _compute_speaker_embedding(self, audio_path: str):
        """Pre-compute speaker conditioning latents from reference audio."""
        return self.tts.synthesizer.tts_model.get_conditioning_latents(
            audio_path=audio_path,
            gpt_cond_len=30,
            gpt_cond_chunk_len=4,
            max_ref_length=60
        )
    
    def synthesize(self, text: str) -> bytes:
        """Synthesize speech using cached speaker embedding. Returns WAV bytes."""
        # Use the model's inference method with cached embeddings
        wav = self.tts.synthesizer.tts_model.inference(
            text=text,
            language=self.language,
            gpt_cond_latent=self.gpt_cond_latent,
            speaker_embedding=self.speaker_embedding,
        )
        
        # inference returns dict with 'wav' key
        if isinstance(wav, dict):
            wav = wav['wav']
        
        # Convert to numpy array if tensor
        import numpy as np
        if hasattr(wav, 'cpu'):
            wav = wav.cpu().numpy()
        wav = np.array(wav).flatten()
        
        # Convert to WAV bytes
        buffer = io.BytesIO()
        wavfile.write(buffer, self.sample_rate, wav)
        return buffer.getvalue()


class TTSHandler(BaseHTTPRequestHandler):
    def log_message(self, format, *args):
        # Quieter logging
        pass
    
    def do_GET(self):
        if self.path == "/health":
            self.send_response(200)
            self.send_header("Content-Type", "application/json")
            self.end_headers()
            self.wfile.write(b'{"status":"ok"}')
        else:
            self.send_error(404)
    
    def do_POST(self):
        global _xtts_server
        
        if self.path == "/synthesize":
            try:
                content_length = int(self.headers.get("Content-Length", 0))
                body = self.rfile.read(content_length).decode("utf-8")
                data = json.loads(body)
                text = data.get("text", "")
                
                if not text:
                    self.send_error(400, "Missing 'text' field")
                    return
                
                print(f"[XTTS] Synthesizing: {text[:50]}...", file=sys.stderr)
                wav_bytes = _xtts_server.synthesize(text)
                
                self.send_response(200)
                self.send_header("Content-Type", "audio/wav")
                self.send_header("Content-Length", len(wav_bytes))
                self.end_headers()
                self.wfile.write(wav_bytes)
                
            except Exception as e:
                print(f"[XTTS] Error: {e}", file=sys.stderr)
                self.send_error(500, str(e))
        else:
            self.send_error(404)


def run_server(reference_audio: str, port: int = 5050, language: str = "pt"):
    global _xtts_server
    
    _xtts_server = XTTSModel(reference_audio, language)
    
    server = HTTPServer(("0.0.0.0", port), TTSHandler)
    print(f"[XTTS] HTTP Server running on http://localhost:{port}", file=sys.stderr)
    print(f"[XTTS] POST /synthesize with JSON {{\"text\": \"...\"}}", file=sys.stderr)
    
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        print("\n[XTTS] Shutting down...", file=sys.stderr)
        server.shutdown()


def single_shot(reference_audio: str, text: str, output: str, language: str = "pt"):
    """Single-shot mode for backwards compatibility."""
    model = XTTSModel(reference_audio, language)
    wav_bytes = model.synthesize(text)
    
    with open(output, "wb") as f:
        f.write(wav_bytes)
    print(f"Saved to: {output}")


def main():
    parser = argparse.ArgumentParser(description="XTTS TTS Server")
    parser.add_argument("--reference", "-r", required=True, 
                        help="Path to reference voice WAV file")
    parser.add_argument("--language", "-l", default="pt",
                        help="Language code (default: pt)")
    parser.add_argument("--port", "-p", type=int, default=5050,
                        help="HTTP server port (default: 5050)")
    parser.add_argument("--text", "-t", default=None,
                        help="Text to synthesize (single shot mode)")
    parser.add_argument("--output", "-o", default="/tmp/xtts_output.wav",
                        help="Output WAV file path (single shot mode)")
    parser.add_argument("--server", "-s", action="store_true",
                        help="Run in HTTP server mode (recommended)")
    
    args = parser.parse_args()
    
    if not os.path.exists(args.reference):
        print(f"Error: Reference audio not found: {args.reference}", file=sys.stderr)
        sys.exit(1)
    
    if args.server:
        run_server(args.reference, args.port, args.language)
    elif args.text:
        single_shot(args.reference, args.text, args.output, args.language)
    else:
        # Default to server mode
        run_server(args.reference, args.port, args.language)


if __name__ == "__main__":
    main()
