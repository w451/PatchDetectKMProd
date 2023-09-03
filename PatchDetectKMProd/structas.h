#pragma once
#include <ntddk.h>

typedef struct _MMPFN {
	char data[0x30];
} MMPFN, * PMMPFN;