#include <wdm.h>

#define IOCTL_START CTL_CODE(FILE_DEVICE_UNKNOWN, 0x800, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_STOP  CTL_CODE(FILE_DEVICE_UNKNOWN, 0x801, METHOD_BUFFERED, FILE_ANY_ACCESS)

#define DEV_NAME L"\\Device\\MyDriver"
#define SYM_NAME L"\\DosDevices\\MyDriver"

PVOID pThread=NULL;
volatile BOOLEAN bExit=FALSE;
PDEVICE_OBJECT pNextDevice=NULL;

VOID MyThread(PVOID pParam)
{
  LARGE_INTEGER stTime;

  stTime.HighPart|= -1;
  stTime.LowPart = -10000000;
  DbgPrint("Current process: %s", (char*)((ULONG)IoGetCurrentProcess() + 0x174));
  while(bExit != TRUE){
      KeDelayExecutionThread(KernelMode, FALSE, &stTime);
      DbgPrint("Sleep 1s");
  }
  DbgPrint("Exit MyThread");
  PsTerminateSystemThread(STATUS_SUCCESS);
}

NTSTATUS AddDevice(PDRIVER_OBJECT pOurDriver, PDEVICE_OBJECT pPhyDevice)
{
  PDEVICE_OBJECT pOurDevice=NULL;
  UNICODE_STRING usDeviceName;
  UNICODE_STRING usSymboName;

  RtlInitUnicodeString(&usDeviceName, DEV_NAME);
  IoCreateDevice(pOurDriver, 0, &usDeviceName, FILE_DEVICE_UNKNOWN, 0, FALSE, &pOurDevice);
  RtlInitUnicodeString(&usSymboName, SYM_NAME);
  IoCreateSymbolicLink(&usSymboName, &usDeviceName);
  pNextDevice = IoAttachDeviceToDeviceStack(pOurDevice, pPhyDevice);
  pOurDevice->Flags&= ~DO_DEVICE_INITIALIZING;
  pOurDevice->Flags|= DO_BUFFERED_IO;
  return STATUS_SUCCESS;
}

void Unload(PDRIVER_OBJECT pOurDriver)
{
  pOurDriver = pOurDriver;
}

NTSTATUS IrpPnp(PDEVICE_OBJECT pOurDevice, PIRP pIrp)
{
  PIO_STACK_LOCATION psk = IoGetCurrentIrpStackLocation(pIrp);
  UNICODE_STRING usSymboName;

  if(psk->MinorFunction == IRP_MN_REMOVE_DEVICE){
    RtlInitUnicodeString(&usSymboName, SYM_NAME);
    IoDeleteSymbolicLink(&usSymboName);
    IoDetachDevice(pNextDevice);
    IoDeleteDevice(pOurDevice);
  }
  IoSkipCurrentIrpStackLocation(pIrp);
  return IoCallDriver(pNextDevice, pIrp);
}

NTSTATUS IrpIOCTL(PDEVICE_OBJECT pOurDevice, PIRP pIrp)
{
  HANDLE hThread;
  NTSTATUS status;
  PIO_STACK_LOCATION psk = IoGetCurrentIrpStackLocation(pIrp);

  switch(psk->Parameters.DeviceIoControl.IoControlCode){
  case IOCTL_START:
    DbgPrint("IOCTL_START");
    bExit = FALSE;
    
    // user thread
    status = PsCreateSystemThread(&hThread, THREAD_ALL_ACCESS, NULL, (PHANDLE)-1, NULL, MyThread, (PVOID)pOurDevice);
    
    // system thread
    //status = PsCreateSystemThread(&hThread, THREAD_ALL_ACCESS, NULL, NULL, NULL, MyThread, pOurDevice);
    
    if(status == STATUS_SUCCESS){
      ObReferenceObjectByHandle(hThread, THREAD_ALL_ACCESS, NULL, KernelMode, &pThread, NULL);
      ZwClose(hThread);
    }
    break;
  case IOCTL_STOP:
    DbgPrint("IOCTL_STOP");
    bExit = TRUE;
    if(pThread != NULL){
      KeWaitForSingleObject(pThread, Executive, KernelMode, FALSE, NULL);
      ObDereferenceObject(pThread);
    }
    break;
  }
  pIrp->IoStatus.Information = 0;
  pIrp->IoStatus.Status = STATUS_SUCCESS;
  IoCompleteRequest(pIrp, IO_NO_INCREMENT);
  return STATUS_SUCCESS;
}

NTSTATUS IrpFile(PDEVICE_OBJECT pOurDevice, PIRP pIrp)
{
  PIO_STACK_LOCATION psk = IoGetCurrentIrpStackLocation(pIrp);

  switch(psk->MajorFunction){
  case IRP_MJ_CREATE:
    DbgPrint("IRP_MJ_CREATE");
    break;
  case IRP_MJ_CLOSE:
    DbgPrint("IRP_MJ_CLOSE");
    break;
  }
  IoCompleteRequest(pIrp, IO_NO_INCREMENT);
  return STATUS_SUCCESS;
}

NTSTATUS DriverEntry(PDRIVER_OBJECT pOurDriver, PUNICODE_STRING pOurRegistry)
{
  pOurDriver->MajorFunction[IRP_MJ_PNP] = IrpPnp;
  pOurDriver->MajorFunction[IRP_MJ_CREATE] =
  pOurDriver->MajorFunction[IRP_MJ_CLOSE] = IrpFile;
  pOurDriver->MajorFunction[IRP_MJ_DEVICE_CONTROL] = IrpIOCTL;
  pOurDriver->DriverExtension->AddDevice = AddDevice;
  pOurDriver->DriverUnload = Unload;
  return STATUS_SUCCESS;
}
