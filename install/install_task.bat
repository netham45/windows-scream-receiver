@echo off

:: Check for admin privileges
net session >nul 2>&1
if %errorLevel% == 0 (
    goto :admin
) else (
    echo Requesting administrative privileges...
    goto :elevate
)

:elevate
:: Self-elevate the script if not already running as admin
powershell -Command "Start-Process cmd -Verb RunAs -ArgumentList '/c %~dpnx0 %*'"
exit /b

:admin
echo Running with administrative privileges.

cd %~dp0

mkdir "%LOCALAPPDATA%\ScreamReceiver" > NUL
copy ScreamReceiver.exe "%LOCALAPPDATA%\ScreamReceiver\ScreamReceiver.exe" > NUL

:: Get user input for IP, port, and multicast option
set /p TARGET_PORT=Enter the receiver port number (default: 4010): 
if not defined TARGET_PORT set TARGET_PORT=4010

:: Prepare the command with parameters
set COMMAND=%%LOCALAPPDATA%%\ScreamReceiver\ScreamReceiver.exe %TARGET_PORT%

:: Add multicast option if selected
if /i "%USE_MULTICAST%"=="y" set COMMAND=%COMMAND% -m
:: Create the scheduled task with parameters
schtasks /create /tn "RunScreamReceiver" /tr "%COMMAND%" /sc onlogon /rl highest /f

if %errorlevel% equ 0 (
    echo Scheduled task created successfully.
    schtasks /run /tn "RunScreamReceiver"
    echo Scheduled task created successfully with the following parameters:
    echo Port: %TARGET_PORT%
    if %errorlevel% equ 0 (    

        echo Scheduled task created successfully.
        echo Scheduled task started successfully.
    ) else (
        echo Failed to start the scheduled task.
    )
) else (
    echo Failed to create the scheduled task.
)

pause
