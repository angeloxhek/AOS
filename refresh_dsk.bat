if exist F:\. (
	copy /y Build\Volume\AOSLDR.BIN F:\
	xcopy "Build\Volume\DRIVERS" "F:\DRIVERS\" /E /I /H /Y
	copy /y Build\Volume\tree.elf F:\
) else (
	echo F: not found
)
pause