#include <stdint.h>
#include <string.h>
#include <device/pci_def.h>
#include <arch/io.h>
#include <device/pnp_def.h>
#include <cpu/x86/lapic.h>
#include <pc80/mc146818rtc.h>
#include <console/console.h>
#include <lib.h>
#include <spd.h>
#include <cpu/amd/model_fxx_rev.h>
#include "northbridge/amd/amdk8/incoherent_ht.c"
#include <southbridge/nvidia/ck804/early_smbus.h>
#include <northbridge/amd/amdk8/raminit.h>
#include <delay.h>
#include <cpu/x86/lapic.h>
#include "northbridge/amd/amdk8/reset_test.c"
#include "northbridge/amd/amdk8/debug.c"
#include <superio/winbond/common/winbond.h>
#include <superio/winbond/w83627hf/w83627hf.h>
#include <cpu/x86/bist.h>
#include "northbridge/amd/amdk8/setup_resource_map.c"

#define SERIAL_DEV PNP_DEV(0x2e, W83627HF_SP1)

static void memreset(int controllers, const struct mem_controller *ctrl) { }
static void activate_spd_rom(const struct mem_controller *ctrl) { }

static inline int spd_read_byte(unsigned device, unsigned address)
{
	return smbus_read_byte(device, address);
}

#include "northbridge/amd/amdk8/raminit.c"
#include "northbridge/amd/amdk8/coherent_ht.c"
#include "lib/generic_sdram.c"
#include "resourcemap.c"
#include "cpu/amd/dualcore/dualcore.c"
#include <southbridge/nvidia/ck804/early_setup_ss.h>

//set GPIO to input mode
#define CK804_MB_SETUP \
	RES_PORT_IO_8, SYSCTRL_IO_BASE + 0xc0+15, ~(0xff),((0<<4)|(0<<2)|(0<<0)),/* M8,GPIO16, PCIXA_PRSNT2_L*/ \
	RES_PORT_IO_8, SYSCTRL_IO_BASE + 0xc0+44, ~(0xff),((0<<4)|(0<<2)|(0<<0)),/* P5,GPIO45, PCIXA_PRSNT1_L*/ \
	RES_PORT_IO_8, SYSCTRL_IO_BASE + 0xc0+16, ~(0xff),((0<<4)|(0<<2)|(0<<0)),/* K4,GPIO17, PCIXB_PRSNT1_L*/ \
	RES_PORT_IO_8, SYSCTRL_IO_BASE + 0xc0+45, ~(0xff),((0<<4)|(0<<2)|(0<<0)),/* P7,GPIO46, PCIXB_PRSNT2_L*/ \

#include "southbridge/nvidia/ck804/early_setup_car.c"
#include "cpu/amd/model_fxx/init_cpus.c"
#include "northbridge/amd/amdk8/early_ht.c"

static void sio_setup(void)
{
	uint32_t dword;
	uint8_t byte;

	byte = pci_read_config8(PCI_DEV(0, CK804_DEVN_BASE+1 , 0), 0x7b);
	byte |= 0x20;
	pci_write_config8(PCI_DEV(0, CK804_DEVN_BASE+1 , 0), 0x7b, byte);

	dword = pci_read_config32(PCI_DEV(0, CK804_DEVN_BASE+1 , 0), 0xa0);
	dword |= (1<<0);
	pci_write_config32(PCI_DEV(0, CK804_DEVN_BASE+1 , 0), 0xa0, dword);
}

void cache_as_ram_main(unsigned long bist, unsigned long cpu_init_detectedx)
{
	static const uint16_t spd_addr [] = {
		DIMM0, DIMM2, 0, 0,
		DIMM1, DIMM3, 0, 0,
#if CONFIG_MAX_PHYSICAL_CPUS > 1
		DIMM4, DIMM6, 0, 0,
		DIMM5, DIMM7, 0, 0,
#endif
	};

	int needs_reset;
	unsigned bsp_apicid = 0, nodes;
	struct mem_controller ctrl[8];

	if (!cpu_init_detectedx && boot_cpu()) {
		/* Nothing special needs to be done to find bus 0 */
		/* Allow the HT devices to be found */
		enumerate_ht_chain();
		sio_setup();
	}

	if (bist == 0)
		bsp_apicid = init_cpus(cpu_init_detectedx);

//	post_code(0x32);

	winbond_enable_serial(SERIAL_DEV, CONFIG_TTYS0_BASE);
	console_init();

	/* Halt if there was a built in self test failure */
	report_bist_failure(bist);

	setup_mb_resource_map();

	needs_reset = setup_coherent_ht_domain();

	wait_all_core0_started();
#if CONFIG_LOGICAL_CPUS
	// It is said that we should start core1 after all core0 launched
	start_other_cores();
	wait_all_other_cores_started(bsp_apicid);
#endif

	needs_reset |= ht_setup_chains_x();
	needs_reset |= ck804_early_setup_x();
	if (needs_reset) {
		printk(BIOS_INFO, "ht reset -\n");
		soft_reset();
	}

	allow_all_aps_stop(bsp_apicid);

	nodes = get_nodes();
	//It's the time to set ctrl now;
	fill_mem_ctrl(nodes, ctrl, spd_addr);

	enable_smbus();

	sdram_initialize(nodes, ctrl);

	post_cache_as_ram();
}
