#!/usr/bin/env bash
# Start a local MLX-LM OpenAI-compatible server on Apple Silicon.
#
# Logotron + the engine's optional LLM path target this server when
# LOGOTRON_LLM_PROVIDER=mlx is set (or when no cloud key is in the
# env). MLX uses Apple's Metal Performance Shaders, so it's the
# native Mac-GPU option — vLLM has no Metal backend.
#
# Pre-req: pip install mlx-lm  (or use the existing miniconda env).
#
# Override knobs:
#   MLX_MODEL  HuggingFace repo (default qwen2.5-14b-instruct-4bit)
#   MLX_PORT   listen port (default 8080)
#
# Once running, Logotron picks it up with:
#   export LOGOTRON_LLM_PROVIDER=mlx
#   export LOGOTRON_LLM_URL="http://localhost:${MLX_PORT}"
#   export LOGOTRON_LLM_MODEL="${MLX_MODEL}"

set -euo pipefail

MLX_MODEL="${MLX_MODEL:-mlx-community/Qwen2.5-14B-Instruct-4bit}"
MLX_PORT="${MLX_PORT:-8080}"

if ! command -v mlx_lm.server >/dev/null 2>&1; then
    echo "mlx_lm.server not found on PATH." >&2
    echo "Install: pip install mlx-lm" >&2
    exit 1
fi

echo "Starting mlx_lm.server"
echo "  model: ${MLX_MODEL}"
echo "  port:  ${MLX_PORT}"
echo "  url:   http://localhost:${MLX_PORT}"
echo

exec mlx_lm.server --model "${MLX_MODEL}" --port "${MLX_PORT}"
