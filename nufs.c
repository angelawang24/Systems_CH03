#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <errno.h>
#include <sys/stat.h>
#include <bsd/string.h>
#include <assert.h>
#include <stdbool.h>

#define FUSE_USE_VERSION 26
#include <fuse.h>

#include "storage.h"
#include "inode.h"
#include "slist.h"
#include "directory.h"
#include "pages.h"
#include "util.h"

char*
get_name(const char *path)
{
	slist* all_name = s_split(path, '/');
	char* name = all_name->data;
	while (all_name != NULL) {
		name = all_name->data;
		all_name = all_name->next;
	}
	return name;
}

int
rename_dirent_to(dirent* d, const char* name_to)
{
	int name_to_len = strlen(name_to) - 1;
	//printf("name to len: %i\n", name_to_len);
	for (int ii = 0; ii < 100; ++ii) {
		if (ii > name_to_len) {
			d->name[ii] = 0;
		} else {
			d->name[ii] = name_to[ii];
		}
	}
	return 0;
	
}

dirent*
find_dirent_in_directory(dirent* first_dirent, int inum)
{
	dirent* start_dirent = first_dirent;

	int count = 0;
	while (start_dirent->inum != inum && count < 39) {
		start_dirent++;
	}
	if (start_dirent->inum == inum) {
		return start_dirent;
	}

	return NULL;
}

dirent*
find_dirent(directory* block_dir, int inum)
{
	void* block_ptr = (void*)block_dir;
	block_ptr += sizeof(directory);
	
	dirent* start_dirent = (dirent*)block_ptr;
	int count = 0;

	inode* node = get_pages_base() + 9 + (inum * INODE_SIZE);
	int overflow = node->overflow;
	
	while (find_dirent_in_directory(start_dirent, inum) == NULL) {
		if (overflow == -1) {
			return NULL;
		} else {
			void* overflow_block = find_corresponding_block(overflow);
			int overflow_inum = ((directory*)overflow_block)->inum;

			overflow_block += sizeof(directory);
			start_dirent = (dirent*)overflow_block;
			void* o_inum = get_pages_base() + 9 + (overflow_inum * INODE_SIZE);
			overflow = ((inode*)o_inum)->overflow;
		}
	}
	
}

// implementation for: man 2 access
// Checks if a file exists.
int
nufs_access(const char *path, int mask)
{
	printf("access(%s, %04o)\n", path, mask);
	inode* node = find_inode(path);

	if (node == NULL) {
		return -ENOENT;
	}

	node->access_time = time(NULL);

	return 0;
}

// implementation for: man 2 stat
// gets an object's attributes (type, permissions, size, etc)
int
nufs_getattr(const char *path, struct stat *st)
{
	printf("getattr(%s)\n", path);
	int rv = get_stat(path, st);
	
	//printf("mode: %08o\n", st->st_mode);
	if (rv == -1) {
		return -ENOENT;
	}
	else {
		return 0;
	}
}

char*
get_path_name(char* name_ptr, const char* path)
{
	int length = strlen(path) + 1 + strlen(name_ptr);
	char path_name[length];
	strcat(path_name, path);
	strcpy(path_name, "/");
	strcat(path_name, name_ptr);
	char* path_name_ptr = (char*)&(path_name);
	return path_name_ptr;

}

// implementation for: man 2 readdir
// lists the contents of a directory
int
nufs_readdir(const char *path, void *buf, fuse_fill_dir_t filler,
             off_t offset, struct fuse_file_info *fi)
{
	struct stat st;

	printf("readdir(%s)\n", path);

	get_stat("/", &st);
	filler(buf, ".", &st, 0);
	
	inode* direct = find_inode(path);
	void* block_ptr = find_corresponding_block(direct->number);
	directory* block_direct = (directory*)block_ptr;

	void* begin_dirents = block_ptr + sizeof(directory);

	dirent* cur_dirent = (dirent*)begin_dirents;
	for (int ii = 0; ii < block_direct->num_dirents; ++ii) {
		if (cur_dirent->inum != -1) {
			char* name_ptr = (char*)&(cur_dirent->name);
			char* path_name_ptr = get_path_name(name_ptr, path);
			int length = strlen(path_name_ptr);

			char path[length];
			strcpy(path, path_name_ptr);

			get_stat(path, &st);
			filler(buf, cur_dirent->name, &st, 0);
		}
		cur_dirent++;
	}
	
	return 0;
}

// mknod makes a filesystem object like a file or directory
// called for: man 2 open, man 2 link
int
nufs_mknod(const char *path, mode_t mode, dev_t rdev)
{
	printf("mknod(%s, %04o)\n", path, mode);

	void* ptr = get_pages_base();
	void* ptr_2 = get_pages_base();
	ptr_2 += 9;
	ptr += 9;

	inode* cur = (inode*)ptr;
	inode* free = NULL;
	// find first free inode
	while (!cur->free) {
		cur += 1;
	}

	free = cur;
	// if no more free inodes
	if (free == NULL) {
		return (-EDQUOT);
	}

	// is a directory
	inode* last_directory_inode = find_second_to_last_inode(path);
	void* last_direct_ptr = find_corresponding_block(last_directory_inode->number);

	// first free inode
	void* free_block_ptr = (void*)free;
	free->parent = last_directory_inode->number;
	free->mode = 0100644;
	free->filetype = 2;
	free->free = false;

	slist* all_name = s_split(path, '/');
	char* name = all_name->data;
	while (all_name != NULL) {
		name = all_name->data;
		all_name = all_name->next;
	}
	
	free->name_length = strlen(name);
	free->num_hard_links = 1;
	free->size = 0; 
	free->create_time = time(NULL);
	free->mod_time = time(NULL);
	free->access_time = time(NULL);
	
	directory* root_d_pointer = (directory*)last_direct_ptr;
	directory_put_ent(root_d_pointer, name, free->number);	

	void* d_ptr = get_pages_base() + 9 + (INODE_SIZE * NUM_BLOCKS) + SIZE_BLOCKS;
	dirent* d = (dirent*)d_ptr;	

	return 0;
}

// most of the following callbacks implement
// another system call; see section 2 of the manual
int
nufs_mkdir(const char *path, mode_t mode)
{
	printf("mkdir(%s)\n", path);

	void* ptr = get_pages_base();
	void* ptr_2 = get_pages_base();
	ptr_2 += 9;
	ptr += 9;

	inode* cur = (inode*)ptr;
	inode* free = NULL;
	// find first free inode
	while (!cur->free) {
		cur += 1;
	}

	free = cur;
	// if no more free inodes
	if (free == NULL) {
		return (-EDQUOT);
	}

	// is a directory
	inode* last_directory_inode = find_second_to_last_inode(path);
	void* last_direct_ptr = find_corresponding_block(last_directory_inode->number);

	// first free inode
	void* free_block_ptr = (void*)free;
	free->parent = last_directory_inode->number;
	free->mode = 040755;
	free->filetype = 3;
	free->free = false;

	char* name = get_name(path);
	
	free->num_hard_links = 1;
	free->name_length = strlen(name);
	free->size = 0; 
	free->create_time = time(NULL);
	free->mod_time = time(NULL);
	free->access_time = time(NULL);
	
	directory* root_d_pointer = (directory*)last_direct_ptr;
	directory_put_ent(root_d_pointer, name, free->number);

	// make directory at data block for this free inode
	void* freeblock_ptr = find_corresponding_block(free->number);
	directory* freeblock = (directory*)freeblock_ptr;
	freeblock->inum = free->number;
	freeblock->num_dirents = 0;
		

	return 0;	
}

int
nufs_link(const char *from, const char *to)
{
	printf("link(%s => %s)\n", to, from);
	inode* from_node = find_inode(from);
	if (from_node == NULL) {
		return -ENOENT;
	}

	from_node->num_hard_links += 1;
	inode* to_inode;
	inode* parent_to_node = find_second_to_last_inode(to);

	void* ptr = get_pages_base();
	ptr += 9;

	inode* cur = (inode*)ptr;

	// find first free inode
	while (!cur->free) {
		cur += 1;
	}

	to_inode = cur;
	// if no more free inodes
	if (to_inode == NULL) {
		return (-EDQUOT);
	}

	// is a directory
	inode* last_directory_inode = parent_to_node;
	void* last_direct_ptr = find_corresponding_block(last_directory_inode->number);

	// first free inode
	void* free_block_ptr = (void*)to_inode;
	to_inode->number = from_node->number;
	to_inode->parent = last_directory_inode->number;
	to_inode->mode = 0100644;
	to_inode->filetype = 2;
	to_inode->free = false;

	slist* all_name = s_split(to, '/');
	char* name = all_name->data;
	while (all_name != NULL) {
		name = all_name->data;
		all_name = all_name->next;
	}
	
	to_inode->name_length = strlen(name);
	to_inode->num_hard_links = 1;
	to_inode->size = 0; // 0 because no data in it yet
	to_inode->create_time = time(NULL);
	to_inode->mod_time = time(NULL);
	to_inode->access_time = time(NULL);

	directory* root_d_pointer = (directory*)last_direct_ptr;
	directory_put_ent(root_d_pointer, name, to_inode->number);	


	return 0;
}

int
nufs_unlink(const char *path)
{
	printf("unlink(%s)\n", path);

	inode* node = find_inode(path);
	
	if (node == NULL) {
		return -ENOENT;
	}

	if (node->filetype == 3) {
		return -EISDIR;
	}

	inode* parent = find_second_to_last_inode(path);
	void* parent_block = find_corresponding_block(parent->number);
	directory* parent_dir = (directory*)parent_block;

	dirent* start_dirent = find_dirent(parent_dir, node->number);

	for (int ii = 0; ii < 100; ++ii) {
		start_dirent->name[ii] = 0;
	}
	start_dirent->inum = -1;
	
	if (node->num_hard_links <= 1) {
		node->free = true;
	} else if (node->number != node->index) {
		node->free = true;
	} else {
		node->free = false;
	}	

	node->number = node->index;
	node->parent = -1;
	node->num_hard_links -= 1;
	node->overflow = -1;
	node->mode = 0;
	node->filetype = -1;
	node->userID = getuid();
	node->groupID = getgid();
	node->name_length = -1;
	node->size = -1;
	node->create_time = 0;
	node->mod_time = 0;
	node->access_time = 0;
	
	return 0;
}

int
nufs_rmdir(const char *path)
{
	printf("rmdir(%s)\n", path);

	inode* node = find_inode(path);
	
	if (node == NULL) {
		return -ENOENT;
	}

	inode* parent = find_second_to_last_inode(path);
	void* parent_block = find_corresponding_block(parent->number);
	directory* parent_dir = (directory*)parent_block;

	dirent* start_dirent = find_dirent(parent_dir, node->number);

	for (int ii = 0; ii < 100; ++ii) {
		start_dirent->name[ii] = 0;
	}
	start_dirent->inum = -1;
	
	node->number = node->index;
	node->parent = -1;
	node->num_hard_links -= 1;
	node->overflow = -1;
	node->mode = 0;
	node->filetype = -1;
	node->userID = getuid();
	node->groupID = getgid();
	node->free = true;
	node->name_length = -1;
	node->size = -1;
	node->create_time = 0;
	node->mod_time = 0;
	node->access_time = 0;
	
	return 0;
}

// implements: man 2 rename
// called to move a file within the same filesystem
int
nufs_rename(const char *from, const char *to)
{
	printf("rename(%s => %s)\n", from, to);

	inode* node_from = find_inode(from);
	inode* node_from_directory = find_second_to_last_inode(from);
	inode* node_to_directory = find_second_to_last_inode(to);
	if (node_from == NULL || node_to_directory == NULL || node_from_directory == NULL) {
		return -ENOENT;
	}

	// gets name for the to file
	const char* name_to = get_name(to);

	node_from->name_length = strlen(name_to);
	void* block = find_corresponding_block(node_from_directory->number);
	directory* block_dir = (directory*)block;
	dirent* d = find_dirent(block_dir, node_from->number);
		
	if (node_from_directory->number == node_to_directory->number) {
		int success = rename_dirent_to(d, name_to);
		return success;
	} else {
		void* block_directory_to = find_corresponding_block(node_to_directory->number);
		directory* block_dir_to = (directory*)block_directory_to;
		
		int sucess = directory_put_ent(block_dir, name_to, d->inum);
		for (int ii = 0; ii < 100; ++ii) {
			d->name[ii] = 0;
		}
		d->inum = -1;
	}
		
	return 0;
}

int
nufs_chmod(const char *path, mode_t mode)
{
	printf("chmod(%s, %04o)\n", path, mode);
	
	inode* node = find_inode(path);
	if (node == NULL) {
		return -1;
	}
	node->mode = mode;

	return 0;
}

int
nufs_truncate(const char *path, off_t size)
{
	printf("truncate(%s, %ld bytes)\n", path, size);

	inode* node = find_inode(path);
	if (node == NULL) {
		return -1;
	}
	
	node->size = node->size + size;

	return 0;
}

// this is called on open, but doesn't need to do much
// since FUSE doesn't assume you maintain state for
// open files.
int
nufs_open(const char *path, struct fuse_file_info *fi)
{
	printf("open(%s)\n", path);
	return 0;
}

// Actually read data
int
nufs_read(const char *path, char *buf, size_t size, off_t offset, struct fuse_file_info *fi)
{
	printf("read(%s, %ld bytes, @%ld)\n", path, size, offset);

	inode* node_to_read_from = find_inode(path);
	// there is no node/file to read from
	if (node_to_read_from == NULL) {
		return -EBADF;
	}
	// trying to read from directory
	if (node_to_read_from->filetype == 3) {
		return -EISDIR;
	}

	void* block_to_read_from = find_corresponding_block(node_to_read_from->number);
	

	if (size + offset <= SIZE_BLOCKS) {
		void* begin_data_ptr = block_to_read_from + offset;
		const char* data = (char*)begin_data_ptr;
		
		int len = node_to_read_from->size + 1;
		if (size < len) {
			len = size;
		}
		strlcpy(buf, data, len);
	} else {
		void* begin_data_ptr = block_to_read_from;
		int how_much_more_offset = offset;
		while (how_much_more_offset > 0) {
			if (node_to_read_from->overflow == -1) {
				return -EINVAL;
			} else {
				if (how_much_more_offset <= SIZE_BLOCKS) {
					begin_data_ptr += how_much_more_offset;
					how_much_more_offset = 0;
				} else {
					begin_data_ptr = find_corresponding_block(node_to_read_from->overflow) + how_much_more_offset;
					how_much_more_offset -= 4096;
					node_to_read_from = get_pages_base() + 9 + (node_to_read_from->number * INODE_SIZE);
				}
			}
		}

		const char* data = (char*)begin_data_ptr;

		int len = node_to_read_from->size + 1;
		if (size < len) {
			len = size;
		}
		strlcpy(buf, data, len);
	}

	return size;
}

// Actually write data
int
nufs_write(const char *path, const char *buf, size_t size, off_t offset, struct fuse_file_info *fi)
{
	printf("write(%s, %ld bytes, @%ld)\n", path, size, offset);

	inode* node_to_write_to = find_inode(path);
	// no node/file exists with path
	if (node_to_write_to == NULL) {
		return -EBADF;
	}
	// node is a directory
	if (node_to_write_to->filetype == 3) {
		return -EINVAL;
	}

	void* block_to_write_to = find_corresponding_block(node_to_write_to->number);
	

	if (node_to_write_to->size + size + offset <= SIZE_BLOCKS) {
		//printf("size before 1: %zu\n", node_to_write_to->size);
		void* begin_data_ptr = block_to_write_to + offset + node_to_write_to->size;

		size_t old_size = node_to_write_to->size;
		size_t new_size = old_size + size;
		node_to_write_to->size = new_size;
		
		memcpy(begin_data_ptr, buf, size);
	} else {
		void* begin_data_ptr = block_to_write_to;
		int how_much_more_offset = offset;
		while (how_much_more_offset > 0) {
			//printf("size before 2: %zu, more offset: %i\n", node_to_write_to->size, how_much_more_offset);
			if (node_to_write_to->overflow == -1) {
				return -EINVAL;
			} else {
				if (how_much_more_offset + node_to_write_to->size <= SIZE_BLOCKS) {
					begin_data_ptr += how_much_more_offset;
					how_much_more_offset = 0;
				} else {
					begin_data_ptr = find_corresponding_block(node_to_write_to->overflow) + how_much_more_offset;
					how_much_more_offset -= 4096;
					node_to_write_to = get_pages_base() + 9 + (node_to_write_to->number * INODE_SIZE);
				}
			}
		}
		int leftover_size = SIZE_BLOCKS - node_to_write_to->size;
		if (size < leftover_size) {
			size_t old_size = node_to_write_to->size;
			size_t new_size = old_size + size;
			node_to_write_to->size = new_size;
			memcpy(block_to_write_to, buf, size);
		} else {
			int size_go_into_rest = size - leftover_size;
			node_to_write_to->size = SIZE_BLOCKS;
			memcpy(block_to_write_to, buf, leftover_size);
			void* buffer = (void*)buf;
			buffer += leftover_size;
			
			// FIND FIRST FREE INODE
			void* ptr = get_pages_base();
			void* ptr_2 = get_pages_base();
			ptr_2 += 9;
			ptr += 9;

			inode* cur = (inode*)ptr;
			inode* free = NULL;
			// find first free inode
			while (!cur->free) {
				cur += 1;
			}

			free = cur;
			// if no more free inodes
			if (free == NULL) {
				return (-EDQUOT);
			}

			node_to_write_to->overflow = free->number;
		
			free->parent = node_to_write_to->parent;
			free->mode = 0100644;
			free->filetype = 2;
			free->free = false;

			free->name_length = node_to_write_to->name_length;
			free->num_hard_links = node_to_write_to->num_hard_links;
			free->create_time = time(NULL);
			free->mod_time = time(NULL);
			free->access_time = time(NULL);

			void* free_block = find_corresponding_block(free->number);
			memcpy(free_block, buffer, size_go_into_rest);
		}		
	}
	return size;
}


int
storage_set_time(const char* path, const struct timespec ts[2])
{
	inode* node = find_inode(path);
	if (node == NULL) {
		return -1;
	}

	node->access_time = ts[0].tv_sec;
	node->mod_time = ts[1].tv_sec;
	return 0;
}


// Update the timestamps on a file or directory.
int
nufs_utimens(const char* path, const struct timespec ts[2])
{
    	int rv = storage_set_time(path, ts);
	printf("utimens(%s, [%ld, %ld; %ld %ld]) -> %d\n",
		path, ts[0].tv_sec, ts[0].tv_nsec, ts[1].tv_sec, ts[1].tv_nsec, rv);
	return rv;
}

void
nufs_init_ops(struct fuse_operations* ops)
{
	memset(ops, 0, sizeof(struct fuse_operations));
	ops->access   = nufs_access; //supposed to return true or false whether you have access to a given path, thin wrapper around stat
	ops->getattr  = nufs_getattr; //gets called either when user did stat system call, or literally every single time you try to do anything. If this returns the wrong result, it'll break literally everything, It also gets called when stat gets called I think
	ops->readdir  = nufs_readdir; //read contents of directory, essentially ls command
	ops->mknod    = nufs_mknod; //make file
	ops->mkdir    = nufs_mkdir; //make dir
	ops->unlink   = nufs_unlink; //takes in a PATH, delete, removes one link. if removes last, it removes the whole thing
	ops->rmdir    = nufs_rmdir; //will fail if there's stuff in the directory
	ops->rename   = nufs_rename; //move
	ops->chmod    = nufs_chmod; //changes the mode / permissions. Can't do this from one type to another (probably, might be worth trying)
	ops->truncate = nufs_truncate; //changes length of file
	ops->open     = nufs_open; //does nothing in FUSE. Just check whether file exists, and if it doesnt do an error message
	ops->read     = nufs_read;
	ops->write    = nufs_write;
	ops->utimens  = nufs_utimens; //adjusts time stamps for a file, 3 time metadata fields, change 0 or more of them
	ops->link     = nufs_link;
};

struct fuse_operations nufs_ops;

int
main(int argc, char *argv[])
{
	printf("main\n");
	assert(argc > 2 && argc < 6);
	storage_init(argv[--argc]);
	nufs_init_ops(&nufs_ops);
	return fuse_main(argc, argv, &nufs_ops, NULL);
}

