# Clock Face Accuracy Reaction Test Raylib + Emscripten Web Game

 **Unit Name:** Advanced Games Programming 25/26 FGCT6012

**Student Name:** Henry Taylor

**Student ID:** 2206046

**Total Word Count:** \[]

**Documentation Link:** https://github.com/University-for-the-Creative-Arts/raylib-webgame-raylib-ramphoryncus/blob/main/README.md

**Repository Link:** https://github.com/University-for-the-Creative-Arts/raylib-webgame-raylib-ramphoryncus

**Build Link:** https://ramphoryncus.itch.io/clock-face-accuracy-reaction-test

**.gif showing game running in browser**
![RaylibWebGame-ezgif com-video-to-gif-converter](https://github.com/user-attachments/assets/197bccb9-f1d0-460b-9fd5-3f5d15afaa7c)

# ClockFace Accuracy & Reaction (raylib + Emscripten)

An accuracy/reaction-time logger: 12 clockface target positions, 100 trials. Outer ring = 5 pts, bullseye = 10 pts. Must return to centre and click to spawn the next target. Exports CSVs; optional POST of summary JSON (Web only).


---

## Build (Emscripten)

**Prereqs**
- Emscripten SDK installed & activated:
  ```powershell
  cd "$env:USERPROFILE\emsdk"
  .\emsdk_env.ps1
  ```
# raylib built for Web (one-time):

  ```powershell

  cd "C:\Users\htayl\raylib\raylib\src"
  emmake make PLATFORM=PLATFORM_WEB -j
```
Compile to /web (emcc, as requested)

```powershell

cd "F:\GitHubRepo\raylib-webgame-raylib-ramphoryncus"
emcc .\src\main.cpp -o .\web\index.html `
 -I"C:\Users\htayl\raylib\raylib\src" `
 -L"C:\Users\htayl\raylib\raylib\src" `
 -lraylib -DPLATFORM_WEB `
 -sUSE_GLFW=3 -sFETCH=1 -sASYNCIFY -sALLOW_MEMORY_GROWTH=1 -O3
```
You can use em++ instead of emcc for C++ sources; both work with the flags above.

(Optional) One-click script:

```powershell

powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\build-web.ps1
```
Outputs dist/web/index.html|.js|.wasm and a zip for itch.

# Serve & play locally
Python (simple static server)

```bash

cd web
python -m http.server 8000
```
# visit http://localhost:8000/

# Emscripten runner

```bash

emrun web/index.html
```

Opening index.html via file:// is not recommended; serve it to avoid security/CORS issues (and to mimic itch.io).

Web request (source / API)
At the end of a run (Web build only), the game posts a JSON summary using the Emscripten Fetch API (#include <emscripten/fetch.h>, linked via -sFETCH=1).

Method: POST

Endpoint: set POST_ENDPOINT inside PostJSON() in main.cpp (default is a placeholder).

Headers: Content-Type: application/json

Payload example:

```json

{
  "trials": 100,
  "hits": 97,
  "bullseyes": 88,
  "totalScore": 930,
  "avgReactionMs": 812.4,
  "hitRate": 0.97
}
```
CORS: Your server must allow the game’s origin (itch.io embed) via Access-Control-Allow-Origin.

If you don’t have a server yet, comment out the PostJSON(json); call; everything else still works.

# Controls
Return to centre and click to spawn target

R = restart

D = download detailed trials CSV

P = download per-direction summary CSV


