/*++

From a file that was:

Copyright (c) 1990-2000 Microsoft Corporation, All Rights Reserved
 
Module Name:

    wtoyio.h  

Phil Nelson, Spring 2004

--*/

#if !defined(__WDASIO_H__)
#define __WDASIO_H__

//
// Define the IOCTL codes we will use.  The IOCTL code contains a command
// identifier, plus other information about the device, the type of access
// with which the file must have been opened, and the type of buffering.
//

//
// Device type           -- in the "User Defined" range."
//

#define DAS_TYPE 0xff01

// The IOCTL function codes from 0x800 to 0xFFF are for customer use.

#define IOCTL_DAS_START_SAMPLING \
    CTL_CODE( DAS_TYPE, 0xFF0, METHOD_BUFFERED, FILE_READ_ACCESS )

#define IOCTL_DAS_STOP_SAMPLING \
    CTL_CODE( DAS_TYPE, 0xFF1, METHOD_BUFFERED, FILE_READ_ACCESS )

#define IOCTL_DAS_SET_RATE \
    CTL_CODE( DAS_TYPE, 0xFF2, METHOD_BUFFERED, FILE_WRITE_ACCESS )

#define IOCTL_DAS_GET_RATE \
    CTL_CODE( DAS_TYPE, 0xFF3, METHOD_BUFFERED, FILE_READ_ACCESS )

#define IOCTL_DAS_SET_CHANNEL \
    CTL_CODE( DAS_TYPE, 0xFF4, METHOD_BUFFERED, FILE_WRITE_ACCESS )

#define IOCTL_DAS_GET_CHANNEL \
    CTL_CODE( DAS_TYPE, 0xFF5, METHOD_BUFFERED, FILE_READ_ACCESS )


//  If helpful, implement these ... not required.

#define IOCTL_DAS_GET_REGISTER \
    CTL_CODE( DAS_TYPE, 0xFF6, METHOD_BUFFERED, FILE_WRITE_ACCESS | \
              FILE_READ_ACCESS )

// Int in set register is (regno << 16) | data
#define IOCTL_DAS_SET_REGISTER \
    CTL_CODE( DAS_TYPE, 0xFF7, METHOD_BUFFERED, FILE_WRITE_ACCESS )

#endif






