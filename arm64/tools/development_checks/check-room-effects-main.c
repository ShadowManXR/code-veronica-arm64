#include "types.h"
#include "main.h"
static SYS_WORK system_work;
void check_room_effects_file(const char *);
int main(int argc,char **argv) { if(argc!=2)return 2;sys=&system_work;check_room_effects_file(argv[1]);return 0; }
