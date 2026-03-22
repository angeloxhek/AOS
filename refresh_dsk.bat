if exist F:\. (
	copy /y Build\FirstVolume\AOSLDR.BIN F:\
	xcopy "Build\FirstVolume\DRIVERS" "F:\DRIVERS\" /E /I /H /Y
	copy /y Build\FirstVolume\tree.elf F:\
) else (
	echo F: not found
)
pause