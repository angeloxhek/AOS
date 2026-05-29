#include <aoslib.h>
#include <vfs/disk_interface.h>

typedef struct {
    uint16_t base;
    uint8_t  is_slave;
    uint64_t total_sectors;
} ide_drive_t;

static ide_drive_t ide_drives[4];
static int ide_drive_count = 0;

static void ide_wait_bsy(uint16_t base) {
    while (hal_inb(base + 7) & 0x80);
}

static void ide_wait_drq(uint16_t base) {
    while (!(hal_inb(base + 7) & 0x08));
}

static int ide_init(void) {
    uint16_t bases[2] = { 0x1F0, 0x170 };
    ide_drive_count = 0;

    for (int bus = 0; bus < 2; bus++) {
        for (int slave = 0; slave < 2; slave++) {
            uint16_t base = bases[bus];
            
            hal_outb(base + 6, 0xA0 | (slave << 4)); 
            for(int i=0; i<4; i++) hal_inb(base + 7);
            
            hal_outb(base + 2, 0); hal_outb(base + 3, 0);
            hal_outb(base + 4, 0); hal_outb(base + 5, 0);
            hal_outb(base + 7, 0xEC);
            
            if (hal_inb(base + 7) == 0) continue; 
            
            ide_wait_bsy(base);
            if (hal_inb(base + 4) != 0 || hal_inb(base + 5) != 0) continue;
            
            ide_wait_drq(base);
            uint16_t ident[256];
            hal_insw(base, ident, 256);
            
            ide_drives[ide_drive_count].base = base;
            ide_drives[ide_drive_count].is_slave = slave;
            ide_drives[ide_drive_count].total_sectors = *((uint32_t*)&ident[60]);
            ide_drive_count++;
        }
    }
    return ide_drive_count;
}

static disk_instance_t ide_get_disk(int index, uint64_t* out_sectors, uint32_t* out_sec_size) {
    if (index < 0 || index >= ide_drive_count) return 0;
    *out_sectors = ide_drives[index].total_sectors;
    *out_sec_size = 512;
    return &ide_drives[index];
}

static int ide_read(disk_instance_t disk, uint64_t lba, uint64_t count, void* buf) {
    ide_drive_t* d = (ide_drive_t*)disk;
    uint8_t* ptr = (uint8_t*)buf;
    
    for (uint64_t i = 0; i < count; i++, lba++) {
        ide_wait_bsy(d->base);
        hal_outb(d->base + 6, 0xE0 | (d->is_slave << 4) | ((lba >> 24) & 0x0F));
        hal_outb(d->base + 2, 1);
        hal_outb(d->base + 3, (uint8_t)lba);
        hal_outb(d->base + 4, (uint8_t)(lba >> 8));
        hal_outb(d->base + 5, (uint8_t)(lba >> 16));
        hal_outb(d->base + 7, 0x20);
        
        ide_wait_bsy(d->base);
        ide_wait_drq(d->base);
        hal_insw(d->base, ptr, 256);
        ptr += 512;
    }
    return 0;
}

static int ide_write(disk_instance_t disk, uint64_t lba, uint64_t count, const void* buf) {
    ide_drive_t* d = (ide_drive_t*)disk;
    const uint8_t* ptr = (const uint8_t*)buf;
    
    for (uint64_t i = 0; i < count; i++, lba++) {
        ide_wait_bsy(d->base);
        hal_outb(d->base + 6, 0xE0 | (d->is_slave << 4) | ((lba >> 24) & 0x0F));
        hal_outb(d->base + 2, 1);
        hal_outb(d->base + 3, (uint8_t)lba);
        hal_outb(d->base + 4, (uint8_t)(lba >> 8));
        hal_outb(d->base + 5, (uint8_t)(lba >> 16));
        hal_outb(d->base + 7, 0x30);
        
        ide_wait_bsy(d->base);
        ide_wait_drq(d->base);
        hal_outsw(d->base, ptr, 256);
        ptr += 512;
    }
    hal_outb(d->base + 7, 0xE7);
    ide_wait_bsy(d->base);
    return 0;
}

disk_driver_t ide_disk_driver = {
    .name = "ide", .init = ide_init, .get_disk = ide_get_disk, .read = ide_read, .write = ide_write
};