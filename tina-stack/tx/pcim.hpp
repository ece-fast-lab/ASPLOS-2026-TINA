#include <stdint.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <errno.h>
#include <string.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>

// This structure holds the state for our mapped PCI device.
typedef struct
{
    int fd;            // File descriptor for the sysfs resource
    void *map_base;    // Base address of the mmap()ed region
    size_t map_size;   // Size of the mapping
    off_t mapped_base; // Base offset that was mapped (page-aligned)
    size_t page_size;  // System page size (from sysconf)
} pcimem_dev_t;

pcimem_dev_t *pcimem_init(const char *sysfile, off_t offset, size_t length)
{
    pcimem_dev_t *dev = new pcimem_dev_t();
    if (!dev)
    {
        return NULL;
    }

    // Get system page size
    dev->page_size = sysconf(_SC_PAGE_SIZE);
    if (dev->page_size <= 0)
    {
        delete (dev);
        return NULL;
    }

    // Open the PCI resource file
    dev->fd = open(sysfile, O_RDWR | O_SYNC);
    if (dev->fd == -1)
    {
        fprintf(stderr, "Error opening %s: %s\n", sysfile, strerror(errno));
        delete (dev);
        return NULL;
    }

    // Compute a page-aligned base offset for the mapping.
    dev->mapped_base = offset & ~(dev->page_size - 1);

    // Compute the mapping size needed to cover the desired region.
    dev->map_size = (offset - dev->mapped_base) + length;

    // Memory-map the device
    dev->map_base = mmap(NULL, dev->map_size,
                         PROT_READ | PROT_WRITE, MAP_SHARED,
                         dev->fd, dev->mapped_base);
    if (dev->map_base == MAP_FAILED)
    {
        fprintf(stderr, "Error mapping memory: %s\n", strerror(errno));
        close(dev->fd);
        delete (dev);
        return NULL;
    }

    return dev;
}

int pcimem_write(pcimem_dev_t *dev, off_t offset, char access_type, uint64_t value)
{
    if (!dev || !dev->map_base)
    {
        fprintf(stderr, "Device not initialized.\n");
        return -1;
    }

    // Make sure the requested offset falls within our mapped region.
    if (offset < dev->mapped_base || ((size_t)(offset - dev->mapped_base) + 1 > dev->map_size))
    {
        fprintf(stderr, "Offset 0x%lx is outside the mapped region [0x%lx, 0x%lx).\n",
                (unsigned long)offset, (unsigned long)dev->mapped_base,
                (unsigned long)(dev->mapped_base + dev->map_size));
        return -1;
    }

    // Compute the virtual address corresponding to the desired offset.
    void *virt_addr = (char *)dev->map_base + (offset - dev->mapped_base);

    // Write the value using the appropriate width.
    switch (tolower(access_type))
    {
    case 'b':
        *((volatile uint8_t *)virt_addr) = (uint8_t)value;
        break;
    case 'h':
        *((volatile uint16_t *)virt_addr) = (uint16_t)value;
        break;
    case 'w':
        *((volatile uint32_t *)virt_addr) = (uint32_t)value;
        break;
    case 'd':
        *((volatile uint64_t *)virt_addr) = (uint64_t)value;
        break;
    default:
        fprintf(stderr, "Invalid access type '%c'.\n", access_type);
        return -1;
    }

    return 0;
}

void pcimem_cleanup(pcimem_dev_t *dev)
{
    if (!dev)
        return;
    if (dev->map_base && dev->map_base != MAP_FAILED)
        munmap(dev->map_base, dev->map_size);
    if (dev->fd != -1)
        close(dev->fd);
    delete (dev);
}