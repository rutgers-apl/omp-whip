#ifndef FILEUTIL_H_
#define FILEUTIL_H_

#include <sys/stat.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <iostream>

int createDir(const char* dirName);
int dirExists(const char *path);

#endif