# Rosey The Voice (RTV)

> Assistente de voz local-first baseado em Google Gemma 3, inspirado na Rosey the Robot do desenho *The Jetsons*.

[![License](https://img.shields.io/badge/License-Apache%202.0-blue.svg)](LICENSE)
[![C++](https://img.shields.io/badge/C%2B%2B-20-blue.svg)](https://isocpp.org/)

---

## Funcionalidades

| Funcionalidade | Tecnologia |
|----------------|------------|
| Wake Word | OpenWakeWord ("Hey Rosey") |
| Fala para Texto | Whisper Small Q5 (PT-BR) |
| Conversação | Gemma 3 12B |
| Detecção de Ações | FunctionGemma 270M |
| Embeddings RAG | EmbeddingGemma 308M |
| Texto para Fala | Piper TTS (offline) |
| Barge-in | WebRTC AEC3 |
| Modo Offline | Cache SQLite (email/calendário) |

---

## Requisitos

### Hardware
- **CPU**: x86_64 com suporte a AVX2
- **RAM**: 16GB mínimo, 32GB+ recomendado
- **Armazenamento**: 20GB para modelos

### Software
- Linux (Ubuntu 22.04+ recomendado)
- Python 3.9 a 3.11 (requerido pelo modulo TTS)
- CMake 3.20+
- GCC 11+ ou Clang 14+
- Docker e Docker Compose

---

## Início Rápido

### 1. Instalar Dependencias do Sistema

```bash
# Ubuntu/Debian
sudo apt update
sudo apt install -y \
    build-essential \
    cmake \
    git \
    libboost-all-dev \
    libsqlite3-dev \
    portaudio19-dev \
    libfftw3-dev \
    python3-pip \
    curl

# HuggingFace CLI (para baixar modelos Gemma)
pip install huggingface_hub[cli]
hf login  # inserir token do HuggingFace

# Coqui TTS XTTS v2 (sintese de voz com clonagem)
# Requer versoes especificas para compatibilidade
pip install TTS torch==2.1.0 torchaudio==2.1.0 transformers==4.39.3

# libfvad (compilar manualmente)
git clone https://github.com/dpirch/libfvad.git
cd libfvad
autoreconf -i && ./configure && make && sudo make install
sudo ldconfig
cd ..

# Verificar
cmake --version   # >= 3.20
g++ --version     # >= 11
```

### 2. Clonar o Repositório

```bash
git clone https://github.com/lucianommartins/roseyTheVoice.git
cd roseyTheVoice
```

### 3. Baixar Modelos

```bash
# Whisper (STT) - ~181MB
./scripts/download_models.sh whisper

# Gemma 3 12B (LLM) - ~7GB (requer aceitar licença no HuggingFace primeiro)
# Acesse: https://huggingface.co/google/gemma-3-12b-it-qat-q4_0-gguf
./scripts/download_models.sh gemma

# XTTS v2 (TTS) - ~1.5GB + audio de referencia para voz
./scripts/download_models.sh tts

# Ou baixar todos de uma vez:
./scripts/download_models.sh all
```

**Nota sobre TTS**: O XTTS v2 usa clonagem de voz. Voce precisa fornecer um audio
de referencia da voz desejada:

```bash
# Opcao 1: Gravar com microfone (10-15 segundos de fala clara)
arecord -d 15 -f S16_LE -r 22050 models/tts/reference_voice.wav

# Opcao 2: Converter audio existente para WAV
ffmpeg -i seu_audio.mp3 -ar 22050 -ac 1 models/tts/reference_voice.wav
```

**Requisitos do audio de referencia:**
- Duracao: 6-30 segundos
- Conteudo: fala clara, sem musica ou ruido
- Formato: WAV 16-bit
- Local: `models/tts/reference_voice.wav`

### 4. Compilar

```bash
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)
```

### 5. Testar Audio Pipeline

```bash
# Da raiz do projeto (onde está a pasta models/)
./build/test_ring_buffer
./build/test_aec3_pipeline
./build/rtv_audio_test
```

### 6. Testar Speech-to-Text

```bash
# Transcrição ao vivo do microfone
./build/rtv_live_transcription
# Fale no microfone e veja a transcrição
# Ctrl+C para sair
```

### 7. Iniciar Servidores LLM (em breve)

```bash
# Subir containers Docker (Gemma 12B, FunctionGemma, EmbeddingGemma)
docker-compose up -d

# Verificar
docker-compose ps
```

### 8. Executar

```bash
./build/rtv
```

---

## Arquitetura

```
+-------------------------------------------------------------------+
|                      ROSEY THE VOICE (RTV)                        |
+-------------------------------------------------------------------+
|                                                                   |
|  [Mic] -> AEC3 -> VAD -> Whisper -> FunctionGemma -> Executor     |
|                                          |                        |
|                               Gemma 3 12B -> Piper -> [Speaker]   |
|                                                                   |
+-------------------------------------------------------------------+
```

### Modulos

| Modulo | Descricao |
|--------|-----------|
| `audio/` | PortAudio, AEC3, VAD, Ring Buffer |
| `stt/` | Wrapper para Whisper.cpp |
| `llm/` | ActionDetector, ConversationEngine, EmbeddingEngine |
| `tts/` | Piper TTS |
| `cache/` | SQLite para modo offline |
| `ipc/` | Shared memory (boost::interprocess) |
| `orchestrator/` | Maquina de estados principal |

---

## Acoes Suportadas

| Acao | Descricao | Requer Online |
|------|-----------|---------------|
| `play_music` | Tocar no YouTube Music | Sim |
| `check_calendar` | Listar compromissos | Nao (cache) |
| `add_calendar_event` | Criar evento | Sim |
| `send_email` | Enviar via Gmail | Sim |
| `check_email` | Listar emails | Nao (cache) |
| `search_web` | Buscar no Google | Sim |
| `get_weather` | Previsao do tempo | Sim |
| `control_media` | Play/pause/volume | Nao |

---

## Configuracao

### Variaveis de Ambiente

```bash
export RTV_WAKE_WORD="hey rosey"
export RTV_LANGUAGE="pt"
export RTV_LOG_LEVEL="info"
```

### Configurar APIs do Google (para funcionalidades online)

1. Crie um projeto no [Google Cloud Console](https://console.cloud.google.com)
2. Ative as APIs: Calendar, Gmail, YouTube Data, Custom Search
3. Crie credenciais OAuth 2.0
4. Salve `credentials.json` na raiz do projeto
5. Execute o RTV e autorize na primeira execucao

---

## Desenvolvimento

### Executar Testes

```bash
cd build
ctest --output-on-failure
```

### Estrutura do Projeto

```
roseyTheVoice/
├── CMakeLists.txt
├── docker-compose.yml
├── src/
│   ├── main.cpp
│   ├── audio/
│   ├── stt/
│   ├── llm/
│   ├── tts/
│   ├── cache/
│   ├── ipc/
│   └── orchestrator/
├── include/rtv/
├── tests/
├── scripts/
└── models/          # ignorado pelo git
```

---

## Status de Implementacao

| Fase | Componente | Status |
|------|------------|--------|
| 1 | Core Setup (CMake, Deps) | Completo |
| 2 | Audio Pipeline (PortAudio, VAD, AEC3) | Completo |
| 3 | Speech-to-Text (whisper.cpp) | Completo |
| 4 | LLM Integration | Pendente |
| 5 | Text-to-Speech (Piper) | Pendente |
| 6 | Orchestrator | Pendente |

---

## Licenca

Apache 2.0 - Veja [LICENSE](LICENSE)

---

## Agradecimentos

- [whisper.cpp](https://github.com/ggerganov/whisper.cpp)
- [llama.cpp](https://github.com/ggerganov/llama.cpp)
- [Piper TTS](https://github.com/rhasspy/piper)
- [WebRTC AEC3](https://github.com/AEC3)
- [Google Gemma](https://ai.google.dev/gemma)
