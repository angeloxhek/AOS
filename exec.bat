@echo off
title AOS Executor
color 0a

"C:\Program Files\qemu\qemu-system-x86_64.exe" -gdb tcp:127.0.0.1:1234 -hda disk.vhd -d guest_errors,unimp,int,cpu_reset -D qemu.log -no-reboot -no-shutdown
pause