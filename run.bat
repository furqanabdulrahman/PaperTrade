@echo off
REM PaperTrade launcher — double-click this file to start the app.
REM It runs from the project folder so .env (your Finnhub key) is found.
cd /d "%~dp0"
build\bin\papertrade.exe
