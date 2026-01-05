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

case "$1" in
    whisper)
        download_whisper
        ;;
    piper)
        download_piper
        ;;
    all)
        download_whisper
        download_piper
        echo "Note: Gemma models require manual download from HuggingFace/Kaggle"
        ;;
    *)
        echo "Usage: $0 {whisper|piper|all}"
        exit 1
        ;;
esac

echo "Done!"
