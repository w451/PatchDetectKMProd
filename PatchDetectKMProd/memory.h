#include <ntddk.h>
#include <intrin.h>
#include "structas.h"

NTSTATUS NTAPI MmCopyVirtualMemory(PEPROCESS SourceProcess,PVOID SourceAddress,PEPROCESS TargetProcess,PVOID TargetAddress,SIZE_T BufferSize,KPROCESSOR_MODE PreviousMode,PSIZE_T ReturnSize);
NTSTATUS PsLookupProcessByProcessId(HANDLE ProcessId, PEPROCESS* Process);

NTSTATUS ReadPhysicalAddress(PVOID physAddress, PVOID lpBuffer, SIZE_T Size, SIZE_T* BytesRead)
{
	MM_COPY_ADDRESS AddrToRead = { 0 };
	AddrToRead.PhysicalAddress.QuadPart = (LONGLONG)physAddress;
	return MmCopyMemory(lpBuffer, AddrToRead, Size, MM_COPY_MEMORY_PHYSICAL, BytesRead);
}

#define PAGE_OFFSET_SIZE 12
static const ULONG64 PMASK = (~0xfull << 8) & 0xfffffffffull;

ULONG64 getPteValue(ULONG64 directoryTableBase, ULONG64 virtualAddress) {
	directoryTableBase &= ~0xf;
	ULONG64 pte = ((virtualAddress >> 12) & (0x1ffll));
	ULONG64 pt = ((virtualAddress >> 21) & (0x1ffll));
	ULONG64 pd = ((virtualAddress >> 30) & (0x1ffll));
	ULONG64 pdp = ((virtualAddress >> 39) & (0x1ffll));

	SIZE_T readsize = 0;
	ULONG64 pdpe = 0;
	ReadPhysicalAddress((PVOID)(directoryTableBase + 8 * pdp), &pdpe, sizeof(pdpe), &readsize);
	if (~pdpe & 1)
		return 0;

	ULONG64 pde = 0;
	ReadPhysicalAddress((PVOID)((pdpe & PMASK) + 8 * pd), &pde, sizeof(pde), &readsize);
	if (~pde & 1)
		return 0;

	/* 1GB large page, use pde's 12-34 bits */
	if (pde & 0x80)
		return pde;

	ULONG64 pteAddr = 0;
	ReadPhysicalAddress((PVOID)((pde & PMASK) + 8 * pt), &pteAddr, sizeof(pteAddr), &readsize);
	if (~pteAddr & 1)
		return 0;

	/* 2MB large page */
	if (pteAddr & 0x80)
		return pte;

	ULONG64 pteValue = 0;
	ReadPhysicalAddress((PVOID)((pteAddr & PMASK) + 8 * pte), &pteValue, sizeof(pteValue), &readsize);
	return pteValue;
}

ULONG64 PsGetProcessDirectoryTableBase(PEPROCESS proc) {
	return ((ULONG64*)proc)[5];
}

ULONG64 queryPage(ULONG64 pid, ULONG64 virtualAddress, char* valid) {
	PEPROCESS proc = 0;
	PsLookupProcessByProcessId((HANDLE)pid, &proc);
	if (proc != 0) {
		*valid = 1;
		return getPteValue(PsGetProcessDirectoryTableBase(proc), virtualAddress);
	}
	*valid = 0;
	return 0;
}