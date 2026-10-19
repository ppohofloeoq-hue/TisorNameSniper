# Tisor Name Sniper

High-performance Discord username sniper — C++20 · MSVC · FTXUI · cpr

---

## Prerequisites

- **Windows 10/11 64-bit**
- [Visual Studio 2022](https://visualstudio.microsoft.com/) — install with **"Desktop development with C++"** workload
  - Required components: MSVC v143 C++ x64 build tools, Windows 10/11 SDK
- [CMake 3.20+](https://cmake.org/download/) — check "Add CMake to PATH" during install
- [Git for Windows](https://git-scm.com/download/win)

---

## Build

Open **"Developer PowerShell for VS 2022"** (search Start menu):

```powershell
git clone https://github.com/you/TisorNameSniper.git
cd TisorNameSniper

# Configure — downloads FTXUI, cpr, nlohmann/json automatically
cmake -B build -G "Visual Studio 17 2022" -A x64

# Build Release binary (~2-3 min first time, deps compile)
cmake --build build --config Release
```

Output: `build\Release\TisorNameSniper.exe`

---

## Input Files

Place these in the **same directory as the `.exe`** (`build\Release\`):

### `tokens.txt` — raw tokens, one per line
