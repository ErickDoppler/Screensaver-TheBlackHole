@echo off
rem One-shot Release build without installing. Same as
rem   2-build-and-install-windows.cmd noinstall
call "%~dp0..\2-build-and-install-windows.cmd" noinstall %*
