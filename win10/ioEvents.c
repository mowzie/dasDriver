#include <ntddk.h>
#include <wdf.h>
#include "driver.h"

VOID
DasEvtIoRead(
    IN WDFQUEUE Queue,
    IN WDFREQUEST Request,
    IN size_t Length
)
{
    UNREFERENCED_PARAMETER(Length);
    PDAS_CONTEXT   devExt;

    devExt = DasGetData(WdfIoQueueGetDevice(Queue));
    KdPrint(("length: %d\n", Length));

    if (bufempty(devExt)) {
        if (devExt->Flags & DAS_ISSAMPLING) {
            devExt->Flags |= DAS_NEEDSREAD;
            devExt->Request = Request;
            KdPrint(("waiting to read request 0x%p\n", Request));
            return;
        }
        else {
            //case 1:  buffer is empty
                KdPrint(("not printing anything, buffer empty\n"));
                WdfRequestCompleteWithInformation(Request, STATUS_END_OF_FILE, 0L);
                return;
        }
    }
    read2(Request, devExt, (UINT16)Length);

}

VOID read2(
    IN WDFREQUEST Request,
    IN PDAS_CONTEXT devExt,
    IN UINT16 Length
) {
    KdPrint(("-->read"));
    WDFMEMORY memory;
    UINT16 wrapcount = 0;
    NTSTATUS status = STATUS_SUCCESS;
    if (Length == 0) {
        WdfRequestCompleteWithInformation(Request, STATUS_END_OF_FILE, 0L);
        return;
    }
    status = WdfRequestRetrieveOutputMemory(Request, &memory);
    if (!NT_SUCCESS(status)) {
        KdPrint(("DasEvtIoRead Could not get request memory buffer 0x%x\n", status));
        WdfRequestCompleteWithInformation(Request, status, 0L);
        return;
    }
    
    KdPrint(("before mod Length: %d\n", Length));
    //require requests to be multiples of 4
	//also changes to samples instead of bytes
    Length = Length>>2;
    KdPrint(("readptr: %d  writeptr: %d", devExt->readptr, devExt->writeptr));
    KdPrint(("length: %d\n", Length));

    UINT16 count = bufsize(devExt);
    UINT16 samplesRead = 0;
    if (count < Length) {
        Length = count;
    }
    //case 2:  read pointer is behind write pointer
    if (devExt->writeptr >= devExt->readptr) {

        status = WdfMemoryCopyFromBuffer(memory, 0, (UINT32 *)devExt->buff + devExt->readptr, Length * 4);
        if (!NT_SUCCESS(status)) {
            KdPrint(("EchoEvtIoRead: WdfMemoryCopyFromBuffer failed 0x%x\n", status));
            WdfRequestComplete(Request, status);
            return;
        }
        samplesRead += Length;
        devExt->readptr = bufmask(devExt->readptr + Length);
    }
    //case 3:  read pointer is ahead of write pointer (need to wrap)
    else {
        wrapcount = bufmask(bufsize(devExt) - devExt->readptr);
        KdPrint(("wrapcount: %d\n", wrapcount));
        KdPrint(("Length - wrapcount: %d\n", Length - wrapcount));
        if (wrapcount > Length) {
            wrapcount = Length;
        }
        KdPrint(("modified wrap: %d\n", wrapcount));
        if (wrapcount != 0) {
            status = WdfMemoryCopyFromBuffer(memory, 0, (UINT32 *)devExt->buff + devExt->readptr, wrapcount * 4);
            if (!NT_SUCCESS(status)) {
                KdPrint(("<<EchoEvtIoRead: WdfMemoryCopyFromBuffer failed 0x%x\n", status));
                WdfRequestComplete(Request, status);
                return;
            }
        }
        samplesRead += wrapcount;
        devExt->readptr = bufmask(devExt->readptr + wrapcount);
        if (wrapcount != Length) {
            KdPrint(("rest: %d\n", Length - wrapcount));
            status = WdfMemoryCopyFromBuffer(memory, wrapcount * 4, (UINT32 *)devExt->buff + devExt->readptr, (Length - wrapcount) * 4);
            if (!NT_SUCCESS(status)) {
                KdPrint((">>EchoEvtIoRead: WdfMemoryCopyFromBuffer failed 0x%x\n", status));
                WdfRequestComplete(Request, status);
                return;
            }
            samplesRead = Length;
            devExt->readptr = bufmask(devExt->readptr + Length-wrapcount);
        }
    }
    KdPrint(("samples read: %d\n", samplesRead));
    KdPrint(("<--read"));
    WdfRequestCompleteWithInformation(Request, status, samplesRead *4);
}


VOID
DasEvtIoWrite(
    IN WDFQUEUE Queue,
    IN WDFREQUEST Request,
    IN size_t Length
)
{
    UNREFERENCED_PARAMETER(Length);
    UNREFERENCED_PARAMETER(Queue);
    NTSTATUS status = STATUS_NOT_SUPPORTED;
    WdfRequestCompleteWithInformation(Request, status, 0);
}

VOID
DasEvtIoDeviceControl(
    IN WDFQUEUE Queue,
    IN WDFREQUEST Request,
    IN size_t OutputBufferLength,
    IN size_t InputBufferLength,
    IN ULONG IoControlCode
)
{
    PDAS_CONTEXT	devExt;
    PVOID buff;
    //	size_t bufLength;
    size_t bytesReturned = 0;
    devExt = DasGetData(WdfIoQueueGetDevice(Queue));
    UNREFERENCED_PARAMETER(InputBufferLength);
    UNREFERENCED_PARAMETER(OutputBufferLength);

    NTSTATUS status = STATUS_SUCCESS;
    switch (IoControlCode) {
    case IOCTL_DAS_START_SAMPLING:
        KdPrint(("--> start sampling\n"));
        if (devExt->Flags & ~DAS_ISSAMPLING) {
            devExt->Flags |= DAS_ISSAMPLING;
            devExt->lights = 1;
            //start clock
            WRITE_PORT_UCHAR(devExt->Badr2 + COUNTER_CONTROL, COUNTER_RESET);
            WRITE_PORT_UCHAR(devExt->Badr2 + COUNTER2, (UINT8)(devExt->counter & 0x00FF));
            WRITE_PORT_UCHAR(devExt->Badr2 + COUNTER2, (UINT8)(devExt->counter >> 8));
            //turn on interripts and setup OP1
            DasInterruptEnable(devExt);
            //start conversion
            WRITE_PORT_UCHAR(devExt->Badr2 + DAS_DATA2, 1);
            //latch value
            WRITE_PORT_UCHAR(devExt->Badr2 + COUNTER_CONTROL, COUNTER_LATCH);
            UINT8 lsb = READ_PORT_UCHAR(devExt->Badr2 + COUNTER2);
            UINT8 msb = READ_PORT_UCHAR(devExt->Badr2 + COUNTER2);
            devExt->old_clock = msb << 8 | lsb;
            KdPrint(("<-- start sampling\n"));
        }
        break;

    case IOCTL_DAS_STOP_SAMPLING:
        KdPrint(("--> stop sampling\n"));
        if (devExt->Flags & DAS_ISSAMPLING) {		
            UINT8 dlsb = READ_PORT_UCHAR(devExt->Badr2 + DAS_DATA1);
            UINT8 dmsb = READ_PORT_UCHAR(devExt->Badr2 + DAS_DATA2);
            UINT32 sample = (devExt->counter - devExt->old_clock) << 16 | dmsb << 4 | dlsb >> 4;
            bufwrite(sample, devExt);
            KdPrint(("sample: %d\n", sample));
            DasInterruptDisable(devExt);
        }
        KdPrint(("<-- stop sampling\n"));
        break;

    case IOCTL_DAS_SET_RATE:
        KdPrint(("-->set rate\n"));
        if (devExt->Flags & (~DAS_ISSAMPLING)) {
            status = WdfRequestRetrieveInputBuffer(Request, 0, &buff, &bytesReturned);
            if (!NT_SUCCESS(status)) {
                KdPrint(("WdfRequestRetrieveInputBuffer fail\n"));
                WdfRequestComplete(Request, status);
                break;
            }
            UINT16 rate;
            RtlCopyMemory(&rate, buff, sizeof(UINT16));

            if ((rate < 1) || (rate > 1588)) {
                bytesReturned = 0;
            } else {
                devExt->rate = rate;
                devExt->counter = RATE_TO_COUNTER(rate);
            }
        }
        KdPrint(("<--rate\n"));
        break;

    case IOCTL_DAS_SET_CHANNEL:
        KdPrint(("-->set channel\n"));
        if (devExt->Flags & (~DAS_ISSAMPLING)) {
            status = WdfRequestRetrieveInputBuffer(Request, 0, &buff, &bytesReturned);
            if (!NT_SUCCESS(status)) {
                KdPrint(("WdfRequestRetrieveInputBuffer fail\n"));
                WdfRequestComplete(Request, status);
                break;
            }
            UINT8 chan;
            RtlCopyMemory(&chan, buff, sizeof(UINT8));
            if ((chan < 0) || (chan > 7)) {
                bytesReturned = 0;
            }
            else {
                devExt->channel = chan;
            }
        }
        KdPrint(("<--channel\n"));
        break;

    case IOCTL_DAS_GET_CHANNEL:
        KdPrint(("das: get channel\n"));
        status = WdfRequestRetrieveOutputBuffer(Request, 0, &buff, &bytesReturned);
        if (!NT_SUCCESS(status)) {
            KdPrint(("WdfRequestRetrieveOutputBuffer fail\n"));
            WdfRequestComplete(Request, status);
            break;
        }
        RtlZeroMemory(buff, bytesReturned);
        RtlCopyMemory(buff, &(devExt->channel), sizeof(devExt->channel));
        break;

    case IOCTL_DAS_GET_RATE:
        KdPrint(("--> get rate\n"));
        status = WdfRequestRetrieveOutputBuffer(Request, 0, &buff, &bytesReturned);
        if (!NT_SUCCESS(status)) {
            KdPrint(("WdfRequestRetrieveOutputBuffer fail\n"));
            WdfRequestComplete(Request, status);
            break;
        }
        RtlZeroMemory(buff, bytesReturned);
        RtlCopyMemory(buff, &(devExt->rate), sizeof(UINT16));
        KdPrint(("<-- get rate"));
        break;

    default:
        KdPrint(("bad ioctl\n"));
        status = STATUS_INVALID_DEVICE_REQUEST;
    }
    WdfRequestCompleteWithInformation(Request, status, bytesReturned);
}



//helper functions for the ring buffer

UINT16 bufsize(PDAS_CONTEXT devExt) {
    return (bufmask(devExt->writeptr - devExt->readptr));
}
BOOLEAN bufempty(PDAS_CONTEXT devExt) {
    return (devExt->readptr == devExt->writeptr);
}
VOID bufwrite(UINT32 val, PDAS_CONTEXT devExt) {
    UINT16 next = bufmask(devExt->writeptr + 1);
    //buffer is full, so move the read pointer
    //this drops old data in preference to new data
    if (next == devExt->readptr)
        devExt->readptr = bufmask(next + 1);
    devExt->buff[devExt->writeptr] = val;
    devExt->writeptr = next;
}
UINT16 bufmask(UINT16 val) {
    return val & (BUFF_SIZE - 1);
}

