#ifndef INODE_H
#define INODE_H

#include <stdio.h>
#include <sys/stat.h>
#include <stdbool.h>
#include <time.h>

const static int INODE_SIZE = 88;
const static int NUM_BLOCKS = 250;
const static int SIZE_BLOCKS = 4096;

// size of inode is 88
// name for each inode will be in the corresponding block. If there are 
// multiple inodes for one single block, there will be an array of an array of
// characters representing the multiple names.
typedef struct inode {
	int number; // index value of the inode in a list of inodes
	int parent; // index value of parent of this inode, -1 if no parent
	int index; // index value
		
	int num_hard_links; // number of hard links
	int overflow; // index value of inode that controls block it overflows to
			// -1 if no overflow
	mode_t mode; // read, write or execute
	int filetype; // 1 is symlink, 2 is file, 3 is directory; -1 is default
	uid_t userID; // user id
	gid_t groupID; // group id
	bool free; // true if inode is free, false if it is used
	int block_num; // index value of the corresponding block

	size_t name_length; // length of the name -- if block has not been named, -1
	size_t size; // size of file/dir/symlink; -1 if no file
	time_t create_time; // time of creation
	time_t mod_time; // modified time
	time_t access_time; // access time
	
} inode;

#endif