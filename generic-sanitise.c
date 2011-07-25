#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <limits.h>
#include <string.h>

#include "files.h"
#include "arch.h"
#include "trinity.h"
#include "sanitise.h"


char * filebuffer = NULL;
unsigned long filebuffersize = 0;

unsigned long get_interesting_32bit_value()
{
	int i;

	i = rand() % 10;

	if (i > 3) {
		/* common case, return small values*/
		i = rand() % 6;

		switch (i) {
		case 0:	return 0x00000001;
		case 1:	return 0x00000fff;	// 4095
		case 2:	return 0x00001000;	// 4096
		case 3:	return 0x00001001;	// 4097
		case 4:	return 0x00008000;
		case 5:	return 0x0000ffff;
		}

	} else {
		/* less common case, go crazy */
		i = rand() % 9;

		switch (i) {
		case 0:	return 0x00010000;
		case 1:	return 0x7fffffff;
		case 2:	return 0x80000000;
		case 3:	return 0x80000001;
		case 4:	return 0x8fffffff;
		case 5:	return 0xf0000000;
		case 6:	return 0xff000000;
		case 7:	return 0xffffff00 | (rand() % 256);
		case 8:	return 0xffffffff;
		}
	}

	/* Should never be reached. */
	return 0;
}

unsigned long get_interesting_value()
{

/* /usr/include/bits/wordsize.h */
#if __WORDSIZE == 32
#pragma message("__WORDSIZE == 32")
	return get_interesting_32bit_value();
#else
#pragma message("__WORDSIZE == 64")
	int i;
	unsigned long low;

	low = get_interesting_32bit_value();

	i = rand() % 10;
	if (i > 3)
		/* return small values 70% of the time */
		return low;


	i = rand() % 13;

	switch (i) {
	/* 64 bit */
	case 0:	return 0;
	case 1:	return 0x0000000100000000;
	case 2:	return 0x7fffffff00000000;
	case 3:	return 0x8000000000000000;
	case 4:	return 0xffffffff00000000;
	case 5:	return low;
	case 6:	return low | 0x0000000100000000;
	case 7:	return low | 0x7fffffff00000000;
	case 8:	return low | 0x8000000000000000;
	case 9:	return low | 0xffffffff00000000;
	case 10: return (low & 0xffffff) | 0xffffffff81000000;	// x86-64 kernel text address
	case 11: return (low & 0xffffff) | 0xffffffffa0000000;	// x86-64 module space
	case 12: return (low & 0x0fffff) | 0xffffffffff600000;	// x86-64 vdso
	}
	/* Should never be reached. */
	return 0;
#endif
}

void * get_address()
{
	int i;

	i = rand() % 7;
	switch (i) {
	case 0:	return (void *) KERNEL_ADDR;
	case 1:	return page_zeros;
	case 2:	return page_0xff;
	case 3:	return page_rand;
	case 4: return page_allocs;
	case 5:	return (void *) get_interesting_value();
	case 6: return get_map();
	}

	return 0;
}

void regenerate_random_page()
{
	unsigned int i, j;
	void *addr;

	/* sometimes return a page of complete trash */
	if ((rand() % 100) < 50) {
		unsigned int type = rand() % 3;

		switch (type) {
		case 0:	/* bytes */
			for (i = 0; i < page_size; i++)
				page_rand[i++] = (unsigned char)rand();
			return;

		case 1:	/* ints */
			for (i = 0; i < (page_size / 2); i++) {
				page_rand[i++] = 0;
				page_rand[i++] = (unsigned char)rand();
			}
			return;

		case 2:	/* longs */
			for (i = 0; i < (page_size / 4); i++) {
				page_rand[i++] = 0;
				page_rand[i++] = 0;
				page_rand[i++] = 0;
				page_rand[i++] = (unsigned char)rand();
			}
			return;
		}
	}

	/* sometimes return a page that looks kinda like a struct */
	for (i = 0; i < page_size; i++) {
		j = rand() % 4;
		switch (j) {
		case 0: page_rand[i] = get_interesting_32bit_value();
			i += sizeof(unsigned long);
			break;
		case 1: page_rand[i] = get_interesting_value();
			i += sizeof(unsigned long long);
			break;
		case 2: addr = get_address();
			page_rand[i] = (unsigned long) addr;
			i += sizeof(unsigned long);
			break;
		case 3: page_rand[i] = (unsigned int) rand() % page_size;
			i += sizeof(unsigned int);
			break;
		}
	}
}

static unsigned int get_pid()
{
	int i;
	i = rand() % 2;

	switch (i) {
	case 0:	return getpid();
	case 1:	return rand() & 32768;
	case 2: break;
	}
	return 0;
}


static unsigned long fill_arg(int call, int argnum)
{
	int fd;
	unsigned long i;
	unsigned int bits;
	unsigned long mask=0;
	unsigned long low=0, high=0;
	unsigned int num=0;
	unsigned int *values=NULL;
	unsigned int argtype=0;

	switch (argnum) {
	case 1:	argtype = syscalls[call].entry->arg1type;
		break;
	case 2:	argtype = syscalls[call].entry->arg2type;
		break;
	case 3:	argtype = syscalls[call].entry->arg3type;
		break;
	case 4:	argtype = syscalls[call].entry->arg4type;
		break;
	case 5:	argtype = syscalls[call].entry->arg5type;
		break;
	case 6:	argtype = syscalls[call].entry->arg6type;
		break;
	}

	switch (argtype) {
	case ARG_FD:
		fd = get_random_fd();
		//printf (YELLOW "DBG: %x" WHITE "\n", fd);
		return fd;
	case ARG_LEN:
		if ((rand() % 2) == 0)
			return rand() % page_size;
		else
			return get_interesting_value();
	case ARG_ADDRESS:
		return (unsigned long)get_address();
	case ARG_PID:
		return (unsigned long)get_pid();
	case ARG_RANGE:
		switch (argnum) {
		case 1:	low = syscalls[call].entry->low1range;
			high = syscalls[call].entry->hi1range;
			break;
		case 2:	low = syscalls[call].entry->low2range;
			high = syscalls[call].entry->hi2range;
			break;
		case 3:	low = syscalls[call].entry->low3range;
			high = syscalls[call].entry->hi3range;
			break;
		case 4:	low = syscalls[call].entry->low4range;
			high = syscalls[call].entry->hi4range;
			break;
		case 5:	low = syscalls[call].entry->low5range;
			high = syscalls[call].entry->hi5range;
			break;
		case 6:	low = syscalls[call].entry->low6range;
			high = syscalls[call].entry->hi6range;
			break;
		}
		i = rand64() % high;
		if (i < low) {
			i += low;
			i &= high;
		}
		return i;
	case ARG_LIST:
		switch (argnum) {
		case 1:	num = syscalls[call].entry->arg1list.num;
			values = syscalls[call].entry->arg1list.values;
			break;
		case 2:	num = syscalls[call].entry->arg2list.num;
			values = syscalls[call].entry->arg2list.values;
			break;
		case 3:	num = syscalls[call].entry->arg3list.num;
			values = syscalls[call].entry->arg3list.values;
			break;
		case 4:	num = syscalls[call].entry->arg4list.num;
			values = syscalls[call].entry->arg4list.values;
			break;
		case 5:	num = syscalls[call].entry->arg5list.num;
			values = syscalls[call].entry->arg5list.values;
			break;
		case 6:	num = syscalls[call].entry->arg6list.num;
			values = syscalls[call].entry->arg6list.values;
			break;
		}
		bits = rand() % num;	/* num of bits to OR */
		for (i=0; i<bits; i++)
			mask |= values[rand() % num];
		return mask;

	case ARG_RANDPAGE:
		if ((rand() % 2) == 0)
			return (unsigned long) page_allocs;
		else
			return (unsigned long) page_rand;
	}

	return 0x5a5a5a5a;	/* Should never happen */
}


void generic_sanitise(int call,
	unsigned long *a1,
	unsigned long *a2,
	unsigned long *a3,
	unsigned long *a4,
	unsigned long *a5,
	unsigned long *a6)
{
	if (syscalls[call].entry->arg1type != 0)
		*a1 = fill_arg(call, 1);
	if (syscalls[call].entry->arg2type != 0)
		*a2 = fill_arg(call, 2);
	if (syscalls[call].entry->arg3type != 0)
		*a3 = fill_arg(call, 3);
	if (syscalls[call].entry->arg4type != 0)
		*a4 = fill_arg(call, 4);
	if (syscalls[call].entry->arg5type != 0)
		*a5 = fill_arg(call, 5);
	if (syscalls[call].entry->arg6type != 0)
		*a6 = fill_arg(call, 6);
}

