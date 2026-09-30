# Specyfikacja dla Claude Code — Windows soft-TEMPEST display masking overlay

> Wersja Windows, równoległa do `SPEC.md` (Android). Ta sama idea: pełnoekranowa,
> klikalna-na-wylot warstwa szumu pikseli losowanego **co klatkę**, składana przez
> kompozytor systemowy (DWM) nad całą treścią. Narzędzie **ochronne** dla własnego
> urządzenia — nic nie przechwytuje, nie nadaje, nie atakuje.

## Cel projektu

Zbuduj aplikację desktopową na Windows, która renderuje pełnoekranową (na wszystkich
monitorach) warstwę szumu z per-pikselowym kanałem alfa, aby podnieść poziom szumu w
emisji elektromagnetycznej toru wyświetlania (soft TEMPEST wg Kuhn & Anderson, 1998).
Warstwa musi być przezroczysta dla wejścia (mysz/klawiatura przechodzą pod spód),
zawsze na wierzchu i renderowana przez GPU.

## Model zagrożenia i świadome ograniczenia (WAŻNE — umieść w README)

1. **Bezpieczny pulpit (Secure Desktop).** Monity UAC, ekran Ctrl+Alt+Del, logowanie i
   Windows Hello działają na osobnym pulpicie, do którego zwykła aplikacja nie ma
   dostępu. Overlay tam się NIE pojawi. Tego nie obchodzimy.
2. **Treść chroniona (DRM/OPM).** Sprzętowo chroniony wideo (PlayReady, chroniona
   ścieżka wyjścia) może iść torem, którego overlay nie pokryje.
3. **Ekskluzywny fullscreen.** Gry/aplikacje w prawdziwym exclusive fullscreen
   (flip model) potrafią ominąć kompozycję DWM — wtedy overlay nie jest widoczny nad
   nimi. Borderless-windowed działa normalnie.
4. **Uśrednianie N ramek.** Statyczny szum spada jako √N. Dlatego szum MUSI być
   losowany niezależnie w każdej klatce (time-seeded PRNG w pixel shaderze). Naiwny
   statyczny overlay = złudzenie ochrony.
5. **Bez pomiaru nie ma dowodu skuteczności.** To prototyp/uzupełnienie ekranowania
   sprzętowego, nie zamiennik. README ma to mówić wprost.
6. **Bateria/GPU.** Ciągłe renderowanie pełnoekranowe obciąża GPU; na laptopie skróci
   czas pracy. Rozważ limit FPS.

## Stack techniczny

- Język: **C++17** (Win32 API, bez frameworków GUI).
- Grafika: **Direct3D 11 + DXGI + DirectComposition** — swapchain z per-pikselową alfą
  na oknie warstwowym, animowany pixel shaderem. DirectComposition to właściwa droga do
  przezroczystej, animowanej nakładki (nie `UpdateLayeredWindow`, który jest zbyt wolny
  dla pełnoekranowej animacji co klatkę).
- Shadery: **HLSL**, kompilowane w buildzie przez `fxc` (lub `D3DCompile` w runtime).
- Build: **CMake + MSVC (Visual Studio 2022)**.
- CI: **GitHub Actions**, runner `windows-latest`, artefakt `.exe`.

## Architektura okna

- Klasa okna `WS_POPUP`, rozmiar = **virtual screen** (wszystkie monitory):
  `GetSystemMetrics(SM_XVIRTUALSCREEN / SM_YVIRTUALSCREEN / SM_CXVIRTUALSCREEN /
  SM_CYVIRTUALSCREEN)`.
- Styl rozszerzony:
  `WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOPMOST | WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW`
  - `LAYERED` + DirectComposition → per-pikselowa alfa,
  - `TRANSPARENT` → klik-na-wylot (wejście idzie pod spód),
  - `NOACTIVATE` + `TOOLWINDOW` → okno nie kradnie fokusu, nie ma na pasku zadań/Alt-Tab.
- **DPI awareness:** manifest Per-Monitor v2 (`SetProcessDpiAwarenessContext(
  DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2)`), żeby pokryć piksele fizyczne bez skalowania.
- **Wielomonitorowość:** jedno okno na cały virtual screen zwykle wystarcza; jeśli
  monitory mają nierówne DPI/ułożenie, rozważ jedno okno na monitor (enumeracja przez
  `EnumDisplayMonitors`). MVP: jedno okno na virtual screen.
- Utrzymuj `TOPMOST` — reaguj na `WM_DISPLAYCHANGE` i zmiany rozdzielczości, przebudowując
  rozmiar/łańcuch wymiany.

## Potok renderowania

1. `D3D11CreateDevice` (BGRA support), `IDXGIFactory2`.
2. `DCompositionCreateDevice`, `IDCompositionTarget` dla HWND, `IDCompositionVisual`.
3. Swapchain `CreateSwapChainForComposition` (`DXGI_ALPHA_MODE_PREMULTIPLIED`,
   `DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL`, format `B8G8R8A8_UNORM`).
4. Pętla: pełnoekranowy trójkąt/kwadrat, pixel shader generuje szum z alfą, `Present(1,0)`.
5. Uniformy przez constant buffer: `time`, `amplitude`, `mode`, `resolution`.
6. **Premultiplied alpha** — jeśli alfa jest premultiplikowana, mnóż RGB przez alfę w
   shaderze (kompozycja DComp tego wymaga).

## Projekt pixel shadera (HLSL)

Constant buffer: `float time; float amplitude; int mode; float2 resolution;`

- **Per-pixel, per-frame PRNG** — hash typu PCG z `SV_Position.xy` + ziarna czasu, by szum
  zmieniał się co klatkę (kluczowe wobec √N).
- **Tryby**:
  - `0 WHITE` — biały szum, równomierna alfa = amplitude.
  - `1 HFREQ_HORIZONTAL` — energia w wysokich częstotliwościach **poziomych** (zegar
    pikseli): naprzemienny znak co kolumnę modulowany losowo.
  - `2 TEMPORAL` — dodatkowa losowa modulacja alfy per piksel/klatkę.
- Wyjście z premultiplikacją: `return float4(rgb * a, a);`

Szkic:
```hlsl
cbuffer CB : register(b0) {
    float  uTime;
    float  uAmplitude;
    int    uMode;
    float  _pad;
    float2 uResolution;
    float2 _pad2;
};

uint pcg(uint v){
    uint s = v*747796405u + 2891336453u;
    uint w = ((s >> ((s >> 28u)+4u)) ^ s) * 277803737u;
    return (w >> 22u) ^ w;
}
uint hash3(uint3 v){ return pcg(v.x ^ pcg(v.y ^ pcg(v.z))); }
float rnd(uint3 v){ return (hash3(v) & 0x00FFFFFFu) / float(0x01000000u); }

float4 main(float4 pos : SV_Position) : SV_Target {
    uint2 pix = uint2(pos.xy);
    uint  frame = (uint)(uTime * 240.0);
    float n = rnd(uint3(pix, frame));
    float a = uAmplitude;

    if (uMode == 1) {
        float alt = (pix.x & 1u) == 0u ? 1.0 : -1.0;
        n = 0.5 + 0.5 * alt * (2.0*n - 1.0);
    } else if (uMode == 2) {
        a *= rnd(uint3(pix.x ^ 2654435769u, pix.y, frame ^ 2246822519u));
    }
    float3 rgb = float3(n, n, n);
    return float4(rgb * a, a);   // premultiplied alpha
}
```
Vertex shader: pełnoekranowy trójkąt generowany z `SV_VertexID` (bez bufora wierzchołków),
rysowany `Draw(3,0)`.

## Sterowanie / UX

- **Ikona w zasobniku (tray)** przez `Shell_NotifyIcon`: menu Start/Stop, wybór trybu,
  Wyjście.
- **Globalny skrót** (`RegisterHotKey`, np. Ctrl+Alt+N) — szybkie włącz/wyłącz.
- **Amplituda** — pozycje w menu (np. 10/25/40%) lub małe okno ustawień; zapis do rejestru
  (`HKCU\Software\SoftTempest`) albo pliku w `%APPDATA%`.
- **Autostart** (opcjonalnie) — wpis w `HKCU\...\Run` przełączany z menu.
- Krótki disclaimer o ograniczeniach dostępny z menu (link/okno „O programie").

## Struktura plików (proponowana)

```
/ (repo root)
├── CMakeLists.txt
├── cmake/  (opcjonalne moduły)
├── src/
│   ├── main.cpp            # WinMain, pętla komunikatów, tray, hotkey
│   ├── OverlayWindow.h/.cpp# tworzenie okna warstwowego, style, DPI, virtual screen
│   ├── Renderer.h/.cpp     # D3D11 + DXGI + DirectComposition, pętla klatek, CB
│   ├── NoiseConfig.h       # stan: amplitude, mode, running (współdzielony)
│   ├── Settings.h/.cpp     # zapis/odczyt rejestr lub %APPDATA%
│   └── shaders/
│       ├── noise_vs.hlsl
│       └── noise_ps.hlsl
├── res/
│   ├── app.manifest        # Per-Monitor v2 DPI awareness
│   ├── app.rc              # ikona, wersja
│   └── icon.ico
├── .github/workflows/build.yml
├── README.md               # w tym sekcja ograniczeń z tego dokumentu
└── SYNC.md                 # ten plik
```

## Kompilacja shaderów

Preferuj **kompilację w buildzie** przez `fxc` (jest w Windows SDK) do nagłówków `.h`
z bajtkodem (`/Fh`, `/E main`, `/T ps_5_0` i `/T vs_5_0`), włączanych do binarki — brak
zależności od `d3dcompiler` w runtime. CMake: krok custom command wołający `fxc`.
Alternatywa awaryjna: `D3DCompileFromFile` w runtime (wtedy linkuj `d3dcompiler`).

## Linkowane biblioteki

`d3d11.lib  dxgi.lib  dcomp.lib  user32.lib  gdi32.lib  shell32.lib`
(+ `d3dcompiler.lib` tylko jeśli kompilujesz shadery w runtime).

## GitHub Actions — build .exe

`.github/workflows/build.yml`:

```yaml
name: Build Windows EXE

on:
  push:
    branches: [ main ]
  pull_request:
  workflow_dispatch:

jobs:
  build:
    runs-on: windows-latest
    steps:
      - uses: actions/checkout@v4

      - name: Configure (CMake, MSVC)
        run: cmake -S . -B build -G "Visual Studio 17 2022" -A x64

      - name: Build Release
        run: cmake --build build --config Release

      - name: Upload EXE
        uses: actions/upload-artifact@v4
        with:
          name: soft-tempest-windows
          path: build/Release/*.exe
          if-no-files-found: error
```

Uwagi:
- `windows-latest` ma VS 2022 + Windows SDK (z `fxc`), więc kompilacja HLSL zadziała bez
  dodatkowej instalacji.
- Aplikacja jest GUI (`WinMain`) — w CMake ustaw `WIN32` w `add_executable(... WIN32 ...)`
  albo `set_target_properties(... WIN32_EXECUTABLE TRUE)`.
- Dołącz `res/app.manifest` i `app.rc` do targetu (CMake `target_sources`).
- Podpisywanie kodu opcjonalne (osobny krok z certyfikatem z `secrets`); na start pomiń.

## Szkic CMakeLists.txt (dla Claude Code do rozwinięcia)

```cmake
cmake_minimum_required(VERSION 3.21)
project(SoftTempest LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)

add_executable(SoftTempest WIN32
    src/main.cpp
    src/OverlayWindow.cpp
    src/Renderer.cpp
    src/Settings.cpp
    res/app.rc
)
# TODO: custom command fxc dla src/shaders/*.hlsl -> nagłówki w ${CMAKE_BINARY_DIR}/shaders
target_include_directories(SoftTempest PRIVATE ${CMAKE_BINARY_DIR}/shaders)
target_link_libraries(SoftTempest PRIVATE d3d11 dxgi dcomp user32 gdi32 shell32)
```

## Kolejność pracy dla Claude Code

1. Szkielet CMake + pusty `WinMain` z oknem `WS_POPUP`, tak by `build.yml` produkował `.exe`.
2. Okno warstwowe: style rozszerzone, virtual screen, DPI Per-Monitor v2, klik-na-wylot.
3. Renderer: D3D11 + DirectComposition + swapchain for composition, czyszczenie na
   przezroczysto, `Present`.
4. Pixel shader: biały szum per-frame z premultiplied alpha; podłącz constant buffer.
5. Tryby (HF poziomy, temporal) + amplituda; sterowanie z menu tray + hotkey.
6. Ustawienia trwałe (rejestr/%APPDATA%), opcjonalny autostart.
7. README z sekcją ograniczeń z tego dokumentu.
8. Utrzymuj zielony build w Actions na każdym etapie.

## Definicja „gotowe" (MVP)

- `.exe` buduje się w GitHub Actions i jest do pobrania jako artefakt.
- Po uruchomieniu pojawia się klik-na-wylot szum na wszystkich monitorach (poza Secure
  Desktop / exclusive fullscreen / treścią chronioną), wyłączany z tray/hotkey.
- Szum zmienia się co klatkę; amplituda i tryby działają.
- README zawiera uczciwy opis ograniczeń (Secure Desktop, DRM/OPM, exclusive fullscreen,
  √N, konieczność pomiaru, status „uzupełnienie ekranowania, nie zamiennik").
