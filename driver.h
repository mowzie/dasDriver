#include <ntddk.h>
#include <wdf.h>
#include "wdasio.h"
#include <initguid.h>

#define BUFF_SIZE (1<<14)

typedef struct _FDO_DATA {
    WDFDEVICE               Device;
    ULONG                   Flags;
    WDFINTERRUPT            Interrupt;
    WDFREQUEST              Request;

    BUS_INTERFACE_STANDARD  BusInterface;
    PUCHAR                  Badr2;

    //device buffer information
    UINT32                  buff[BUFF_SIZE];
    UINT16                  readptr;
    UINT16                  writeptr;

    //current lights & channels
    UINT8                   lights;
    UINT8                   channel;

    //clock variables
    UINT16                  rate;
    UINT16                  counter;
    UINT16                  old_clock;
} DAS_CONTEXT, *PDAS_CONTEXT;

WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(DAS_CONTEXT, DasGetData)
// {CA1E1257-6182-4710-98D9-ECD0A76F002B}
DEFINE_GUID(GUID_das_Interface,
    0xca1e1257, 0x6182, 0x4710, 0x98, 0xd9, 0xec, 0xd0, 0xa7, 0x6f, 0x0, 0x2b);

#define RATE_TO_COUNTER(_rate_) (_rate_ * 165 / 4)
#define COUNTER_TO_RATE(_counter_) ((_counter_) * 40 / 165)
#define DELTA_T(_counter_, _prev_) COUNTER_TO_RATE((_counter_ - _prev_))

#define DAS_DEVICE_NAME L"\\DosDevices\\das1"

#define INTR            0x4c
#define INTRSRC         0x50  
#define INTE			1<<0
#define PCIINT          1<<6
#define BIT0            1<<0
#define BIT1            1<<1
#define OUT0            1<<2

#define DAS_DATA1       0x00
#define DAS_DATA2       0x01
#define DAS_STATUS      0x02

//Clock controls
#define COUNTER_RESET   0xB4
#define COUNTER_LATCH   0x80
#define COUNTER2        0x06
#define COUNTER_CONTROL 0x07

//status flags
#define DAS_ISOPEN     (1<<0)
#define DAS_ISSAMPLING (1<<1)
#define DAS_NEEDSREAD  (1<<2)

//
// Driver setup / teardown
//
DRIVER_INITIALIZE DriverEntry;
EVT_WDF_DRIVER_UNLOAD DasEvtDriverUnload;
EVT_WDF_DRIVER_DEVICE_ADD DasEvtDeviceAdd;
EVT_WDF_DEVICE_FILE_CREATE DasEvtDeviceFileCreate;
EVT_WDF_FILE_CLEANUP DasEvtFileCleanup;
EVT_WDF_FILE_CLOSE DasEvtFileClose;
//
// Io events callbacks.
//
EVT_WDF_IO_QUEUE_IO_READ DasEvtIoRead;
EVT_WDF_IO_QUEUE_IO_WRITE DasEvtIoWrite;
EVT_WDF_IO_QUEUE_IO_DEVICE_CONTROL DasEvtIoDeviceControl;

//
// HW Setup
//
EVT_WDF_DEVICE_PREPARE_HARDWARE DasEvtDevicePrepareHardware;
EVT_WDF_DEVICE_RELEASE_HARDWARE DasEvtDeviceReleaseHardware;

//
//Interrupts
//
NTSTATUS DasInterruptCreate(IN PDAS_CONTEXT devExt);
EVT_WDF_INTERRUPT_ISR DasEvtInterruptIsr;
EVT_WDF_INTERRUPT_DPC DasEvtInterruptDpc;
VOID DasInterruptEnable(IN PDAS_CONTEXT devExt);
VOID DasInterruptDisable(IN PDAS_CONTEXT devExt);

//
//read
//
VOID read2(
    IN WDFREQUEST Request,
    IN PDAS_CONTEXT devExt,
    IN UINT16 Length
);

//ring buffer helpers
UINT16 bufsize(PDAS_CONTEXT devExt);
BOOLEAN bufempty(PDAS_CONTEXT devExt);
VOID bufwrite(UINT32 val, PDAS_CONTEXT devExt);
UINT16 bufmask(UINT16 val);
