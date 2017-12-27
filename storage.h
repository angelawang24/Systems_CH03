#ifndef NUFS_STORAGE_H
#define NUFS_STORAGE_H

#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>

#include "inode.h"

void storage_init(const char* path);
int         get_stat(const char* path, struct stat* st);
const char* get_data(const char* path);


void* find_corresponding_block(int inum);
inode* get_inode(int inum);
inode* find_inode_within(char* cur_data, inode* cur_node);
inode* find_inode(const char *path);
inode* find_second_to_last_inode(const char *path);

#endif
