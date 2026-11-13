#pragma once
#include <stdio.h>

#define LOG(...)                                 \
	do {                                     \
		fprintf(stderr, "vesper: ");     \
		fprintf(stderr, __VA_ARGS__);    \
		fputc('\n', stderr);             \
	} while (0)
