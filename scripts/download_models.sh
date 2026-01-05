#!/bin/bash
# download_models.sh - Download and prepare models for RTV

set -e

MODELS_DIR="$(dirname "$0")/../models"
mkdir -p "$MODELS_DIR/whisper" "$MODELS_DIR/gemma" "$MODELS_DIR/tts"

echo "=== Rosey The Voice - Model Downloader ==="

download_whisper() {
    echo "Downloading whisper-small-q5_1 (~181MB)..."
    local url="https://huggingface.co/ggerganov/whisper.cpp/resolve/main"
    curl -L "$url/ggml-small-q5_1.bin" -o "$MODELS_DIR/whisper/ggml-small-q5_1.bin"
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

download_tts_voice() {
    echo "Downloading reference voice for XTTS..."
    echo ""
    echo "XTTS requires a reference audio file for voice cloning."
    echo "You need to provide a 6-30 second WAV file of the desired voice."
    echo ""
    echo "Place your reference audio at: $MODELS_DIR/tts/reference_voice.wav"
    echo ""
    echo "Example: Record yourself or use a royalty-free voice sample."
    echo ""
    
    # Create placeholder
    if [ ! -f "$MODELS_DIR/tts/reference_voice.wav" ]; then
        echo "# Place your reference voice WAV file here" > "$MODELS_DIR/tts/README.txt"
        echo "# The file should be:"  >> "$MODELS_DIR/tts/README.txt"
        echo "#   - 6-30 seconds of clear speech" >> "$MODELS_DIR/tts/README.txt"
        echo "#   - WAV format, 16-bit, mono or stereo" >> "$MODELS_DIR/tts/README.txt"
        echo "#   - Named: reference_voice.wav" >> "$MODELS_DIR/tts/README.txt"
        echo "Created $MODELS_DIR/tts/README.txt with instructions."
    fi
}

setup_xtts() {
    echo "Setting up Coqui XTTS v2..."
    echo ""
    
    # Check if TTS is installed
    if ! command -v tts &> /dev/null; then
        echo "Installing Coqui TTS with pinned dependencies..."
        pip install TTS torch==2.1.0 torchaudio==2.1.0 transformers==4.39.3
    fi
    
    # Pre-download XTTS model
    echo "Pre-downloading XTTS v2 model (~1.5GB)..."
    tts --model_name tts_models/multilingual/multi-dataset/xtts_v2 \
        --text "teste" \
        --language_idx pt \
        --out_path /tmp/xtts_test.wav 2>/dev/null || true
    
    rm -f /tmp/xtts_test.wav
    
    echo "XTTS v2 ready!"
    download_tts_voice
}

case "$1" in
    whisper)
        download_whisper
        ;;
    gemma)
        download_gemma
        ;;
    tts)
        setup_xtts
        ;;
    all)
        download_whisper
        download_gemma
        setup_xtts
        ;;
    *)
        echo "Usage: $0 {whisper|gemma|tts|all}"
        echo ""
        echo "Models:"
        echo "  whisper  - Speech-to-Text (small-q5_1, ~181MB)"
        echo "  gemma    - Gemma 3 12B QAT Q4_0 from Google (~7GB)"
        echo "  tts      - Setup XTTS v2 for voice synthesis (~1.5GB)"
        echo "  all      - Download/setup all models"
        exit 1
        ;;
esac

echo "Done!"
