if exist F:\. (
	format F: /FS:FAT32 /Q /X
	copy /y Build\FirstVolume\AOSLDR.BIN F:\
	xcopy "Build\FirstVolume\DRIVERS" "F:\DRIVERS\" /E /I /H /Y
) else (
	echo F: not found
)
pause