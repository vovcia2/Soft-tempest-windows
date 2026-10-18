# Soft TEMPEST overlay dla Windows

Pełnoekranowa, klikalna-na-wylot warstwa szumu pikseli, losowana od nowa **w każdej
klatce** i składana przez kompozytor systemowy (DWM) nad całą treścią ekranu. Celem jest
podniesienie poziomu szumu w emisji elektromagnetycznej toru wyświetlania (soft TEMPEST
wg Kuhn & Anderson, *Soft Tempest: Hidden Data Transmission Using Electromagnetic
Emanations*, 1998).

To narzędzie **ochronne** dla własnego urządzenia: niczego nie przechwytuje, nie nadaje
i nie atakuje. Specyfikacja, na podstawie której powstał projekt, jest w [`SYNC.md`](SYNC.md).

> **English summary:** a Windows tray app that draws a click-through, always-on-top,
> per-frame-randomised noise overlay over all monitors (Direct3D 11 + DirectComposition)
> to raise the noise floor of the display's electromagnetic emanations. Prototype; read
> the limitations below before relying on it.

## Model zagrożenia i świadome ograniczenia — przeczytaj przed użyciem

1. **Bezpieczny pulpit (Secure Desktop).** Monity UAC, ekran Ctrl+Alt+Del, logowanie
   i Windows Hello działają na osobnym pulpicie, do którego zwykła aplikacja nie ma
   dostępu. Overlay tam się **nie pojawi** i nie próbuje tego obchodzić.
2. **Treść chroniona (DRM/OPM).** Sprzętowo chronione wideo (PlayReady, chroniona
   ścieżka wyjścia) może iść torem, którego overlay nie pokryje.
3. **Ekskluzywny fullscreen.** Gry i aplikacje w prawdziwym exclusive fullscreen (flip
   model z pominięciem kompozycji) omijają DWM, więc overlay nie jest nad nimi widoczny.
   Tryb borderless-windowed działa normalnie.
4. **Uśrednianie N ramek.** Statyczny szum spada jak √N po uśrednieniu N klatek przez
   napastnika. Dlatego szum jest losowany **niezależnie w każdej klatce** (nowe ziarno
   PRNG podawane do pixel shadera co klatkę). Naiwny statyczny overlay dawałby tylko
   złudzenie ochrony.
5. **Bez pomiaru nie ma dowodu skuteczności.** Nie wykonano pomiarów emisji. To
   prototyp i **uzupełnienie** ekranowania sprzętowego, **nie zamiennik**. Jeśli
   potrzebujesz realnej ochrony TEMPEST, potrzebujesz sprzętu i pomiarów, nie tylko
   tej aplikacji.
6. **Bateria / GPU.** Ciągłe pełnoekranowe renderowanie obciąża GPU i na laptopie skraca
   czas pracy. W menu jest ograniczenie częstotliwości klatek (co 2. / co 4. vblank).

## Jak to działa

- Jedno okno `WS_POPUP` o rozmiarze całego *virtual screen* (wszystkie monitory),
  ze stylami `WS_EX_NOREDIRECTIONBITMAP | WS_EX_LAYERED | WS_EX_TRANSPARENT |
  WS_EX_TOPMOST | WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW`:
  - `NOREDIRECTIONBITMAP` — okno nie ma własnej powierzchni GDI; jedyną treścią jest
    drzewo DirectComposition z per-pikselową alfą (to szybka ścieżka, bez
    `UpdateLayeredWindow`),
  - `LAYERED + TRANSPARENT` — hit-testing pomija okno, więc mysz i klawiatura trafiają
    do tego, co jest pod spodem,
  - `NOACTIVATE + TOOLWINDOW` — okno nie kradnie fokusu i nie ma go na pasku zadań ani
    w Alt-Tab.
- Renderowanie: Direct3D 11 (BGRA) → swapchain `CreateSwapChainForComposition`
  (`B8G8R8A8_UNORM`, `FLIP_SEQUENTIAL`, `DXGI_ALPHA_MODE_PREMULTIPLIED`) →
  `IDCompositionVisual` → `IDCompositionTarget` okna. Co klatkę rysowany jest jeden
  pełnoekranowy trójkąt z pixel shaderem szumu i `Present(syncInterval, 0)`.
- Pixel shader (`src/shaders/noise_ps.hlsl`): hash PCG z `SV_Position.xy` i
  per-klatkowego ziarna; wyjście z premultiplikowaną alfą.
- DPI: manifest Per-Monitor v2 (+ `SetProcessDpiAwarenessContext`), więc okno pokrywa
  piksele fizyczne bez skalowania. Zmiany rozdzielczości/układu monitorów
  (`WM_DISPLAYCHANGE`, `WM_DPICHANGED`) przebudowują rozmiar okna i swapchaina;
  `TOPMOST` jest okresowo ponawiany.

### Tryby szumu

| Tryb | Opis |
|------|------|
| `0` White | niezależny biały szum per piksel, jednolita alfa = amplituda |
| `1` HF horizontal | energia w wysokich częstotliwościach **poziomych** (zegar pikseli): pary sąsiednich kolumn dzielą losową wartość ze znakiem przeciwnym, więc sygnał zmienia się co kolumnę |
| `2` Temporal | biały szum + dodatkowa losowa modulacja alfy per piksel i klatkę |

## Sterowanie

- **Ikona w zasobniku:** prawy przycisk → menu: Start/Stop, Mode, Amplitude
  (5/10/25/40/60 %), Frame rate (co 1/2/4 vblank), Start with Windows, About /
  limitations, Exit. Dwuklik / Enter na ikonie przełącza overlay.
- **Globalny skrót:** `Ctrl+Alt+N` — włącz/wyłącz.
- **Ustawienia** są zapisywane w rejestrze `HKCU\Software\SoftTempest`
  (`AmplitudePercent`, `Mode`, `SyncInterval`, `Enabled`).
- **Autostart** to wpis `SoftTempest` w `HKCU\Software\Microsoft\Windows\CurrentVersion\Run`.
- Druga instancja aplikacji kończy się komunikatem (pojedyncza instancja).

## Budowanie

### Visual Studio + CMake (zalecane)

Generator Visual Studio (2022 lub nowszy):

```powershell
cmake -S . -B build -A x64
cmake --build build --config Release
# wynik: build\Release\SoftTempest.exe
```

Albo Ninja z poziomu „Developer PowerShell / Command Prompt for VS” (tak buduje CI,
bo nie wymaga znajomości wersji Visual Studio):

```powershell
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
# wynik: build\SoftTempest.exe
```

Shadery HLSL są kompilowane w buildzie przez `fxc` z Windows SDK do nagłówków z bajtkodem
(`vs_5_0` / `ps_5_0`), więc binarka nie zależy od `d3dcompiler` w runtime. Jeśli `fxc`
nie zostanie znalezione (albo podasz `-DSOFTTEMPEST_RUNTIME_SHADERS=ON`), źródła HLSL
są osadzane w binarce i kompilowane przy starcie przez `D3DCompile`
(`d3dcompiler_47.dll` jest częścią Windows).

### MinGW-w64 (cross-compile z Linuksa)

```bash
cmake -S . -B build-mingw -DCMAKE_TOOLCHAIN_FILE=cmake/mingw-w64-x86_64.cmake -DCMAKE_BUILD_TYPE=Release
cmake --build build-mingw
# wynik: build-mingw/SoftTempest.exe (statycznie zlinkowany, shadery w runtime)
```

### GitHub Actions

`.github/workflows/build.yml` buduje wersję MSVC na `windows-latest` i publikuje
artefakt `soft-tempest-windows` (`SoftTempest.exe`). Job znajduje zainstalowane Visual
Studio przez `vswhere`, ładuje jego środowisko (`vcvars64.bat`) i buduje Ninją, więc nie
zależy od konkretnej wersji VS na runnerze. Dodatkowo buduje wariant MinGW na Ubuntu jako
kontrolę przenośności źródeł.

## Wymagania

- Windows 10 / 11 (DirectComposition, `WS_EX_NOREDIRECTIONBITMAP`; działa też na 8.1).
- GPU z Direct3D 11 (feature level 11.0). Przy braku sprzętowego urządzenia używany jest
  WARP (renderowanie programowe, znacznie wolniejsze).

## Struktura repozytorium

```
CMakeLists.txt                 # CMake (MSVC + MinGW), krok fxc, fallback runtime
cmake/mingw-w64-x86_64.cmake   # toolchain do cross-kompilacji
cmake/noise_sources.h.in       # szablon osadzania HLSL dla trybu runtime
src/main.cpp                   # WinMain, tray, hotkey, pętla klatek
src/OverlayWindow.h/.cpp       # okno overlay: style, virtual screen, DPI, topmost
src/Renderer.h/.cpp            # D3D11 + DXGI + DirectComposition, constant buffer
src/NoiseConfig.h              # stan: amplitude, mode, syncInterval, running
src/Settings.h/.cpp            # rejestr HKCU + autostart
src/shaders/noise_vs.hlsl      # pełnoekranowy trójkąt z SV_VertexID
src/shaders/noise_ps.hlsl      # szum per piksel / per klatka, premultiplied alpha
res/app.manifest               # Per-Monitor v2 DPI
res/app.rc, res/icon.ico       # ikona, wersja
.github/workflows/build.yml    # CI: artefakt .exe
SYNC.md                        # specyfikacja projektu
```
