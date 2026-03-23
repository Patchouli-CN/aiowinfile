@echo off
chcp 65001 > nul
setlocal enabledelayedexpansion

echo ========================================
echo   ayafileio 优化空间打包脚本
echo ========================================
echo.

:: 设置临时目录（可以使用其他盘符节省C盘空间）
set TEMP_DIR=D:\temp\cibw_build
if not exist %TEMP_DIR% mkdir %TEMP_DIR%
set TEMP=%TEMP_DIR%
set TMP=%TEMP_DIR%

:: 设置cibuildwheel缓存目录
set CIBW_CACHE_DIR=%TEMP_DIR%\cibw_cache

echo 临时目录: %TEMP_DIR%
echo.

:: 检查磁盘空间
echo [1/5] 检查磁盘空间...
for /f "tokens=3" %%a in ('dir %TEMP_DIR% 2^>nul ^| find "可用字节"') do (
    set /a FREE_SPACE=%%a / 1048576
    echo 可用空间: !FREE_SPACE! MB
    if !FREE_SPACE! lss 2048 (
        echo ⚠️ 警告: 可用空间小于2GB，可能不够用
    )
)
echo.

:: 安装cibuildwheel
echo [2/5] 安装 cibuildwheel...
pip install --cache-dir %TEMP_DIR%\pip_cache cibuildwheel
echo.

:: 清理旧文件
echo [3/5] 清理旧文件...
if exist build rmdir /s /q build
if exist dist rmdir /s /q dist
if exist ayafileio.egg-info rmdir /s /q ayafileio.egg-info
echo ✅ 清理完成
echo.

:: 设置环境变量优化构建
echo [4/5] 配置构建环境...
set CIBW_BUILD=cp310-* cp311-* cp312-* cp313-* cp314-*
set CIBW_SKIP=*-win32 *-musllinux* pp*
set CIBW_ARCHS=auto64
set CIBW_ENVIRONMENT=CMAKE_BUILD_PARALLEL_LEVEL=2
set CIBW_BUILD_VERBOSITY=1
set CIBW_BEFORE_BUILD=echo "Building for Python"
set CIBW_AFTER_BUILD=rmdir /s /q build 2>nul

:: 每个版本构建后立即清理
set CIBW_BEFORE_ALL=echo "Starting build"
set CIBW_AFTER_ALL=echo "All builds completed"

echo 开始构建...
echo.

:: 执行构建
cibuildwheel --platform windows --output-dir dist
if errorlevel 1 (
    echo.
    echo ❌ 构建失败！
    pause
    exit /b 1
)

echo.
echo [5/5] 清理临时文件...
:: 清理构建缓存（可选）
set /p CLEAN_CACHE="是否清理构建缓存？(y/n): "
if /i "!CLEAN_CACHE!"=="y" (
    echo 清理缓存...
    rmdir /s /q %TEMP_DIR% 2>nul
    echo ✅ 缓存已清理
) else (
    echo 保留缓存用于下次构建
)

echo.
echo ========================================
echo   📦 打包完成！
echo ========================================
echo.
echo 生成的 wheel 文件:
if exist dist (
    dir dist\*.whl /b
    echo.
    echo 总文件大小:
    set TOTAL_SIZE=0
    for %%f in (dist\*.whl) do (
        set /a TOTAL_SIZE+=%%~zf
    )
    set /a TOTAL_SIZE_MB=!TOTAL_SIZE! / 1048576
    echo !TOTAL_SIZE_MB! MB
) else (
    echo ❌ 未生成任何文件
)
echo.
pause