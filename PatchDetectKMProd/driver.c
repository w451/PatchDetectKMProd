#include <ntddk.h>
#include "driver.h"
#include "structas.h"
#include "memory.h"
#include "encryption.h"


#define NT_DEVICE_NAME L"\\Device\\JDPatchDetect"
#define DOS_DEVICE_NAME L"\\DosDevices\\PatchDetector"

#define READ_PHYS CTL_CODE(0x8069, 0x869, METHOD_BUFFERED, FILE_READ_DATA | FILE_WRITE_DATA)
#define QUERY_PFN_DB CTL_CODE(0x806a, 0x86a, METHOD_BUFFERED, FILE_READ_DATA | FILE_WRITE_DATA)
#define QUERY_VIRT_PAGE CTL_CODE(0x806b, 0x86b, METHOD_BUFFERED, FILE_READ_DATA | FILE_WRITE_DATA)

PDRIVER_DISPATCH dispatches[2];

MMPFN* MmPfnDatabase;

NTSTATUS NTAPI DriverEntry(PDRIVER_OBJECT DriverObject, PUNICODE_STRING RegistryPath) {
	UNREFERENCED_PARAMETER(RegistryPath);
    NTSTATUS ntStatus;
	UNICODE_STRING ntUnicodeString;
	UNICODE_STRING ntWin32NameString;
	PDEVICE_OBJECT deviceObject = NULL;

	RtlInitUnicodeString(&ntUnicodeString, NT_DEVICE_NAME);
	RtlInitUnicodeString(&ntWin32NameString, DOS_DEVICE_NAME);
	MmPfnDatabase = (MMPFN*)((*(ULONG64*)(((ULONG64)&MmGetVirtualForPhysical + 0x10))) & ~0xf);

    ntStatus = IoCreateDevice(DriverObject,0,&ntUnicodeString,FILE_DEVICE_UNKNOWN,FILE_DEVICE_SECURE_OPEN,FALSE,&deviceObject);

	if (!NT_SUCCESS(ntStatus))
	{
		DbgPrintEx(0,0,"Couldn't create the device object\n");
		return ntStatus;
	}

	dispatches[0] = &createOrClose;
	dispatches[1] = &deviceControlInternal;

	DriverObject->MajorFunction[IRP_MJ_DEVICE_CONTROL] = deviceControl;
	DriverObject->MajorFunction[IRP_MJ_CREATE] = createOrClose;
	DriverObject->MajorFunction[IRP_MJ_CLOSE] = createOrClose;
	DriverObject->DriverUnload = driverUnload;

	ntStatus = IoCreateSymbolicLink(
		&ntWin32NameString, &ntUnicodeString);

	if (!NT_SUCCESS(ntStatus))
	{
		DbgPrintEx(0,0,"Couldn't create symbolic link\n");
		IoDeleteDevice(deviceObject);
	} else {
		DbgPrintEx(0, 0, "Success\n");
	}

	return ntStatus;
}

NTSTATUS deviceControl(PDEVICE_OBJECT DeviceObject, PIRP Irp) {
	PIO_STACK_LOCATION  irpSp = IoGetCurrentIrpStackLocation(Irp);
	ULONG64* sb = (ULONG64*)Irp->AssociatedIrp.SystemBuffer;
	
	return dispatches[irpSp->Parameters.DeviceIoControl.InputBufferLength >= 8 && *sb == 0xab6eea1a379cec1a](DeviceObject, Irp); //Check that our application is calling
}

NTSTATUS deviceControlInternal(PDEVICE_OBJECT DeviceObject, PIRP Irp) {
	UNREFERENCED_PARAMETER(DeviceObject);
	PIO_STACK_LOCATION  irpSp = IoGetCurrentIrpStackLocation(Irp);
	NTSTATUS ntStatus = STATUS_SUCCESS;

	void* ib = (PVOID)(((ULONG64)Irp->AssociatedIrp.SystemBuffer)+8);
	void* ob = Irp->AssociatedIrp.SystemBuffer;

	ULONG inLen = irpSp->Parameters.DeviceIoControl.InputBufferLength-8;
	ULONG outLen = irpSp->Parameters.DeviceIoControl.OutputBufferLength;

	substituteCipher(ib, inLen);

	if (!ob)
	{
		ntStatus = STATUS_INVALID_PARAMETER;
		goto End;
	}

	ULONG cc = irpSp->Parameters.DeviceIoControl.IoControlCode;

	if (cc == READ_PHYS) {
		if (inLen < 8 || outLen < 1) {
			ntStatus = STATUS_INVALID_PARAMETER;
		} else {
			ULONG64 n = 0;
			ntStatus = ReadPhysicalAddress((PVOID) * (ULONG64*)ib, ob, outLen, &n);
			Irp->IoStatus.Information = n;
		}
	} else if (cc == QUERY_PFN_DB) {
		if (inLen < 8 || outLen < sizeof(MMPFN)) {
			ntStatus = STATUS_INVALID_PARAMETER;
		} else {
			__try {
				ULONG64 n = 0;
				ntStatus = MmCopyVirtualMemory(IoGetCurrentProcess(), &MmPfnDatabase[*(ULONG64*)ib], IoGetCurrentProcess(), ob, outLen, KernelMode, &n);
				Irp->IoStatus.Information = n;
			}
			__except (EXCEPTION_EXECUTE_HANDLER) {}
		}
	} else if (cc == QUERY_VIRT_PAGE) {
		if (inLen < 16 || outLen < 8) {
			ntStatus = STATUS_INVALID_PARAMETER;
		} else {
			char valid = 0;
			ULONG64 qp = queryPage(((ULONG64*)ib)[0], ((ULONG64*)ib)[1], &valid);
			if (valid) {
				*(ULONG64*)ob = qp;
				Irp->IoStatus.Information = 8;
			} else {
				ntStatus = STATUS_INVALID_PARAMETER;
			}
		}
	}

End:
	substituteCipher(ob, Irp->IoStatus.Information);
	Irp->IoStatus.Status = ntStatus;
	IofCompleteRequest(Irp, IO_NO_INCREMENT);
	return ntStatus;
}

NTSTATUS createOrClose(PDEVICE_OBJECT DeviceObject, PIRP Irp) {
	UNREFERENCED_PARAMETER(DeviceObject);
	PAGED_CODE();
	Irp->IoStatus.Status = STATUS_SUCCESS;
	Irp->IoStatus.Information = 0;
	IofCompleteRequest(Irp, IO_NO_INCREMENT);
	return STATUS_SUCCESS;
}

void driverUnload(PDRIVER_OBJECT DriverObject) {
	PDEVICE_OBJECT deviceObject = DriverObject->DeviceObject;
	UNICODE_STRING uniWin32NameString;
	RtlInitUnicodeString(&uniWin32NameString, DOS_DEVICE_NAME);
	IoDeleteSymbolicLink(&uniWin32NameString);
	if (deviceObject != NULL)
	{
		IoDeleteDevice(deviceObject);
	}
	DbgPrintEx(0, 0, "Goodbye!\n");
}