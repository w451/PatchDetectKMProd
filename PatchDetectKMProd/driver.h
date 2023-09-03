#include <ntddk.h>

void driverUnload(PDRIVER_OBJECT DriverObject);
NTSTATUS deviceControl(PDEVICE_OBJECT DeviceObject, PIRP Irp);
NTSTATUS createOrClose(PDEVICE_OBJECT DeviceObject, PIRP Irp);
NTSTATUS deviceControlInternal(PDEVICE_OBJECT DeviceObject, PIRP Irp);