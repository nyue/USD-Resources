@echo off
if "%~1"=="" (
    echo Usage: %~nx0 ^<SKROTVIKTOR_DIR^> 1>&2
    exit /b 1
)
set SKROTVIKTOR_DIR=%~1
set PXR_PLUGINPATH_NAME=%SKROTVIKTOR_DIR%\lib\usd\hairProcHoudini\resources
set PATH=%SKROTVIKTOR_DIR%\lib;%PATH%
set PYTHONPATH=%SKROTVIKTOR_DIR%\lib\python
hython hairProc\testenv\genHairProc.py
