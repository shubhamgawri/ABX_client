@echo off
echo ABX Client Builder for Windows

REM Check if Visual Studio is installed (via vswhere)
where /q vswhere
if %ERRORLEVEL% NEQ 0 (
    echo Visual Studio detection tool not found. Assuming Visual Studio is not available.
    goto :USE_MINGW
)

set VS_PATH=
for /f "usebackq tokens=*" %%i in (`vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do (
  set VS_PATH=%%i
)

if not "%VS_PATH%"=="" (
    echo Found Visual Studio at: %VS_PATH%
    goto :USE_MSVC
) else (
    echo Visual Studio with C++ tools not found.
    goto :USE_MINGW
)

:USE_MSVC
echo Building with Visual Studio...
echo Checking if developer command prompt is available...

set VCVARS_PATH="%VS_PATH%\VC\Auxiliary\Build\vcvars64.bat"
if exist %VCVARS_PATH% (
    echo Using Visual Studio developer environment.
    call %VCVARS_PATH%
    
    echo Downloading JSON library...
    if not exist json.hpp (
        powershell -Command "Invoke-WebRequest -Uri https://raw.githubusercontent.com/nlohmann/json/develop/single_include/nlohmann/json.hpp -OutFile json.hpp"
        if %ERRORLEVEL% NEQ 0 (
            echo Failed to download JSON library.
            exit /b 1
        )
    )
    
    echo Compiling with MSVC...
    cl /EHsc /std:c++17 /W4 abx_client.cpp /link ws2_32.lib /out:abx_client.exe
    if %ERRORLEVEL% NEQ 0 (
        echo Compilation failed.
        exit /b 1
    )
    goto :END
)

:USE_MINGW
echo Checking for MinGW g++...
where /q g++
if %ERRORLEVEL% NEQ 0 (
    echo Neither Visual Studio nor MinGW g++ found.
    echo Please install one of them and try again.
    echo You can download MinGW from https://www.mingw-w64.org/ or https://www.msys2.org/
    exit /b 1
)

echo Building with MinGW g++...
echo Downloading JSON library...
if not exist json.hpp (
    powershell -Command "Invoke-WebRequest -Uri https://raw.githubusercontent.com/nlohmann/json/develop/single_include/nlohmann/json.hpp -OutFile json.hpp"
    if %ERRORLEVEL% NEQ 0 (
        echo Failed to download JSON library.
        exit /b 1
    )
)

echo Compiling with g++...
g++ -std=c++17 -Wall -Wextra -O2 abx_client.cpp -o abx_client.exe -lws2_32
if %ERRORLEVEL% NEQ 0 (
    echo Compilation failed.
    exit /b 1
)

:END
echo Build completed successfully!
echo You can now run abx_client.exe to connect to the ABX server.