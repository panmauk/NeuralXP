#!/bin/bash
# download-model.sh
# Downloads the Qwen2.5-0.5B-Instruct model (Q2_K quantization)
# This is the best model for your Inspiron 6000: small enough to fit in RAM,
# smart enough for basic Q&A, simple coding, and school facts.

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
DEPLOY_DIR="$PROJECT_DIR/deploy"

mkdir -p "$DEPLOY_DIR/models"

echo "============================================"
echo "  Model Downloader for XP LLM"
echo "============================================"
echo ""

# --- Primary model: Qwen2.5-0.5B-Instruct Q2_K (~300MB) ---
MODEL_URL="https://huggingface.co/Qwen/Qwen2.5-0.5B-Instruct-GGUF/resolve/main/qwen2.5-0.5b-instruct-q2_k.gguf"
MODEL_FILE="$DEPLOY_DIR/models/qwen2.5-0.5b-instruct-q2_k.gguf"

if [ -f "$MODEL_FILE" ]; then
    echo "Model already downloaded: $MODEL_FILE"
else
    echo "Downloading Qwen2.5-0.5B-Instruct (Q2_K, ~300MB)..."
    echo "This model can handle:"
    echo "  - Basic Q&A and school facts"
    echo "  - Simple code snippets"
    echo "  - Short text generation"
    echo ""

    if command -v curl &>/dev/null; then
        curl -L --progress-bar "$MODEL_URL" -o "$MODEL_FILE"
    elif command -v wget &>/dev/null; then
        wget --show-progress "$MODEL_URL" -O "$MODEL_FILE"
    else
        echo "ERROR: Neither curl nor wget found. Download manually from:"
        echo "  $MODEL_URL"
        echo "Save to: $MODEL_FILE"
        exit 1
    fi
fi

echo ""

# --- Optional: Also download SmolLM-135M as ultra-light backup ---
SMOL_URL="https://huggingface.co/QuantFactory/SmolLM-135M-Instruct-GGUF/resolve/main/SmolLM-135M-Instruct.Q4_K_M.gguf"
SMOL_FILE="$DEPLOY_DIR/models/SmolLM-135M-Instruct.Q4_K_M.gguf"

echo "Also downloading SmolLM-135M (Q4_K_M, ~105MB) as a lightweight backup..."
if [ -f "$SMOL_FILE" ]; then
    echo "Already downloaded: $SMOL_FILE"
else
    if command -v curl &>/dev/null; then
        curl -L --progress-bar "$SMOL_URL" -o "$SMOL_FILE" || echo "WARNING: SmolLM download failed (optional, continuing...)"
    elif command -v wget &>/dev/null; then
        wget --show-progress "$SMOL_URL" -O "$SMOL_FILE" || echo "WARNING: SmolLM download failed (optional, continuing...)"
    fi
fi

echo ""
echo "============================================"
echo "  Download complete!"
echo "============================================"
echo ""
echo "Models saved to: $DEPLOY_DIR/models/"
ls -lh "$DEPLOY_DIR/models/" 2>/dev/null
echo ""
echo "Copy the entire 'deploy' folder to your USB drive."
