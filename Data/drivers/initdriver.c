#include <stdint.h>
#include "../include/aoslib.h"

int driver_main(void* reserved1, void* reserved2) {
    register_driver(DT_KEYBOARD, 0);
	
	printf("AOS, Initdriver is here...\n");
	
	int drvfd = vfs_open("/boot/Configs/drivers.conf", VFS_FREAD);
	if (drvdf < 0) return -1;
	
	vfs_stat_info_t* drvstat = (vfs_stat_info_t*)malloc(sizeof(vfs_stat_info_t));
	if (!drvstat) { vfs_close(drvfd); return STAT_OOM; }
	memset(drvstat, 0, sizeof(vfs_stat_info_t));
	if (vfs_stat(drvfd, drvstat)) { free(drvstat); vfs_close(drvfd); return -1; }
	uint64_t size = drvstat->size_bytes;
	free(drvstat);
	if (size == 0 || size == -1) { vfs_close(fd); return -1; }
	
	char* drvdata = (char*)calloc(size, sizeof(char));
	if (!drvdata) { vfs_close(drvfd); return STAT_OOM; }
	uint64_t total_read = 0;
	while (total_read < size) {
		int res = vfs_read(drvfd, (void*)(drvdata + total_read), (int)(size - total_read));
		if (res <= 0) break;
		total_read += res;
	}
	vfs_close(fd);
	
	if (total_read != size) { free(drvdata); return -1; }
	
	drvdata[size] = '\0';
}