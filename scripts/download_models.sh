#!/bin/bash
# download_models.sh - Download and prepare models for RTV

set -e

MODELS_DIR="$(dirname "$0")/../models"
mkdir -p "$MODELS_DIR/whisper" "$MODELS_DIR/gemma"

echo "=== Rosey The Voice - Model Downloader ==="

download_whisper() {
    local model="$1"
    local url="https://huggingface.co/ggerganov/whisper.cpp/resolve/main"
    
    case "$model" in
        whisper-medium)
            echo "Downloading whisper-medium-q5_0..."
            curl -L "$url/ggml-medium-q5_0.bin" -o "$MODELS_DIR/whisper/ggml-medium-q5_0.bin"
            ;;
        whisper-small)
            echo "Downloading whisper-small..."
            curl -L "$url/ggml-small.bin" -o "$MODELS_DIR/whisper/ggml-small.bin"
            ;;
        *)
            echo "Unknown whisper model: $model"
            exit 1
            ;;
    esac
}

download_piper() {
    echo "Downloading Piper PT-BR voice..."
    local url="https://huggingface.co/rhasspy/piper-voices/resolve/main"
    mkdir -p "$MODELS_DIR/piper"
    curl -L "$url/pt/pt_BR/faber/medium/pt_BR-faber-medium.onnx" -o "$MODELS_DIR/piper/pt_BR-faber-medium.onnx"
    curl -L "$url/pt/pt_BR/faber/medium/pt_BR-faber-medium.onnx.json" -o "$MODELS_DIR/piper/pt_BR-faber-medium.onnx.json"
}

case "$1" in
    whisper-medium|whisper-small)
        download_whisper "$1"
        ;;
    piper-ptbr)
        download_piper
        ;;
    all)
        download_whisper whisper-medium
        download_piper
        echo "Note: Gemma models require manual download from HuggingFace/Kaggle"
        ;;
    *)
        echo "Usage: $0 {whisper-medium|whisper-small|piper-ptbr|all}"
        exit 1
        ;;
esac

echo "Done!"
