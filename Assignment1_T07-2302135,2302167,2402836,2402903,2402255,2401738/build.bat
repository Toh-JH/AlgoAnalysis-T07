@echo off
REM ============================================================================
REM  CSD3130 / CSD3131 Assignment Part 1 - build script (Windows / MSVC)
REM
REM    build.bat          compile all three programs
REM    build.bat test     compile, then run every program's full demo + test
REM                       suite and refresh QX\test_output.txt
REM
REM  If this script cannot find Visual Studio, open "Developer Command Prompt
REM  for VS 2022" and run the three cl commands shown below by hand.
REM  On Linux / macOS use g++ instead:
REM    g++ -std=c++17 -O2 -Wall -Wextra -o q1 Q1/hawker_matching.cpp
REM    g++ -std=c++17 -O2 -Wall -Wextra -o q2 Q2/growth_and_recurrence.cpp
REM    g++ -std=c++17 -O2 -Wall -Wextra -o q3 Q3/sorting_analysis.cpp
REM ============================================================================
setlocal
cd /d "%~dp0"

set "VCVARS=C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"
if not exist "%VCVARS%" set "VCVARS=C:\Program Files\Microsoft Visual Studio\2022\Professional\VC\Auxiliary\Build\vcvars64.bat"
if not exist "%VCVARS%" set "VCVARS=C:\Program Files\Microsoft Visual Studio\2022\Enterprise\VC\Auxiliary\Build\vcvars64.bat"
if not exist "%VCVARS%" set "VCVARS=C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars32.bat"

where cl >nul 2>&1
if errorlevel 1 (
    if not exist "%VCVARS%" (
        echo [ERROR] Visual Studio not found and 'cl' is not on PATH.
        echo         Open "Developer Command Prompt for VS 2022" and re-run this script.
        exit /b 1
    )
    call "%VCVARS%" >nul
)

echo.
echo === Building Q1 : NEA hawker stall stable matching ===
cl /nologo /std:c++17 /EHsc /O2 /W4 Q1\hawker_matching.cpp /Fo:Q1\ /Fe:Q1\hawker_matching.exe
if errorlevel 1 exit /b 1

echo.
echo === Building Q2 : orders of growth and T(n)=3T(n/3)+lg n ===
cl /nologo /std:c++17 /EHsc /O2 /W4 Q2\growth_and_recurrence.cpp /Fo:Q2\ /Fe:Q2\growth_and_recurrence.exe
if errorlevel 1 exit /b 1

echo.
echo === Building Q3 : sorting stability and the n lg n lower bound ===
cl /nologo /std:c++17 /EHsc /O2 /W4 Q3\sorting_analysis.cpp /Fo:Q3\ /Fe:Q3\sorting_analysis.exe
if errorlevel 1 exit /b 1

echo.
echo Build complete. Run any of:
echo    Q1\hawker_matching.exe
echo    Q2\growth_and_recurrence.exe
echo    Q3\sorting_analysis.exe
echo Add --all to run every demo and test non-interactively.

if /i not "%~1"=="test" goto :eof

echo.
echo === Running all demos and test suites ===
Q1\hawker_matching.exe        --all > Q1\test_output.txt
if errorlevel 1 echo [WARNING] Q1 reported a failing test - see Q1\test_output.txt
Q2\growth_and_recurrence.exe  --all > Q2\test_output.txt
if errorlevel 1 echo [WARNING] Q2 reported a failing test - see Q2\test_output.txt
Q3\sorting_analysis.exe       --all > Q3\test_output.txt
if errorlevel 1 echo [WARNING] Q3 reported a failing test - see Q3\test_output.txt
echo Transcripts written to Q1\test_output.txt, Q2\test_output.txt, Q3\test_output.txt
