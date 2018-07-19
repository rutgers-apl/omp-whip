#include "fileUtil.h"

int createDir(const char* dirName){
  const int dirErr = mkdir(dirName, S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH);
  if (-1 == dirErr)
  {
    std::cout << "Error creating directory!" << std::endl;
  }
  return dirErr;

}

int dirExists(const char *path){
    struct stat info;
    if(stat( path, &info ) != 0)
        return 0;
    else if(info.st_mode & S_IFDIR)
        return 1;
    else
        return 0;
}
