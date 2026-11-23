#include "types.h"
#include "main.h"
static SYS_WORK system_work;
void check_particle_runtime(const char *,const char *);
int main(int argc,char **argv) { if(argc!=3)return 2;sys=&system_work;check_particle_runtime(argv[1],argv[2]);return 0; }
