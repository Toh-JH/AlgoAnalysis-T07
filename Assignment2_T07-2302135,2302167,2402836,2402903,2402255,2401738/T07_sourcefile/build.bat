@echo off
REM ============================================================================
REM  CSD3130 / CSD3131 Assignment 2 - Seam Carving - build script (Windows / MSVC)
REM
REM    build.bat          compile the CLI, GUI, tests and benchmark into bin\
REM    build.bat test     compile, then run the correctness suite
REM                       (transcript written to results\test_output.txt)
REM    build.bat demo     compile, then generate every report figure into results\
REM    build.bat bench    compile, then run the full benchmark (CSV into results\benchmark)
REM
REM  If this script cannot find Visual Studio, open "Developer Command Prompt
REM  for VS 2022" and run it from there.  On Linux / macOS use CMake:
REM    cmake -S . -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build
REM ============================================================================
setlocal
cd /d "%~dp0"

set "VCVARS=C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"
if not exist "%VCVARS%" set "VCVARS=C:\Program Files\Microsoft Visual Studio\2022\Professional\VC\Auxiliary\Build\vcvars64.bat"
if not exist "%VCVARS%" set "VCVARS=C:\Program Files\Microsoft Visual Studio\2022\Enterprise\VC\Auxiliary\Build\vcvars64.bat"

where cl >nul 2>&1
if errorlevel 1 (
    if not exist "%VCVARS%" (
        echo [ERROR] Visual Studio not found and 'cl' is not on PATH.
        echo         Open "Developer Command Prompt for VS 2022" and re-run this script.
        exit /b 1
    )
    call "%VCVARS%" >nul
)

if not exist bin  mkdir bin
if not exist obj  mkdir obj
if not exist results mkdir results

set "FLAGS=/nologo /std:c++17 /EHsc /O2 /W4 /permissive- /utf-8 /openmp /MP"
set "CORE=src\image.cpp src\energy.cpp src\convolution.cpp src\seam.cpp src\seam_dp.cpp src\seam_greedy.cpp src\seam_graphcut.cpp src\carver.cpp src\visualize.cpp"

echo.
echo === Building core library ===
cl %FLAGS% /c %CORE% /Fo:obj\
if errorlevel 1 exit /b 1
lib /nologo obj\image.obj obj\energy.obj obj\convolution.obj obj\seam.obj obj\seam_dp.obj obj\seam_greedy.obj obj\seam_graphcut.obj obj\carver.obj obj\visualize.obj /OUT:obj\seamcore.lib
if errorlevel 1 exit /b 1

echo.
echo === Building tests, CLI, benchmark, GUI ===
cl %FLAGS% tests\tests.cpp        obj\seamcore.lib /Fo:obj\ /Fe:bin\seamcarve_tests.exe
if errorlevel 1 exit /b 1
cl %FLAGS% apps\seamcarve_cli.cpp obj\seamcore.lib /Fo:obj\ /Fe:bin\seamcarve.exe
if errorlevel 1 exit /b 1
cl %FLAGS% apps\benchmark.cpp     obj\seamcore.lib /Fo:obj\ /Fe:bin\seamcarve_bench.exe
if errorlevel 1 exit /b 1
cl %FLAGS% apps\seamcarve_gui.cpp obj\seamcore.lib user32.lib gdi32.lib comdlg32.lib comctl32.lib /Fo:obj\ /Fe:bin\seamcarve_gui.exe /link /SUBSYSTEM:WINDOWS
if errorlevel 1 exit /b 1

echo.
echo Build complete:
echo    bin\seamcarve_gui.exe                          interactive app (bonus)
echo    bin\seamcarve.exe in.png out.png --width 400   command line
echo    bin\seamcarve_tests.exe                        correctness suite
echo    bin\seamcarve_bench.exe                        timing / quality benchmark

if /i "%~1"=="test" (
    bin\seamcarve_tests.exe > results\test_output.txt
    type results\test_output.txt | findstr /C:"SUMMARY" /C:"passed" /C:"[FAIL]"
)
if /i "%~1"=="demo"  bin\seamcarve.exe --demo results
if /i "%~1"=="bench" bin\seamcarve_bench.exe results\benchmark
