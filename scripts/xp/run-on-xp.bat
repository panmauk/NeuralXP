@echo off
REM ============================================
REM   LLM for Windows XP - Inspiron 6000
REM   Qwen2.5-0.5B-Instruct
REM ============================================
REM
REM Copy this entire folder to C:\llama on your XP machine
REM Then double-click this file or run it from Command Prompt
REM

title NeuralXP for Windows XP
color 1F
cls

echo.
echo  ============================================
echo    NeuralXP for Windows XP
echo    Running on Dell Inspiron 6000
echo  ============================================
echo.
echo  Model: Qwen2.5-0.5B-Instruct (Q2_K)
echo  Speed: ~2-8 tokens/sec (be patient!)
echo.
echo  Type your question and press Enter.
echo  Press Ctrl+C to exit.
echo.
echo  Tips:
echo   - Keep questions short and specific
echo   - For code, say "write a simple [language] program that..."
echo   - For facts, just ask directly
echo   - Shorter answers = faster responses
echo.
echo  ============================================
echo.

REM Check if model exists
if exist "models\qwen2.5-0.5b-instruct-q2_k.gguf" (
    set MODEL=models\qwen2.5-0.5b-instruct-q2_k.gguf
) else if exist "models\SmolLM-135M-Instruct.Q4_K_M.gguf" (
    set MODEL=models\SmolLM-135M-Instruct.Q4_K_M.gguf
) else (
    echo  ERROR: No model found in models\ folder!
    echo  Make sure you copied the .gguf file to the models\ folder.
    pause
    exit /b 1
)

llama-simple-chat.exe -m %MODEL% -c 256

pause
