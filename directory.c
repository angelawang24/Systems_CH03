#define _GNU_SOURCE
#include <string.h>

#include <sys/mman.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <assert.h>
#include <fcntl.h>
#include <errno.h>
#include <stdio.h>

#include "directory.h"
#include "slist.h"
#include "pages.h"

int 
directory_put_ent(directory* dd, const char* name, int pnum)
{
	void* begin_ptr = (void*)get_pages_base();

	begin_ptr += 9;

	begin_ptr += ((NUM_BLOCKS - pnum) * INODE_SIZE);
	// pointer for inode with pnum as its number
	void* inode_ptr = begin_ptr;
	inode* inode_at_pnum = (inode*)inode_ptr;
	
	// increment pointer to where the data block is for the inode
	void* begin_block_ptr = (void*)get_pages_base() + 9 + (INODE_SIZE * NUM_BLOCKS);
	begin_block_ptr += (dd->inum * SIZE_BLOCKS);
	begin_block_ptr += sizeof(directory);
	begin_block_ptr += (sizeof(dirent) * dd->num_dirents);
	dirent* new_dirent_ptr = (dirent*)begin_block_ptr;
	new_dirent_ptr->inum = pnum;

	strcpy(new_dirent_ptr->name, name);
	
	dd->num_dirents = dd->num_dirents + 1;
	return 0;
}