#include "geometry.hpp"
#include <cstdio>
using namespace boayo;
int main(){Geometry g;puts("#include <stdint.h>\nstatic const uint32_t initial_camera[180]={");for(auto v:g.camera())printf("%uu,",unsigned(v));puts("};\nstatic const uint32_t initial_palette[256]={");for(unsigned i=0;i<256;i++)printf("%uu,",palette(i));puts("};");}
