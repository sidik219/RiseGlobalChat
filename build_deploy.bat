@echo off
setlocal

set "SCRIPT_DIR=%~dp0"
set "PROJECT_DIR=%SCRIPT_DIR%"
set "DLL_PATH=%PROJECT_DIR%build\Release\RiseGlobalChat.dll"
set "PLUGINS_DIR=D:\SteamLibrary\steamapps\common\MonsterHunterRise\reframework\plugins"
set "VCVARS=C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvarsall.bat"
set "VCPKG_ROOT=D:\CODING\Game\Modding\MHRise\Thridparty\vcpkg"
set "EXIT_CODE=0"

echo.
echo ============================================
echo  RGC Build and Deploy (IXWebSocket)
echo ============================================
echo.
echo [INFO] Project dir : %PROJECT_DIR%
echo [INFO] DLL target  : %DLL_PATH%
echo [INFO] Plugins dir : %PLUGINS_DIR%
echo [INFO] vcvarsall   : %VCVARS%
echo [INFO] vcpkg root  : %VCPKG_ROOT%
echo.

if exist "%VCVARS%" goto have_vcvars
echo [ERROR] vcvarsall.bat tidak ditemukan di:
echo     %VCVARS%
set "EXIT_CODE=1"
goto end

:have_vcvars
if exist "%PLUGINS_DIR%" goto have_plugins
echo [ERROR] Folder plugins game tidak ditemukan:
echo     %PLUGINS_DIR%
set "EXIT_CODE=1"
goto end

:have_plugins
if exist "%VCPKG_ROOT%\scripts\buildsystems\vcpkg.cmake" goto have_vcpkg
echo [ERROR] vcpkg tidak ditemukan di:
echo     %VCPKG_ROOT%
echo     Install dulu:
echo       git clone https://github.com/microsoft/vcpkg.git C:\vcpkg
echo       C:\vcpkg\bootstrap-vcpkg.bat
echo       C:\vcpkg\vcpkg.exe install ixwebsocket:x64-windows-static
echo       C:\vcpkg\vcpkg.exe install openssl:x64-windows-static
set "EXIT_CODE=1"
goto end

:have_vcpkg
echo [1/4] Initializing Visual Studio x64 environment...
call "%VCVARS%" x64
if errorlevel 1 goto fail_vcvars
echo     OK
goto step2

:fail_vcvars
echo [ERROR] vcvarsall.bat gagal
set "EXIT_CODE=1"
goto end

:step2
echo.
echo [2/4] Configuring CMake with vcpkg toolchain...
cd /d "%PROJECT_DIR%"
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 ^
  -DCMAKE_TOOLCHAIN_FILE="%VCPKG_ROOT%\scripts\buildsystems\vcpkg.cmake" ^
  -DVCPKG_TARGET_TRIPLET=x64-windows-static ^
  -DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded
if errorlevel 1 goto fail_cmake

echo.
echo [3/4] Building Release DLL...
cmake --build build --config Release
if errorlevel 1 goto fail_build

echo.
echo [4/4] Deploying to game plugins folder...
if exist "%DLL_PATH%" goto have_dll
echo [ERROR] DLL tidak ditemukan setelah build:
echo     %DLL_PATH%
set "EXIT_CODE=4"
goto end

:have_dll
copy /Y "%DLL_PATH%" "%PLUGINS_DIR%\"
if errorlevel 1 goto fail_copy

echo.
echo [SUCCESS] Build + deploy selesai.
echo           DLL: %PLUGINS_DIR%\RiseGlobalChat.dll
goto end

:fail_cmake
echo [ERROR] CMake configure gagal
set "EXIT_CODE=2"
goto end

:fail_build
echo [ERROR] Build gagal
set "EXIT_CODE=3"
goto end

:fail_copy
echo [ERROR] Copy gagal. Tutup game jika sedang berjalan.
set "EXIT_CODE=5"
goto end

:end
echo.
echo ============================================
echo  Selesai. Exit code: %EXIT_CODE%
echo ============================================
echo.
pause
endlocal
exit /b %EXIT_CODE%