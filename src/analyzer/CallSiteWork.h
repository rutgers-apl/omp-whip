#ifndef CALL_SITE_WORK_H
#define CALL_SITE_WORK_H

#include <string>

struct CallSiteWork{
    long work;
    long cWork;
    int height;
};

struct RegionData {
  std::string region_filename;
  int region_line_number;
  
};

#endif