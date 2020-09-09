/*dasio .h -- ioctl definitions for CS513 */
/*Sampled data is returned as delta time / data pairs in a 32 bit
 *integer . Delta time is in the most significant 16 bits, data in
 *the least significant bits . Delta time is in 10E -6 seconds .
 */
#define DAS_START_SAMPLING _IO(’D’, 0)
#define DAS_STOP_SAMPLING _IO(’D’, 1)
/*Rate ... int is time in units of 10E -5 seconds . So 100000 is 1 second . */
#define DAS_SET_RATE _IOW(’D’, 2, int)
#define DAS_GET_RATE _IOR(’D’, 3, int)
/*Channel is a number from 0 to 7. */
#define DAS_SET_CHANNEL _IOW(’D’, 4, int)
#define DAS_GET_CHANNEL _IOR(’D’, 5, int)
/*For debugging .. only */
/*Int is regno, register val returned in the int . */
#define DAS_GET_REGISTER _IOWR(’D’, 30, int)
/*Int in set register is (regno << 16) | data */
#define DAS_SET_REGISTER _IOW(’D’, 30, int)