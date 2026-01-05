#!/bin/bash
# download_models.sh - Download and prepare models for RTV

set -e

MODELS_DIR="$(dirname "$0")/../models"
mkdir -p "$MODELS_DIR/whisper" "$MODELS_DIR/gemma" "$MODELS_DIR/piper"

echo "=== Rosey The Voice - Model Downloader ==="

download_whisper() {
    echo "Downloading whisper-small-q5_1 (~181MB)..."
    local url="https://huggingface.co/ggerganov/whisper.cpp/resolve/main"
    curl -L "$url/ggml-small-q5_1.bin" -o "$MODELS_DIR/whisper/ggml-small-q5_1.bin"
}

download_piper() {
    echo "Downloading Piper PT-BR voice..."
    local url="https://huggingface.co/rhasspy/piper-voices/resolve/main"
    curl -L "$url/pt/pt_BR/faber/medium/pt_BR-faber-medium.onnx" -o "$MODELS_DIR/piper/pt_BR-faber-medium.onnx"
    curl -L "$url/pt/pt_BR/faber/medium/pt_BR-faber-medium.onnx.json" -o "$MODELS_DIR/piper/pt_BR-faber-medium.onnx.json"
}

download_gemma() {
    echo "Downloading Gemma 3 12B QAT Q4_0 GGUF (~7GB) from Google..."
    echo ""
    echo "Note: Gemma requires license acceptance at:"
    echo "  https://huggingface.co/google/gemma-3-12b-it-qat-q4_0-gguf"
    echo ""
    
    # Download using hf CLI (uses stored token)
    hf download \
        google/gemma-3-12b-it-qat-q4_0-gguf \
        gemma-3-12b-it-q4_0.gguf \
        --local-dir "$MODELS_DIR/gemma"
    
    echo "Gemma 3 12B (Google official QAT) downloaded!"
}

case "$1" in
    whisper)
        download_whisper
        ;;
    piper)
        download_piper
        ;;
    gemma)
        download_gemma
        ;;
    all)
        download_whisper
        download_piper
        download_gemma
        ;;
    *)
        echo "Usage: $0 {whisper|piper|gemma|all}"
        echo ""
        echo "Models:"
        echo "  whisper  - Speech-to-Text (small-q5_1, ~181MB)"
        echo "  piper    - Text-to-Speech PT-BR (~50MB)"
        echo "  gemma    - Gemma 3 12B QAT Q4_0 from Google (~7GB)"
        echo "  all      - Download all models"
        echo ""
        echo "Note: For gemma, you must first accept the license at:"
        echo "  https://huggingface.co/google/gemma-3-12b-it-qat-q4_0-gguf"
        exit 1
        ;;
esac

echo "Done!"
