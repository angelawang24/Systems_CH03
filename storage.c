
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <stdbool.h>
#include <time.h>

#include "storage.h"
#include "inode.h"
#include "pages.h"
#include "directory.h"
#include "util.h"


typedef struct file_data {
	const char* path;
	int mode;
	const char* data;
} file_data;

static file_data file_table[] = {
	{"/", 040755, 0},
	{"/hello.txt", 0100644, "hello\n"},
	{0, 0, 0},
};

void*
find_corresponding_block(int inum)
{
	void* begin_ptr = get_pages_base();
	begin_ptr += 9;
	
	void* block_ptr = begin_ptr + (NUM_BLOCKS * INODE_SIZE) + (inum * SIZE_BLOCKS);
	return block_ptr;
}


inode*
get_inode(int inum) 
{
	void* inode_ptr = get_pages_base() + 9 + (INODE_SIZE * inum);
	return (inode*) inode_ptr;
}


inode* 
find_inode_within(char* cur_data, inode* cur_node) 
{
	void* corr_block = find_corresponding_block(cur_node->number);
	directory* dir = (directory*) corr_block;

	void* dirent_start = corr_block + sizeof(directory);
	dirent* dirent_ptr = (dirent*)dirent_start;
	void* inode_for_dirent_ptr = get_pages_base() + 9 + (dirent_ptr->inum * INODE_SIZE);
	inode* inode_for_dirent = (inode*)inode_for_dirent;
	int name_length = inode_for_dirent->name_length;

	for (int ii = 0; ii < dir->num_dirents; ++ii) {
		if (streq(cur_data, dirent_ptr->name) == 1) {
			int inode_num = dirent_ptr->inum;
			return get_inode(inode_num);
		}
		dirent_ptr++;
	}
	return NULL;
	
}


inode* 
find_inode(const char *path)
{
	slist* all_path = s_split(path, '/')->next;
	
	void* cur_node_number_ptr = get_pages_base();
	int* cur_node_number = (int*)cur_node_number_ptr;
	int num = *(cur_node_number);
	
	// first inode/root's inode
	void* cur_node_ptr = get_pages_base() + 9 + (INODE_SIZE * num);
	inode* cur_node = (inode*)cur_node_ptr;

	while((all_path != NULL) && (cur_node->filetype == 3)) {
		char* cur_data = (char*)all_path->data;
		inode* inode_with_data = find_inode_within(cur_data, cur_node);
		cur_node = inode_with_data;
		all_path = all_path->next;
		
	}
	// didn't finish finding the thing at the end of path
	if (all_path != NULL && cur_node->filetype == 2) {
		return NULL;
	}
	return cur_node;	
}

inode* 
find_second_to_last_inode(const char *path)
{
	slist* all_path = s_split(path, '/')->next;
	
	void* cur_node_number_ptr = get_pages_base();
	int* cur_node_number = (int*)cur_node_number_ptr;
	int num = *(cur_node_number);
	
	// first inode/root's inode
	void* cur_node_ptr = get_pages_base() + 9 + (INODE_SIZE * num);
	inode* cur_node = (inode*)cur_node_ptr;

	while((all_path->next != NULL) && (cur_node->filetype == 3)) {
		char* cur_data = (char*)all_path->data;
		inode* inode_with_data = find_inode_within(cur_data, cur_node);
		cur_node = inode_with_data;
		all_path = all_path->next;
		
	}
	// didn't finish finding the thing at the end of path
	if (all_path != NULL && cur_node->filetype == 2) {
		return NULL;
	}


	return cur_node;
}

void
storage_init(const char* path)
{
	pages_init(path);

	void* base_ptr = get_pages_base();
	void* ptr = base_ptr;

	// superblock root node is initialized to 0
	*((int*) ptr) = 0;
	// superblock will have char array of init in it if it has been initialized
	char init[4] = {'i', 'n', 'i', 't'};
	ptr += 4;

	if (strcmp(init, (char*)ptr) != 0) { 
		strcpy(ptr, init);
		// move pointer past superblock
		ptr += 5;
	
		inode* cur = (inode*)ptr;

		// initialize all inodes
		for (int ii = 0; ii < NUM_BLOCKS; ii++) {

			cur->number = ii;
			cur->parent = -1;
			cur->index = ii;
			cur->num_hard_links = 0;
			cur->overflow = -1;
			cur->mode = 0;
			cur->filetype = -1;
			cur->userID = getuid();
			cur->groupID = getgid();
			cur->free = true;
			cur->block_num = ii;
			cur->name_length = -1;
			cur->size = -1;
			cur->create_time = 0;
			cur->mod_time = 0;
			cur->access_time = 0;

			cur++; 
	
		}

		base_ptr += 9;
		// initialize root directory (mnt)
		inode* root_inode = (inode*)base_ptr;
		root_inode->filetype = 3;
		root_inode->free = false;
		root_inode->name_length = 3;
		root_inode->mode = 040755;

		root_inode->create_time = time(NULL);
		root_inode->mod_time = time(NULL);
		root_inode->access_time = time(NULL);

		// initialize root directory data block
		void* root_block_ptr = ((void*)root_inode) + (sizeof(inode)*NUM_BLOCKS);
		directory* root_block = (directory*) root_block_ptr;
		root_block->inum = root_inode->number;
		root_block->num_dirents = 0;
	}
	
}

static file_data*
get_file_data(const char* path) {
	for (int ii = 0; 1; ++ii) {
		file_data row = file_table[ii];

		if (file_table[ii].path == 0) {
			break;
		}

		if (streq(path, file_table[ii].path)) {
			return &(file_table[ii]);
		}
	}

	return 0;
}

int
get_stat(const char* path, struct stat* st)
{
	//file_data* dat = get_file_data(path);
	inode* dat = find_inode(path);
	
	if (!dat) {
		return -1;
	}

	memset(st, 0, sizeof(struct stat));
	
	st->st_nlink = (nlink_t)dat->num_hard_links;
	st->st_uid  = dat->userID;
	st->st_gid = dat->groupID;
	//printf("node name_length: %zu\n", dat->name_length);
	//printf("node mode: %08o\n", dat->mode);
	st->st_mode = dat->mode;
	
	st->st_size = dat->size;
	st->st_atime = dat->access_time;
	st->st_mtime = dat->mod_time;
	st->st_ctime = dat->create_time;

	st->st_blksize = (blksize_t)SIZE_BLOCKS;
	st->st_blocks = (blkcnt_t)NUM_BLOCKS;

	return 0;
}

const char*
get_data(const char* path)
{
	file_data* dat = get_file_data(path);
	if (!dat) {
		return 0;
	}

	return dat->data;
}

