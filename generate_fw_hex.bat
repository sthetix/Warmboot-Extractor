@echo off
setlocal enabledelayedexpansion

:menu
cls
echo ============================================
echo   Firmware Version to Hex Converter
echo   For Warmboot Extractor Database
echo ============================================
echo.

:input
echo Enter firmware version (format: MAJOR.MINOR.PATCH)
echo Example: 21.0.0 or 22.0.0
echo.
set /p firmware="Firmware version: "

:: Parse the firmware version
for /f "tokens=1,2,3 delims=." %%a in ("%firmware%") do (
    set major=%%a
    set minor=%%b
    set patch=%%c
)

:: Validate input
if not defined major goto invalid
if not defined minor set minor=0
if not defined patch set patch=0

:: Convert to decimal (remove leading zeros)
set /a major_dec=%major%
set /a minor_dec=%minor%
set /a patch_dec=%patch%

:: Check valid ranges
if %major_dec% GTR 255 goto invalid
if %minor_dec% GTR 15 goto invalid
if %patch_dec% GTR 15 goto invalid

:: Calculate hex value: (major << 8) | (minor << 4) | patch
set /a result=%major_dec% * 256 + %minor_dec% * 16 + %patch_dec%

:: Convert to hex
set "hex="
set "digits=0123456789ABCDEF"
set /a temp=%result%

:hexloop
set /a "digit=temp %% 16"
set /a "temp=temp / 16"
for /f "tokens=* delims=" %%d in ("!digits:~%digit%,1!") do set "hex=%%d!hex!"
if %temp% gtr 0 goto hexloop

:: Pad with zeros to 4 digits
:pad
if not "!hex:~3,1!"=="" goto padded
set "hex=0!hex!"
goto pad

:padded
echo.
echo ============================================
echo   RESULT
echo ============================================
echo Firmware: %major%.%minor%.%patch%
echo Hex Code: 0x%hex%
echo.
echo Add this to wb_db.txt:
echo 0x%hex%=FUSE_COUNT
echo.
echo (Replace FUSE_COUNT with the actual fuse count)
echo ============================================
echo.

:prompt
set /p another="Generate another? (y/n): "
if /i "%another%"=="y" goto menu
if /i "%another%"=="yes" goto menu
goto end

:invalid
echo.
echo [ERROR] Invalid firmware version format!
echo Please use format: MAJOR.MINOR.PATCH
echo Example: 21.0.0
echo.
echo Valid ranges:
echo - Major: 0-255
echo - Minor: 0-15
echo - Patch: 0-15
echo.
pause
goto menu

:end
echo.
echo Goodbye!
timeout /t 2 >nul
exit /b
