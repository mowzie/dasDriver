#include "driver.h"

NTSTATUS
DriverEntry(
    IN PDRIVER_OBJECT DriverObject,
    IN PUNICODE_STRING RegistryPath
)
{
    NTSTATUS status = STATUS_SUCCESS;
    WDF_DRIVER_CONFIG config;
    KdPrint(("das: DASIO Driver is being loaded\n"));

    WDF_DRIVER_CONFIG_INIT(
                            &config,
                            DasEvtDeviceAdd
                            );
    config.EvtDriverUnload = DasEvtDriverUnload;

    status = WdfDriverCreate(
        DriverObject,
        RegistryPath,
        WDF_NO_OBJECT_ATTRIBUTES,
        &config,
        WDF_NO_HANDLE
    );

    if (!NT_SUCCESS(status)) {
        KdPrint(("das: WdfDriverCreate failed with status 0x%x\n", status));
    }
    return status;
}

VOID
DasEvtDriverUnload(
    IN WDFDRIVER Driver
)
{
    UNREFERENCED_PARAMETER(Driver);
    KdPrint(("das: dasEvtDriverUnload called\n"));
}

NTSTATUS
DasEvtDeviceAdd(
    IN WDFDRIVER Driver,
    IN PWDFDEVICE_INIT DeviceInit
)
{
    NTSTATUS                              status = STATUS_SUCCESS;
    WDF_PNPPOWER_EVENT_CALLBACKS pnpPowerCallbacks;
    WDF_OBJECT_ATTRIBUTES                 fdoAttributes;
    WDF_FILEOBJECT_CONFIG                 fileConfig;
    WDFDEVICE                             device;
    WDF_IO_QUEUE_CONFIG                   queueConfig;
    PDAS_CONTEXT                             fdoData;
    WDFQUEUE                              queue;
    DECLARE_CONST_UNICODE_STRING(SymbolicLink, DAS_DEVICE_NAME);
    UNREFERENCED_PARAMETER(Driver);
    KdPrint(("--> DasEvtDeviceAdd\n"));

    //
    // Set Callbacks for any of the functions we are interested in.
    // If no callback is set, Framework will take the default action
    // by itself.
    //
    WDF_PNPPOWER_EVENT_CALLBACKS_INIT(&pnpPowerCallbacks);
    pnpPowerCallbacks.EvtDevicePrepareHardware = DasEvtDevicePrepareHardware;
    pnpPowerCallbacks.EvtDeviceReleaseHardware = DasEvtDeviceReleaseHardware;
    WdfDeviceInitSetPnpPowerEventCallbacks(DeviceInit, &pnpPowerCallbacks);

    WDF_FILEOBJECT_CONFIG_INIT(&fileConfig, DasEvtDeviceFileCreate, DasEvtFileClose, WDF_NO_HANDLE);
    WdfDeviceInitSetFileObjectConfig(DeviceInit, &fileConfig, WDF_NO_OBJECT_ATTRIBUTES);

    WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&fdoAttributes, DAS_CONTEXT);
    fdoAttributes.SynchronizationScope = WdfSynchronizationScopeDevice;

    status = WdfDeviceCreate(&DeviceInit, &fdoAttributes, &device);
    if (!NT_SUCCESS(status)) {
        KdPrint(("das: DeviceCreate failed with status code 0x%x\n", status));
        return status;
    }
    fdoData = DasGetData(device);
    fdoData->Device = device;

    //Create a GUID interface as well as symbolic link to test with
    status = WdfDeviceCreateDeviceInterface(device, (LPGUID)&GUID_das_Interface, NULL);
    status = WdfDeviceCreateSymbolicLink(device, &SymbolicLink);

    //create queue
    //
    WDF_IO_QUEUE_CONFIG_INIT_DEFAULT_QUEUE(&queueConfig, WdfIoQueueDispatchSequential);
    queueConfig.EvtIoRead = DasEvtIoRead;
    queueConfig.EvtIoWrite = DasEvtIoWrite;
    queueConfig.EvtIoDeviceControl = DasEvtIoDeviceControl;

    __analysis_assume(queueConfig.EvtIoStop != 0);
    status = WdfIoQueueCreate(device,
        &queueConfig,
        WDF_NO_OBJECT_ATTRIBUTES,
        &queue
    );
    __analysis_assume(queueConfig.EvtIoStop == 0);
    if (!NT_SUCCESS(status)) {
        KdPrint(("das: WdfIoQueueCreate failed 0x%x\n", status));
        return status;
    }
    //interrupts!
    status = DasInterruptCreate(fdoData);
    KdPrint(("<-- DasEvtDeviceAdd\n"));
    return status;
}

NTSTATUS
DasEvtDevicePrepareHardware(
    IN WDFDEVICE Device,
    IN WDFCMRESLIST ResourcesRaw,
    IN WDFCMRESLIST ResourcesTranslated
)
{
    UNREFERENCED_PARAMETER(ResourcesRaw);
    NTSTATUS status = STATUS_SUCCESS;
    ULONG numBar = 0;
    BOOLEAN foundMem = FALSE;
    PULONG Badr1 = 0;
    ULONG badr1_length = 0;
    PCM_PARTIAL_RESOURCE_DESCRIPTOR desc;
    ULONG i;
    PDAS_CONTEXT devExt;
    KdPrint(("das: Set up device\n"));
    devExt = DasGetData(Device);

    KdPrint(("das: DevicePrepareHardware Device 0x%p ResRaw 0x%p ResTrans "
        "0x%p Count %d\n", Device, ResourcesRaw, ResourcesTranslated,
        WdfCmResourceListGetCount(ResourcesTranslated)));


    for (i = 0; i < WdfCmResourceListGetCount(ResourcesTranslated); i++) {
        desc = WdfCmResourceListGetDescriptor(ResourcesTranslated, i);
        switch (desc->Type)
        {
        case CmResourceTypeMemory:
            //
            // Handle memory resources here.
            //
            //BAR1 is also mapped to memory
            //but we are not memory mapping for this device
            //we can safely ignore this
            if (!foundMem)
                foundMem = TRUE;
            else
                return STATUS_DEVICE_CONFIGURATION_ERROR;
            break;

        case CmResourceTypePort:
            //
            // Handle port resources here.
            //
            //Expecting 2 IO ports: BAR1 and BAR2
            numBar++;
            if (desc->u.Port.Length == 128) {
                Badr1 = (PULONG)desc->u.Port.Start.LowPart;
                badr1_length = desc->u.Port.Length;
            }
            else if (desc->u.Port.Length == 8)
            {
                devExt->Badr2 = (PUCHAR)desc->u.Port.Start.LowPart;
            }
            else {
                KdPrint(("das: more than two BAR's found\n"));
                return STATUS_DEVICE_CONFIGURATION_ERROR;
            }

            break;

        case CmResourceTypeInterrupt:
            KdPrint(("das: interupt: \n"));
            //
            // Ignoring the software interrupt for now
            //

            //we have one interrupt resource
            break;
        default:
            KdPrint(("das: something else\n"));
            //
            // Ignore all other descriptors.
            //
            break;
        }
    }

    //read and write badr1 info
    if (Badr1 != NULL) {
        ULONG data;
        KdPrint(("read badr1 at %d\n", (PULONG)((PUCHAR)Badr1 + INTR)));
        data = READ_PORT_ULONG(   (PULONG)(   (PUCHAR)Badr1 + INTR  )   );

        data |= INTE;
        data |= PCIINT;
        KdPrint(("write badr1 "));
        KdPrint(("data: %d\n", data));
        WRITE_PORT_ULONG((PULONG)((PUCHAR)Badr1 + INTR), data);
        data = READ_PORT_ULONG((PULONG)((PUCHAR)Badr1 + INTR));
        KdPrint(("read back out data: %d\n", data));
        data = 0;
        KdPrint(("read badr1\n"));
        data = READ_PORT_ULONG((PULONG)((PUCHAR)Badr1 + INTRSRC));
            data &= ~BIT0;
            data |= BIT1;
            data |= OUT0;
            KdPrint(("write badr1"));
            WRITE_PORT_ULONG((PULONG)((PUCHAR)Badr1 + INTRSRC), data);
    }

    if (devExt->Badr2 != NULL) {
        WRITE_PORT_UCHAR(devExt->Badr2 + DAS_STATUS, 0);
    }
    if (NT_SUCCESS(status))
        KdPrint(("das: Status: Successfully prepared hardware\n"));
    DasInterruptDisable(devExt);
    return status;
}

NTSTATUS
DasEvtDeviceReleaseHardware(
    IN WDFDEVICE Device,
    IN WDFCMRESLIST ResourcesTranslated
)
{
    //PFDO_DATA devExt;
    NTSTATUS status = STATUS_SUCCESS;
    UNREFERENCED_PARAMETER(ResourcesTranslated);
    UNREFERENCED_PARAMETER(Device);
    KdPrint(("das: -->releaseHardware"));
    KdPrint(("das: <--releaseHardware"));
    return status;
}



VOID
DasEvtDeviceFileCreate(
    IN WDFDEVICE Device,
    IN WDFREQUEST Request,
    IN WDFFILEOBJECT FileObject
)
{
    KdPrint(("das: ---> deviceFileCreate"));
    NTSTATUS status = STATUS_SUCCESS;
    PDAS_CONTEXT devExt;
    UNREFERENCED_PARAMETER(FileObject);
    devExt = DasGetData(Device);
    if (devExt == NULL)
        status = STATUS_DEVICE_DOES_NOT_EXIST;
    else if (devExt->Flags & DAS_ISOPEN)
    {
        KdPrint(("Device is busy\n"));
        status = STATUS_DEVICE_BUSY;
    }
    if (!NT_SUCCESS(status)) {
        KdPrint(("das: failed create\n"));
        WdfRequestComplete(Request, status);
        return;
    }
    devExt->Flags |= DAS_ISOPEN;
    devExt->readptr = 0;
    devExt->writeptr = 0;
    devExt->channel = 2;
    devExt->rate = 1000;
    devExt->counter = RATE_TO_COUNTER(devExt->rate);
    //devExt->counter = devExt->rate * 165 / 4;
    devExt->lights = 0;

    //send the defaults to the device
    WRITE_PORT_UCHAR(devExt->Badr2 + DAS_STATUS, devExt->channel);
    KdPrint(("das: <--- deviceFileCreate"));
    WdfRequestComplete(Request, status);
}

VOID
DasEvtFileClose(
    IN WDFFILEOBJECT    FileObject
)
{
    KdPrint(("das: ----> DasEvtFileClose"));
    PDAS_CONTEXT devExt;
    devExt = DasGetData(WdfFileObjectGetDevice(FileObject));
    devExt->Flags = 0;
    devExt->readptr = 0;
    devExt->writeptr = 0;
    devExt->lights = 0;
    devExt->channel = 2;

    WRITE_PORT_UCHAR(devExt->Badr2 + DAS_STATUS, ((devExt->lights << 4) | devExt->channel));
    KdPrint(("das: <---- DasEvtFileClose"));
}