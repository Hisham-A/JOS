
#include "fs.h"
#define MAX_BLOCK_CACHE_SIZE 100
#define MIN_FREE_BLOCK_CACHE_SIZE 10

static int block_cache_size =  0;


void evict_blocks() {
	int free_blocks = 0;
	int i;
	while(free_blocks < MIN_FREE_BLOCK_CACHE_SIZE) {
		for(i=3; i < super->s_nblocks; i++) {
			if(free_blocks >= MIN_FREE_BLOCK_CACHE_SIZE)
				break;
			uint32_t addr = (uint32_t)diskaddr(i);
			if(vpd[PDX(addr)] & PTE_P) {
				if(vpt[VPN(addr)] & PTE_P) {
					if(vpt[VPN(addr)] & PTE_A) {
					//	cprintf("Majd the great =) : %d\n",i);
					//vpt[VPN(addr)] &= ~PTE_A;
						if(sys_clear_block_access_bit(0, (void*) addr) < 0 )
							panic("sys_clear_block_access_bit in evict blocks\n");
					} else {
						flush_block((void*)addr);
						sys_page_unmap(0, (void*)addr);
						free_blocks++;
					}
				} else {
					free_blocks++;
				}
			}else {
				free_blocks++;
			}
				
		}
	
	}


}
// Return the virtual address of this disk block.
void*
diskaddr(uint32_t blockno)
{
	if (blockno == 0 || (super && blockno >= super->s_nblocks))
		panic("bad block number %08x in diskaddr", blockno);
	return (char*) (DISKMAP + blockno * BLKSIZE);
}

// Is this virtual address mapped?
bool
va_is_mapped(void *va)
{
	return (vpd[PDX(va)] & PTE_P) && (vpt[VPN(va)] & PTE_P);
}

// Is this virtual address dirty?
bool
va_is_dirty(void *va)
{
	return (vpt[VPN(va)] & PTE_D) != 0;
}

// Fault any disk block that is read or written in to memory by
// loading it from disk.
// Hint: Use ide_read and BLKSECTS.
static void
bc_pgfault(struct UTrapframe *utf)
{
	void *addr = (void *) utf->utf_fault_va;
	uint32_t blockno = ((uint32_t)addr - DISKMAP) / BLKSIZE;
	int r;

	// Check that the fault was within the block cache region
	if (addr < (void*)DISKMAP || addr >= (void*)(DISKMAP + DISKSIZE))
		panic("page fault in FS: eip %08x, va %08x, err %04x",
		      utf->utf_eip, addr, utf->utf_err);

	// Allocate a page in the disk map region and read the
	// contents of the block from the disk into that page.
	//
	// LAB 5: Your code here
	if(block_cache_size > MAX_BLOCK_CACHE_SIZE)
		evict_blocks();
	
	if(sys_page_alloc(sys_getenvid(), ROUNDDOWN(addr, PGSIZE), PTE_USER) <0)
		panic("page fault in FS error allocating page\n");
	uint32_t va;
	va = (uint32_t)addr;
	if(ide_read(blockno*BLKSECTS, ROUNDDOWN(addr, PGSIZE), BLKSECTS) != 0)
		panic("page fault in FS  error reading from disk\n");
	// Sanity check the block number. (exercise for the reader:
	// why do we do this *after* reading the block in?)
	if (super && blockno >= super->s_nblocks)
		panic("reading non-existent block %08x\n", blockno);

	// Check that the block we read was allocated.
	if (bitmap && block_is_free(blockno))
		panic("reading free block %08x\n", blockno);
	block_cache_size++;
}

// Flush the contents of the block containing VA out to disk if
// necessary, then clear the PTE_D bit using sys_page_map.
// If the block is not in the block cache or is not dirty, does
// nothing.
// Hint: Use va_is_mapped, va_is_dirty, and ide_write.
// Hint: Use the PTE_USER constant when calling sys_page_map.
// Hint: Don't forget to round addr down.
void
flush_block(void *addr)
{
	uint32_t blockno = ((uint32_t)addr - DISKMAP) / BLKSIZE;

	if (addr < (void*)DISKMAP || addr >= (void*)(DISKMAP + DISKSIZE))
		panic("flush_block of bad va %08x", addr);

	// LAB 5: Your code here.
	//panic("flush_block not implemented");
	if(va_is_mapped(addr) && va_is_dirty(addr))  {
		if(ide_write(blockno * BLKSECTS, ROUNDDOWN(addr, PGSIZE) , BLKSECTS) < 0 )
			panic("flush_block error in writing to ide");
		sys_page_map(sys_getenvid(), addr, sys_getenvid(), addr, PTE_USER);
	}
}

// Test that the block cache works, by smashing the superblock and
// reading it back.
static void
check_bc(void)
{
	struct Super backup;

	// back up super block
	memmove(&backup, diskaddr(1), sizeof backup);

	// smash it 
	strcpy(diskaddr(1), "OOPS!\n");
	flush_block(diskaddr(1));
	assert(va_is_mapped(diskaddr(1)));
	assert(!va_is_dirty(diskaddr(1)));

	// clear it out
	sys_page_unmap(0, diskaddr(1));
	assert(!va_is_mapped(diskaddr(1)));

	// read it back in
	assert(strcmp(diskaddr(1), "OOPS!\n") == 0);

	// fix it
	memmove(diskaddr(1), &backup, sizeof backup);
	flush_block(diskaddr(1));

	cprintf("block cache is good\n");
}

void
bc_init(void)
{
	set_pgfault_handler(bc_pgfault);
	check_bc();
}

