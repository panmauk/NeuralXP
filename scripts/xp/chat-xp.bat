@echo off
REM ============================================
REM   Quick Chat - Single Question Mode
REM ============================================
REM Usage: chat-xp.bat "What is the capital of France?"
REM

if "%~1"=="" (
    echo Usage: chat-xp.bat "your question here"
    echo Example: chat-xp.bat "Write a simple Python hello world program"
    pause
    exit /b 1
)

if exist "models\qwen2.5-0.5b-instruct-q2_k.gguf" (
    set MODEL=models\qwen2.5-0.5b-instruct-q2_k.gguf
) else if exist "models\SmolLM-135M-Instruct.Q4_K_M.gguf" (
    set MODEL=models\SmolLM-135M-Instruct.Q4_K_M.gguf
) else (
    echo ERROR: No model found in models\ folder!
    pause
    exit /b 1
)

REM llama-simple-chat.exe only accepts -m/-c/-n/-ngl (it printed its usage and exited for the old
REM --no-mmap/--ctx-size/-t/-p flags), so single questions go to the headless helper the GUI uses.
llama-simple-chat-headless.exe -m %MODEL% ^
    -c 512 ^
    -n 200 ^
    -p "%~1" ^
    2>nul

echo.
pause
