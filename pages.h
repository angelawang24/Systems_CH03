#ifndef PAGES_H
#define PAGES_H

#include <stdio.h>
#include "inode.h"

void   pages_init(const char* path);
void   pages_free();
void*  pages_get_page(int pnum);
void*  get_pages_base();

#endif
