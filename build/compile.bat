@echo off
set PATH=%PATH%;C:\Program Files\LLVM\bin

echo Compiling xapay_hock.c to WebAssembly...

clang ^
--target=wasm32 ^
--no-standard-libraries ^
-Wl,--export-all ^
-Wl,--no-entry ^
-o xapay_hock.wasm ^
../src/c/xapay_hock.c

if %ERRORLEVEL% EQU 0 (
    echo Compilation successful!
    echo Output: xapay_hock.wasm
) else (
    echo Compilation failed!
    exit /b %ERRORLEVEL%
) 