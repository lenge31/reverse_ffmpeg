#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <errno.h>
#include <string.h>
#include <linux/fb.h>

unsigned int virt_to_phys(unsigned int virt);

static char *file_pagemap = "/proc/self/pagemap";
static int fd_pagemap = -1;

#define BUF_SIZE	1280*720*4
static unsigned char *pBuf = NULL;

#define MMAP_SIZE	1280*720*4
static unsigned char *pMmap_buf = NULL;
static char *file_m0032_ctrl = "/proc/m0032_ctrl";
static int fd_m0032_ctrl = -1;

static char *file_fb = "/dev/graphics/fb0";
static int fd_fb = -1;

int main(int argc, char *argv[])
{
	printf("argc=%d, argv[0]=%s\n", argc, argv[0]);

	pBuf = (unsigned char*)malloc(BUF_SIZE);
	if (pBuf == NULL) {
		printf("malloc failed.\n");
		goto out;
	}
	if (mlock(pBuf, BUF_SIZE)) {
		printf("mlock failed.\n");
		goto out;
	}
	printf("pBuf: virt=%p, phys=0x%x\n", pBuf, virt_to_phys((unsigned int)pBuf));

	fd_m0032_ctrl = open(file_m0032_ctrl, O_RDWR);
	if (fd_m0032_ctrl < 0) {
		printf("open %s failed.\n", file_m0032_ctrl);
		goto out;
	}

	pMmap_buf = mmap(NULL, MMAP_SIZE, PROT_READ|PROT_WRITE, MAP_SHARED|MAP_LOCKED, fd_m0032_ctrl, 0);
	if (!pMmap_buf || pMmap_buf==MAP_FAILED) {
		printf("mmap failed(%d:%s).\n", errno, strerror(errno));
		goto out;
	}

	memcpy(pMmap_buf, "123456789", 10);
	memcpy(pBuf, "abcdefghi", 10);
	printf("pMmap_buf=%s, pBuf=%s\n", pMmap_buf, pBuf);

	printf("pMmap_buf: virt=%p, phys=0x%x\n", pMmap_buf, virt_to_phys((unsigned int)pMmap_buf));

	fd_fb = open(file_fb, O_RDWR);
	if (fd_fb < 0) {
		printf("open %s failed.\n", file_fb);
		goto out;
	}
	if (argc > 1) {
		if (ioctl(fd_fb, FBIOBLANK, 4)) {
			printf("ioctl %s failed(%d:%s).\n", file_fb, errno, strerror(errno));
			goto out;
		}
	}
out:
	if (fd_pagemap >= 0) { close(fd_pagemap); fd_pagemap = -1; }
	if (pBuf != NULL) { munlock(pBuf, BUF_SIZE); free(pBuf); pBuf = NULL; }
	if (fd_m0032_ctrl >= 0) { close(fd_m0032_ctrl); fd_m0032_ctrl = -1; }
	if (!pMmap_buf) { munmap(pMmap_buf, MMAP_SIZE); pMmap_buf = NULL; }
	if (fd_fb >= 0) { close(fd_fb); fd_fb = -1; }

	return 0;
}

#define PAGE_PFN_MASK	((((unsigned long long)1)<<55) - 1)
#define PAGE_PRESENT_MASK	(((unsigned long long)1)<<63)
#define PAGE_SWAP_MASK	(((unsigned long long)1)<<62)
unsigned int virt_to_phys(unsigned int virt)
{
	int pfn_offset = 0;
	unsigned long long pfn = 0;
	static int page_size = 0;
	static int page_nums = 0;

	if (fd_pagemap < 0) {
		fd_pagemap = open(file_pagemap, O_RDONLY);
		if (fd_pagemap < 0) {
			printf("open %s failed.\n", file_pagemap);
			return 0;
		}
		page_size = sysconf(_SC_PAGESIZE);
		printf("page_size:%d\n", page_size);
		page_nums = sysconf(_SC_PHYS_PAGES);
		printf("page_nums:%d\n", page_nums);
	}

	pfn_offset = virt/page_size*8;
	if (pfn_offset != lseek(fd_pagemap, pfn_offset, SEEK_SET)) {
		printf("lseek %s failed.\n", file_pagemap);
		return 0;
	}

	if (8 != read(fd_pagemap, &pfn, 8)) {
		printf("read %s failed.\n", file_pagemap);
		return 0;
	}

	printf("pfn_offset=%d, pfn=0x%llx\n", pfn_offset, pfn);

	if (!(pfn&PAGE_SWAP_MASK) && (pfn&PAGE_PRESENT_MASK)) {
		return (unsigned int)(pfn&PAGE_PFN_MASK)*page_size + virt%page_size;
	} else if (pfn & PAGE_SWAP_MASK) {
		printf("pages are swapped.\n");
	} else {
		printf("pages are not present.\n");
	}

	return 0;
}
