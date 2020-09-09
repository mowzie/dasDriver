#include <sys/cdefs.h>
#include <sys/param.h>
#include <sys/kernel.h>
#include <sys/systm.h>
#include <sys/errno.h>
#include <sys/device.h>
#include <sys/conf.h>
#include <sys/proc.h>
#include <sys/uio.h>
#include <sys/file.h>
#include <sys/bus.h>
#include <sys/condvar.h>
#include <sys/mutex.h>
#include <dev/pci/pciio.h>
#include <dev/pci/pcivar.h>
#include <dev/pci/pcidevs.h>
#include <dev/pci/dasio.h>
#include <dev/pci/pcireg.h>

#define DAS_VENDOR 0 x1307
#define DAS_PRODUCT 0 x0029

// DAS Badr1
#define BADR1 PCI_BAR(1)
#define BADR1_INTR 0 x4c
#define BADR1_INTRSRC 0 x50
#define INTE __BIT(0)
#define PCIINT __BIT(6)
#define BIT0 __BIT(0)
#define BIT1 __BIT(1)
#define OUT0 __BIT(2)

// DAS Badr2
#define BADR2 PCI_BAR(2)
#define DAS_DATA1 0 x00
#define DAS_DATA2 0 x01
#define DAS_STATUS 0 x02

// Clock controls
#define COUNTER_RESET 0 xB4
#define COUNTER_LATCH 0 x80
#define COUNTER2 0 x06
#define COUNTER_CONTROL 0 x07
#define BUFF_SIZE(1 << 12)

static int das_match(device_t, cfdata_t, void *);
static void das_attach(device_t, device_t, void *);
int das_intr(void *arg);
dev_type_open(dasopen);
dev_type_write(daswrite);
dev_type_ioctl(dasioctl);
dev_type_close(dasclose);
dev_type_read(dasread);

struct das_softc
{
	uint32_t sc_buff[BUFF_SIZE];	// buffer for incoming data
	device_t sc_dev;	// the device
	void *sc_ih;	// pointer to interrupt handler ... but we don ’ t use it ?
	bus_space_tag_t iot2;	// tag for BADR2
	bus_space_handle_t ioh2;	// handle for BADR2
	uint16_t sc_read;	// read pointer (where to read from)
	uint16_t sc_write;	// write pointer (where to write to)
	uint16_t sc_rate;	// user supplied rate
	uint16_t sc_counter;	// clock ticks = rate *165/4 (165/4 = 330/8)
	uint16_t sc_old_clock;	// previously read clock
	uint8_t sc_channel;	// channel
	uint8_t sc_lights;	// which lights are on
	uint8_t sc_flags;	// state of device
	kcondvar_t sc_cv;	// condition variable for if data
	kmutex_t sc_lock;	// mutex for soft - c read / write
};

// flag states
#define DAS_ISOPEN(1 << 0)
#define DAS_ISSAMPLING(1 << 1)

// ring buffer helpers
static int bufsize(struct das_softc *);
static int bufempty(struct das_softc *);
static void bufwrite(uint32_t val, struct das_softc *);
static uint16_t bufmask(uint16_t);
CFATTACH_DECL_NEW(das, sizeof(struct das_softc), das_match, das_attach, NULL, NULL);
const struct cdevsw das_cdevsw = {
    .d_open = dasopen,
	.d_close = dasclose,
	.d_read = dasread,
	.d_write = daswrite,
	.d_ioctl = dasioctl,
	.d_stop = nostop,
	.d_poll = nopoll,
	.d_mmap = nommap,
	.d_kqfilter = nokqfilter,
	.d_discard = nodiscard,
	.d_flag = D_OTHER
};
extern struct cfdriver das_cd;

// match device with the system probe
static int das_match(device_t parent, cfdata_t cf, void *aux) {
	struct pci_attach_args *pa = aux;
	if (PCI_VENDOR(pa->pa_id) != DAS_VENDOR)
		return (0);
	if (PCI_PRODUCT(pa->pa_id) != DAS_PRODUCT)
		return (0);
	return (1);
}

// attach the device to the system
// setup handles to interrupts and registers
static void das_attach(device_t parent, device_t self, void *aux)
{
	struct das_softc *sc = device_private(self);
	struct pci_attach_args *pa = aux;
	pci_intr_handle_t ih;
	bus_space_tag_t iot1;
	bus_space_handle_t ioh1;
	pci_chipset_tag_t pc = pa->pa_pc;
	const char *intrstr;
	char intrbuf[PCI_INTRSTR_LEN];
	sc->sc_dev = self;
	uint32_t badr1;
	pci_aprint_devinfo(pa, NULL);
	if (pci_mapreg_map(pa, BADR1, PCI_MAPREG_TYPE_IO, 0, &iot1, &ioh1, NULL, NULL))
	{
		aprint_error_dev(self, " can ’t map IO space in BADR1 \n ");
		return;
	}

	if (pci_mapreg_map(pa, BADR2, PCI_MAPREG_TYPE_IO, 0, &sc->iot2, &sc->ioh2, NULL,
			NULL))
	{
		aprint_error_dev(self, " can ’t map IO space in BADR2 \n ");
		return;
	}

	// setup mutex and cv
	mutex_init(&sc->sc_lock, MUTEX_DEFAULT, IPL_VM);
	cv_init(&sc->sc_cv, " das_wait ");
	// map and establish the interrupt
	if (pci_intr_map(pa, &ih))
	{
		aprint_error_dev(self, " canouldn ’ t map interrupt \ n ");
		return;
	}

	intrstr = pci_intr_string(pc, ih, intrbuf, sizeof(intrbuf));
	sc->sc_ih = pci_intr_establish(pc, ih, IPL_VM, das_intr, sc);
	if (sc->sc_ih == NULL)
	{
		aprint_error_dev(self, " couldn ’t establish interrupt ");
		if (intrstr != NULL)
			aprint_error(" at %s", intrstr);
		aprint_error("\ n ");
		return;
	}

	aprint_normal_dev(self, " interrupting at %s\n", intrstr);
	badr1 = bus_space_read_4(iot1, ioh1, BADR1_INTR);
	badr1 |= INTE;
	badr1 |= PCIINT;
	bus_space_write_4(iot1, ioh1, BADR1_INTR, badr1);
	badr1 = bus_space_read_4(iot1, ioh1, BADR1_INTRSRC);
	badr1 &= ~BIT0;
	badr1 |= BIT1;
	badr1 |= OUT0;
	bus_space_write_4(iot1, ioh1, BADR1_INTRSRC, badr1);
	// turn off interrupts on the PCI board
	bus_space_write_1(sc->iot2, sc->ioh2, DAS_STATUS, 0);
}

// Open device
// set defaults
int dasopen(dev_t dev, int flags, int fmt, struct lwp *l)
{
	struct das_softc * sc;
	sc = device_lookup_private(&das_cd, minor(dev));
	if (sc == NULL)
		return (ENXIO);
	if (sc->sc_flags &DAS_ISOPEN)
		return (EBUSY);
	sc->sc_flags |= DAS_ISOPEN;
	// set the defaults
	sc->sc_read = 0;
	sc->sc_write = 0;
	sc->sc_channel = 2;
	sc->sc_lights = 0;
	bus_space_write_1(sc->iot2, sc->ioh2, DAS_STATUS, 2);
	sc->sc_rate = 1000;
	sc->sc_counter = (sc->sc_rate *165) / 4;
	return 0;
}

// close device
// clear out data used
// no need to wipe buffer, since we just overwrite it
int dasclose(dev_t dev, int flags, int fmt, struct lwp *l)
{
	struct das_softc * sc;
	sc = device_lookup_private(&das_cd, minor(dev));
	sc->sc_flags = 0;
	sc->sc_read = 0;
	sc->sc_write = 0;
	sc->sc_lights = 0;
	bus_space_write_1(sc->iot2, sc->ioh2, DAS_STATUS, 2);
	sc->sc_flags &= ~DAS_ISOPEN;
	return 0;
}

// this device does not support write
int daswrite(dev_t dev, struct uio *uio, int flags)
{
	return ENODEV;
}

// ioctl
// supports : start / stop sample
// set / get channel
// set / get rate
// set / get register (status &control of badr2)
int dasioctl(dev_t dev, u_long cmd, void *data, int flags, struct lwp *l)
{
	struct das_softc * sc;
	sc = device_lookup_private(&das_cd, minor(dev));
	int error = 0;
	int *val = (int*) data;
	uint8_t status = 0;
	switch (cmd)
	{
		case (DAS_START_SAMPLING):
			if (sc->sc_flags &DAS_ISSAMPLING)
				return EALREADY;
			sc->sc_flags |= DAS_ISSAMPLING;
			sc->sc_lights = 1;
			// start clock
			bus_space_write_1(sc->iot2, sc->ioh2, COUNTER_CONTROL, COUNTER_RESET);
			bus_space_write_1(sc->iot2, sc->ioh2, COUNTER2, (uint8_t)(sc->sc_counter &0 x00FF));

			bus_space_write_1(sc->iot2, sc->ioh2, COUNTER2, (uint8_t)(sc->sc_counter > > 8));
			// turn on interupts and set OP1
			status = sc->sc_lights << 4 | 1 << 3 | sc->sc_channel;
			bus_space_write_1(sc->iot2, sc->ioh2, DAS_STATUS, status);
			// start conversion
			bus_space_write_1(sc->iot2, sc->ioh2, DAS_DATA2, 1);
			// save counter value
			bus_space_write_1(sc->iot2, sc->ioh2, COUNTER_CONTROL, COUNTER_LATCH);
			uint8_t lsb = bus_space_read_1(sc->iot2, sc->ioh2, COUNTER2);
			uint8_t msb = bus_space_read_1(sc->iot2, sc->ioh2, COUNTER2);
			sc->sc_old_clock = (msb << 8) | lsb;
			break;
		case (DAS_STOP_SAMPLING):
			// store out the last sample
			status = bus_space_read_1(sc->iot2, sc->ioh2, DAS_STATUS);
			// XXX Have not seen this in my testing
			if (status &__BIT(7))
			{
				printf("*******************conversion is not done \ n ");
			}

			// read conversion data
			uint16_t dlsb = bus_space_read_1(sc->iot2, sc->ioh2, DAS_DATA1);
			uint16_t dmsb = bus_space_read_1(sc->iot2, sc->ioh2, DAS_DATA2);
			// make the sample
			uint32_t sample = ((sc->sc_counter - sc->sc_old_clock) *40 / 165) << 16 | dmsb << 4 | dlsb >
				>
				4;
			bufwrite(sample, sc);
			// turn off interupts and lights
			bus_space_write_1(sc->iot2, sc->ioh2, DAS_STATUS, sc->sc_channel);
			sc->sc_flags &= (~DAS_ISSAMPLING);
			break;
		case (DAS_SET_RATE):
			if (sc->sc_flags &DAS_ISSAMPLING)
			{
				return EAGAIN;
			}

			if (*val < 1 || *val > 1588)
				return EINVAL;
			sc->sc_rate = *val;
			sc->sc_counter = sc->sc_rate *165 / 4;
			break;
		case (DAS_GET_RATE):
			*
			val = sc->sc_rate;
			break;
		case (DAS_SET_CHANNEL):
			if (sc->sc_flags &DAS_ISSAMPLING)
			{
				return EAGAIN;
			}

			if ((*val < 0) || (*val) > 7)
			{
				return EINVAL;
			}

			sc->sc_channel = *val;
			status = (sc->sc_lights << 4) | *val;
			bus_space_write_1(sc->iot2, sc->ioh2, DAS_STATUS, status);
			break;
		case (DAS_GET_CHANNEL):
			*
			val = sc->sc_channel;
			break;
		case (DAS_GET_REGISTER):
			*
			val = bus_space_read_1(sc->iot2, sc->ioh2, DAS_STATUS);
			break;
		case (DAS_SET_REGISTER):
			if (sc->sc_flags &DAS_ISSAMPLING)
			{
				return EAGAIN;
			}

			if (*val < -1 || *val > 1588)
				return EINVAL;

			if (*val == -1)
			{
				bus_space_write_1(sc->iot2, sc->ioh2, DAS_STATUS, 0);
				break;
			}
			else
			{
				status = (sc->sc_lights << 4) | sc->sc_channel;
				status ^= __BIT(*val);
				sc->sc_lights = ((status) &0 xF0) > > 4;
				sc->sc_channel = status &0 x7;
				bus_space_write_1(sc->iot2, sc->ioh2, DAS_STATUS, status);
				break;
			}

		default:
			error = ENOTTY;
			break;
	}

	return error;
}

int dasread(dev_t dev, struct uio *uio, int flags)
{
	struct das_softc * sc;
	sc = device_lookup_private(&das_cd, minor(dev));
	int error, count;
	int wrapcnt, oresid;
	int sig;
	mutex_enter(&sc->sc_lock);
	// if empty : get sample if not sampling
	// otherwise, wait until atleast 1 sample is available
	while (bufempty(sc))
	{
		if (sc->sc_flags &DAS_ISSAMPLING)
		{
			sig = cv_wait_sig(&sc->sc_cv, &sc->sc_lock);
			if (sig != 0)
			{
				//(most likely) ctrl -c was triggered
				mutex_exit(&sc->sc_lock);
				return 0;
			}
		}
		else
		{
			break;
		}
	}

	// only support multiples of 4, so modify the request
	uint8_t modresid = (uio->uio_resid > > 2);
	// read data
	count = bufsize(sc);
	if (count > modresid)
		count = modresid;
	// no wrap arround
	// also for empty buffer to return EOF
	if (sc->sc_write >= sc->sc_read)
	{
		error = uiomove((uint32_t*) sc->sc_buff + sc->sc_read, count *4, uio);
		if (error == 0)
		{
			sc->sc_read = bufmask(sc->sc_read + count);
		}

		mutex_exit(&sc->sc_lock);
		return (error);
	}

	// wrap around
	wrapcnt = bufmask((bufsize(sc) - sc->sc_read));
	oresid = modresid;
	if (wrapcnt > modresid)
		wrapcnt = modresid;
	error = uiomove((uint32_t*) sc->sc_buff + sc->sc_read, wrapcnt *4, uio);
	if (error != 0 || wrapcnt == oresid)
	{
		mutex_exit(&sc->sc_lock);
		sc->sc_read = bufmask(sc->sc_read + wrapcnt);
		return (error);

	}

	// and everything else
	count -= wrapcnt;
	error = uiomove((uint32_t*) sc->sc_buff, count *4, uio);
	sc->sc_read = bufmask(sc->sc_read + count);
	mutex_exit(&sc->sc_lock);
	return (error);
}

// Interrupt handler
// check if our interupt
// check if expecting
// store data to buffer
int das_intr(void *arg)
{
	struct das_softc *sc = arg;
	// wasit my interupt ?
	uint8_t status = bus_space_read_1(sc->iot2, sc->ioh2, DAS_STATUS);
	if (!(__BIT(3) &status))
	{
		// not my interupt
		return 0;
	}

	// got interupt, but not expecting it
	if (!(sc->sc_flags &DAS_ISSAMPLING))
	{
		return 1;
	}

	// this is my interupt
	// conversion not done
	// XXX haven ’t seen this happen in my testing XXX
	if (status &__BIT(7))
	{
		printf("*******************conversion is not done \ n ");
		return -1;
	}

	// conversion complete - store all the info
	mutex_enter(&sc->sc_lock);
	// read conversion data
	uint16_t dlsb = bus_space_read_1(sc->iot2, sc->ioh2, DAS_DATA1);
	uint16_t dmsb = bus_space_read_1(sc->iot2, sc->ioh2, DAS_DATA2);
	// start next conversion
	bus_space_write_1(sc->iot2, sc->ioh2, DAS_STATUS, sc->sc_lights << 4 | 1 << 3 | sc - >
		sc_channel);
	bus_space_write_1(sc->iot2, sc->ioh2, DAS_DATA2, 1);
	// latch the clock and read the value
	bus_space_write_1(sc->iot2, sc->ioh2, COUNTER_CONTROL, COUNTER_LATCH);
	uint8_t clsb = bus_space_read_1(sc->iot2, sc->ioh2, COUNTER2);
	uint8_t cmsb = bus_space_read_1(sc->iot2, sc->ioh2, COUNTER2);
	// make the sample in 10^ -5 time
	uint32_t sample = ((sc->sc_counter - sc->sc_old_clock) *40 / 165) << 16 | dmsb << 4 | dlsb > > 4;
	sc->sc_old_clock = (cmsb << 8) | clsb;
	bufwrite(sample, sc);
	mutex_exit(&sc->sc_lock);
	cv_broadcast(&sc->sc_cv);
	return 1;
}

// helper functions for the ring buffer
static int bufsize(struct das_softc *sc)
{
	return bufmask(sc->sc_write - sc->sc_read);
}

static int bufempty(struct das_softc *sc)
{
	return (sc->sc_read == sc->sc_write);
}

static void bufwrite(uint32_t val, struct das_softc *sc)
{
	uint16_t next = (sc->sc_write + 1) &(BUFF_SIZE - 1);
	// buffer is full, so move the read pointer
	// this drops old data in preference to new data
	if (next == sc->sc_read)
		sc->sc_read = (next + 1) &(BUFF_SIZE - 1);
	sc->sc_buff[sc->sc_write] = val;
	sc->sc_write = next;
}

static uint16_t bufmask(uint16_t val)
{
	return val &(BUFF_SIZE - 1);
}
