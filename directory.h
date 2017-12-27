#ifndef DIRECTORY_H
#define DIRECTORY_H

#define DIR_SIZE 64
#define DIR_NAME 48

#include "slist.h"
#include "pages.h"

const static int NUM_DIRENTS = 39;

typedef struct dirent {
	char name[100];
	int inum;
} dirent;

typedef struct directory {
	int inum;
	int num_dirents;
} directory;

int directory_put_ent(directory* dd, const char* name, int pnum);

#endif

