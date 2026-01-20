@echo off
chcp 65001 >nul
setlocal

REM AM32 Firmware Build Script
REM Usage: build.bat [target] [clean]
REM Examples: 
REM   build.bat ZTW_A_HV_F421        - Build target
REM   build.bat ZTW_A_HV_F421 clean  - Clean and rebuild
REM   build.bat clean                - Clean only
REM   build.bat targets              - List all targets

set MAKE_PATH=%~dp0tools\windows\make\bin
set MAKE=%MAKE_PATH%\make.exe
set PATH=%MAKE_PATH%;%PATH%

if "%1"=="" (
    echo Usage: build.bat [target] [clean]
    echo.
    echo Examples:
    echo   build.bat ZTW_A_HV_F421        - Build target
    echo   build.bat ZTW_A_HV_F421 clean  - Clean and rebuild
    echo   build.bat clean                - Clean only
    echo   build.bat targets              - List all targets
    echo.
    goto :eof
)

if "%1"=="clean" (
    echo Cleaning build directory...
    "%MAKE%" clean
    goto :eof
)

if "%1"=="targets" (
    echo Listing all available targets...
    "%MAKE%" targets
    goto :eof
)

if "%2"=="clean" (
    echo Clean and rebuild %1 ...
    "%MAKE%" clean
)

echo Building %1 ...
"%MAKE%" %1

if %ERRORLEVEL%==0 (
    echo.
    echo ========================================
    echo Build successful! Output files:
    dir /b obj\*%1*.bin obj\*%1*.hex 2>nul
    echo ========================================
) else (
    echo.
    echo Build failed, error code: %ERRORLEVEL%
)

endlocal
