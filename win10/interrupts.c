#include <ntddk.h>
#include <wdf.h>
#include "driver.h"

NTSTATUS
DasInterruptCreate(
    IN PDAS_CONTEXT DevExt
)
{
    KdPrint(("-->InterruptCreate"));
    NTSTATUS status = STATUS_SUCCESS;
    WDF_INTERRUPT_CONFIG	InterruptConfig;

    WDF_INTERRUPT_CONFIG_INIT(&InterruptConfig, DasEvtInterruptIsr, DasEvtInterruptDpc);
//	InterruptConfig.EvtInterruptEnable = DasEvtInterruptEnable;
//	InterruptConfig.EvtInterruptDisable = DasEvtInterruptDisable;
//	InterruptConfig.AutomaticSerialization = TRUE;

    status = WdfInterruptCreate(DevExt->Device, &InterruptConfig, WDF_NO_OBJECT_ATTRIBUTES, &DevExt->Interrupt);
    if (!NT_SUCCESS(status)) {
        KdPrint(("Failed to create interrupt\n"));
    }
    KdPrint(("<--InterruptCreate"));
    return status;
}


VOID DasInterruptEnable(
    IN PDAS_CONTEXT devExt
)
{
    devExt->Flags |= DAS_ISSAMPLING;
    WRITE_PORT_UCHAR(devExt->Badr2 + DAS_STATUS, (devExt->lights << 4) | 1 << 3 | devExt->channel);
}

VOID DasInterruptDisable(
    IN PDAS_CONTEXT devExt
)
{
    WRITE_PORT_UCHAR(devExt->Badr2 + DAS_STATUS, devExt->channel);
    devExt->Flags &= ~(DAS_ISSAMPLING);
}


BOOLEAN
DasEvtInterruptIsr(
    IN WDFINTERRUPT Interrupt,
    IN ULONG MessageID
)
{
    BOOLEAN InterruptRecognized = FALSE;
    PDAS_CONTEXT devExt;
    UINT8 IntStatus;

    UNREFERENCED_PARAMETER(MessageID);

    devExt = DasGetData(WdfInterruptGetDevice(Interrupt));
    IntStatus = READ_PORT_UCHAR(devExt->Badr2 + DAS_STATUS);
    if(!(IntStatus & (1<<3))) { 
        InterruptRecognized = FALSE;
    }
    else if (IntStatus & (1 << 7)) {
        InterruptRecognized = FALSE;
    }
    else if (devExt->Flags & DAS_ISSAMPLING) {
    //	KdPrint(("interrupt ISR\n"));
        //read conversion data
        UINT8 dlsb = READ_PORT_UCHAR(devExt->Badr2 + DAS_DATA1);
        UINT8 dmsb = READ_PORT_UCHAR(devExt->Badr2 + DAS_DATA2);
        //start next conversion
        WRITE_PORT_UCHAR(devExt->Badr2 + DAS_STATUS, devExt->lights << 4 | 1 << 3 | devExt->channel);
        WRITE_PORT_UCHAR(devExt->Badr2 + DAS_DATA2, 1);

        //latch clock and read value
        WRITE_PORT_UCHAR(devExt->Badr2 + COUNTER_CONTROL, COUNTER_LATCH);
        UINT8 lsb = READ_PORT_UCHAR(devExt->Badr2 + COUNTER2);
        UINT8 msb = READ_PORT_UCHAR(devExt->Badr2 + COUNTER2);
        UINT32 sample = DELTA_T(devExt->counter, devExt->old_clock) << 16 | dmsb << 4 | dlsb >> 4;
        devExt->old_clock = (msb << 8) | lsb;
        bufwrite(sample, devExt);
        KdPrint(("sample: [%d:0x%x]\n", sample >> 16, sample & 0xffff));
        //WdfRequestCompleteWithInformation()
        InterruptRecognized = TRUE;
        //if request is not null
        if (devExt->Flags & DAS_NEEDSREAD) {
            WdfInterruptQueueDpcForIsr(Interrupt);
        }
    }
    return InterruptRecognized;
}

VOID
DasEvtInterruptDpc(
    IN WDFINTERRUPT Interrupt,
    IN WDFOBJECT Device
)
{
    UNREFERENCED_PARAMETER(Interrupt);
    PDAS_CONTEXT devExt;
    devExt = DasGetData(Device);
    WDF_REQUEST_PARAMETERS param;

    WdfRequestGetParameters(devExt->Request, &param);
    read2(devExt->Request, devExt, (UINT16)param.Parameters.Read.Length);
    KdPrint(("interrupt - dpc\n"));
    devExt->Request = NULL;
    devExt->Flags &= ~DAS_NEEDSREAD;
    return;
}