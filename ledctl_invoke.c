#include <linux/errno.h>
#include <sys/syscall.h>
#include <unistd.h>
#include <stdio.h>

#define __NR_LEDCTL 317

long ledctl(unsigned int mask){
	return (long) syscall(__NR_LEDCTL, mask);
}

int main(int argc, char* argv[]){

	unsigned int mask;
	
	if(argc != 2) {
		printf("Usage: ./ledctl_invoke <ledmask>\n");
		return -1;
	}
	
	sscanf(argv[1], "0x%u", &mask);
	
	return ledctl(mask);
}
