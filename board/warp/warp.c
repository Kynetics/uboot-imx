/*
 * Copyright (C) 2013-2014 Freescale Semiconductor, Inc.
 *
 * Author: Fabio Estevam <fabio.estevam@freescale.com>
 *
 * Copyright (C) 2014 O.S. Systems Software LTDA.
 *
 * Author: Otavio Salvador <otavio@ossystems.com.br>
 *
 * Copyright (C) 2014 Kynetics, LLC
 *
 * Backport to 2013.04: Diego Rondini
 *
 * Copyright (C) 2014 Revolution Robotics, Inc.
 *
 * Author: Jacob Postman <jacob@revolution-robotics.com>
 *
 * SPDX-License-Identifier:    GPL-2.0+
 */

#include <asm/arch/clock.h>
#include <asm/arch/crm_regs.h>
#include <asm/arch/iomux.h>
#include <asm/arch/imx-regs.h>
#include <asm/arch/mx6-pins.h>
#include <asm/arch/sys_proto.h>
#include <asm/gpio.h>

#include <asm/imx-common/iomux-v3.h>
#include <asm/imx-common/boot_mode.h>
#include <asm/io.h>
#include <asm/sizes.h>
#include <common.h>
#include <fsl_esdhc.h>
#include <mmc.h>
#include <max77696.h>

#include "warp_common.h"

// I2C Dependencies
#include <asm/imx-common/mxc_i2c.h>
#include <i2c.h>
#include "warp_bbi2c.h"

#include "warp_mx6sl_pwm.h"
#include "ssd2805.h"
#include "lh154.h"
#include "warp_lcd.h"

#ifdef CONFIG_FASTBOOT
#include <fastboot.h>
#ifdef CONFIG_ANDROID_RECOVERY
#include <recovery.h>
#endif
#endif /*CONFIG_FASTBOOT*/

DECLARE_GLOBAL_DATA_PTR;

struct warp_cfg warp_cfg_info = {
	.rev = -1,
	.pin_lcd_rdx = PINID_LCD_RD_REV1P12,
	.pin_lcd_dcx = PINID_LCD_RS_REV1P12,
	.pin_mipi_rstn = PINID_MIPI_RSTN_REV1P12,
};

int get_warp_rev(void){

	int version = 0;

	// Set revision pads as GPIO with weak pull-downs
	imx_iomux_v3_setup_multiple_pads(brd_rev_pads, ARRAY_SIZE(brd_rev_pads));

	// SD1_DAT4 (REV Bit 2)
	// SD1_DAT5 (REV Bit 1)
	// SD1_DAT6 (REV Bit 0)
	gpio_direction_input(BRD_REV_GPIO_0);
	gpio_direction_input(BRD_REV_GPIO_1);
	gpio_direction_input(BRD_REV_GPIO_2);

	// Drive SD1_DAT7 high
	gpio_direction_output(BRD_REV_GPIO_CHK,1);

	// Read version value from GPIO
	version = (gpio_get_value(BRD_REV_GPIO_0) << 0);
	version |= (gpio_get_value(BRD_REV_GPIO_1) << 1);
	version |= (gpio_get_value(BRD_REV_GPIO_2) << 2);

	return version;
}

static void setup_warp_rev_pads(void){
	switch(warp_cfg_info.rev){
	case BRD_REV_1P10:
		puts("1.10\n");
		warp_cfg_info.pin_lcd_rdx = PINID_LCD_RD_REV1P10;
		warp_cfg_info.pin_lcd_dcx = PINID_LCD_RS_REV1P10;
		warp_cfg_info.pin_mipi_rstn = PINID_MIPI_RSTN_REV1P10;
		break;
	case BRD_REV_1P12:
		puts("1.12\n");
		warp_cfg_info.pin_lcd_rdx = PINID_LCD_RD_REV1P12;
		warp_cfg_info.pin_lcd_dcx = PINID_LCD_RS_REV1P12;
		warp_cfg_info.pin_mipi_rstn = PINID_MIPI_RSTN_REV1P12;
		break;
	default:
		printf("unknown revision code (%d)\n",
			warp_cfg_info.rev);
	}
}

int dram_init(void)
{
	gd->ram_size = get_ram_size((void *)PHYS_SDRAM, PHYS_SDRAM_SIZE);

	return 0;
}

static void setup_iomux_uart(void)
{
	imx_iomux_v3_setup_multiple_pads(uart1_pads, ARRAY_SIZE(uart1_pads));
}

struct i2c_pads_info i2c_pad_info0 = {
       .scl = {
               .i2c_mode =  MX6_PAD_I2C1_SCL__I2C1_SCL | MUX_PAD_CTRL(I2C_PAD_CTRL),
               .gpio_mode = MX6_PAD_I2C1_SCL__GPIO_3_12 | MUX_PAD_CTRL(I2C_PAD_CTRL),
               .gp = IMX_GPIO_NR(3, 12)
       },
       .sda = {
               .i2c_mode = MX6_PAD_I2C1_SDA__I2C1_SDA | MUX_PAD_CTRL(I2C_PAD_CTRL),
               .gpio_mode = MX6_PAD_I2C1_SDA__GPIO_3_13 | MUX_PAD_CTRL(I2C_PAD_CTRL),
               .gp = IMX_GPIO_NR(3, 13)
       }
};

struct i2c_pads_info i2c_pad_info1 = {
       .scl = {
               .i2c_mode =  MX6_PAD_I2C2_SCL__I2C2_SCL | MUX_PAD_CTRL(WARP_I2C_PAD_CTRL_3V),
               .gpio_mode = MX6_PAD_I2C2_SCL__GPIO_3_14 | MUX_PAD_CTRL(WARP_I2C_PAD_CTRL_3V),
               .gp = IMX_GPIO_NR(3, 14)
       },
       .sda = {
               .i2c_mode = MX6_PAD_I2C2_SDA__I2C2_SDA | MUX_PAD_CTRL(WARP_I2C_PAD_CTRL_3V),
               .gpio_mode = MX6_PAD_I2C2_SDA__GPIO_3_15 | MUX_PAD_CTRL(WARP_I2C_PAD_CTRL_3V),
               .gp = IMX_GPIO_NR(3, 15)
       }
};

#ifdef CONFIG_FSL_ESDHC

#define USDHC2_CD_GPIO	IMX_GPIO_NR(5, 0)

static struct fsl_esdhc_cfg usdhc_cfg[1] = {
	{USDHC2_BASE_ADDR},
};

int mmc_get_env_devno(void)
{
	u32 soc_sbmr = readl(SRC_BASE_ADDR + 0x4);
	u32 dev_no;

	/* BOOT_CFG2[3] and BOOT_CFG2[4] */
	dev_no = (soc_sbmr & 0x00001800) >> 11;

	return dev_no;
}

int board_mmc_getcd(struct mmc *mmc)
{
	return 1;       /* Assume boot SD always present */
}

int board_mmc_init(bd_t *bis)
{
	imx_iomux_v3_setup_multiple_pads(usdhc2_pads, ARRAY_SIZE(usdhc2_pads));
	gpio_direction_input(USDHC2_CD_GPIO);
	usdhc_cfg[0].sdhc_clk = mxc_get_clock(MXC_ESDHC2_CLK);

	if (fsl_esdhc_initialize(bis, &usdhc_cfg[0]))
		printf("Warning: failed to initialize mmc dev\n");

	return 0;
}

void board_late_mmc_env_init(void)
{
	char cmd[32];
	u32 dev_no = mmc_get_env_devno();

	setenv_ulong("mmcdev", dev_no);

	sprintf(cmd, "mmc dev %d", dev_no);
	run_command(cmd, 0);
}
#endif

static void __maybe_unused config_pwm3(void)
{
	int reg;
	reg = PWMCR_CLKSRC(2);
	writel(reg, PWM3_PWMCR);
	writel(0x1, PWM3_PWMSAR);
	writel(0x0, PWM3_PWMPR);
}

static void __maybe_unused enable_pwm3(void){
	int reg;
	reg = readl(PWM3_PWMCR);
	reg |= PWMCR_ENABLE;
	writel(reg, PWM3_PWMCR);
}

static void __maybe_unused config_pwm4(void)
{
	int reg;
	reg = PWMCR_CLKSRC(2);
	writel(reg, PWM4_PWMCR);
	writel(0x1, PWM4_PWMSAR);
	writel(0x0, PWM4_PWMPR);
}

static void __maybe_unused enable_pwm4(void){
	int reg;
	reg = readl(PWM4_PWMCR);
	reg |= PWMCR_ENABLE;
	writel(reg, PWM4_PWMCR);
}

static void config_MAX77696_GPIO(void){
	unsigned int reg;
	// Set PMIC GPIO to Solomon:
	// GPIO0 - MIPI_PS0
	// GPIO1 - MIPI_PS1
	// GPIO2 - MIPI_PS2
	// GPIO3 - MIPI_IF_SEL
	// Configure GPIO as push-pull output
	// DIRx=0
	// PPDRVx=1
	// AMEx=0
	// DOx = 1 or 0
	//Set PS[1:0] (PMIC_GPIO[1:0]) to 11 per Solomon recommendation,
	// 	Might want/need 01 for 8Bit, 3 Wire SPI mode instead
	//	00: 24 Bit 3 Wire SPI interface
	//	01: 8 Bit 3 Wire SPI interface
	//	10: 8 Bit 4 Wire SPI interface
	//	11: No SPI interface
	// Set PS[3:2] to 01 for 16bit 8080 MCU interface mode (PS3 tied to GND)
	i2c_reg_write(MAX77696_PMIC_ADDR, AME_GPIO, 0x00);
	reg = (1 << PPRDRVx_SHIFT); // Configure as push-pull
	reg |= (1 << DOx_SHIFT); // Configure as output = 1
	reg &= ~(1 << DIRx_SHIFT); // Configure as general purpose output

	i2c_reg_write(MAX77696_PMIC_ADDR, CNFG_GPIO0, reg);
	i2c_reg_write(MAX77696_PMIC_ADDR, CNFG_GPIO1, reg);

#if (LCDIF_BUS_WIDTH == 8)
	reg &= ~(1 << DOx_SHIFT); // Configure as output = 0
#elif (LCDIF_BUS_WIDTH == 16)
	reg |= (1 << DOx_SHIFT); // Configure as output = 1
#endif
	i2c_reg_write(MAX77696_PMIC_ADDR, CNFG_GPIO2, reg);

	//Set MIPI_IF_SEL (GPIO3) to 1 for MCU 8080 Scheme 1 Mode
	reg |= (1 << DOx_SHIFT); // Configure as output = 1
	i2c_reg_write(MAX77696_PMIC_ADDR, CNFG_GPIO3, reg);
}

static void enable_MAX77696_V1P8_LDO(void)
{
	unsigned int reg;
	// Set to normal mode (0xC0) and 1.8V output (0x28)
	reg = (0x28 | 0xC0);
	// Enable V1P8_LDO - OUTLDO4 enable
	i2c_reg_write(MAX77696_PMIC_ADDR, L04_CNFG1, reg);
}

static void enable_MAX77696_V2P9_LOADSWITCH(void){
	unsigned int reg;
	// Enable Load Switch 3 with other settings kept at default values
	reg = (0x0D);
	i2c_reg_write(MAX77696_PMIC_ADDR, SW3_CNTRL, reg);
}

static void set_MAX77696_V3P0(void)
{
	unsigned int reg;
	reg = (0xC0);
	// Enable V1P8_LDO - OUTLDO4 enable
	i2c_reg_write(MAX77696_PMIC_ADDR, VOUT6, reg);
}

static void setup_display(void)
{
	// Configure iMX6SL pads
	set_lcd_pads();

	// Configure iMX6SL registers for 8080 MPU mode (may be done in SSD driver)
	// SSD2805C power on sequence

	// PMIC: Configure PMIC GPIO to set SSD2805C mode
	config_MAX77696_GPIO();
	udelay(1000);

	// IMX6: Set MIPI_RESET# high
	// Set high during pad config

	// IMX6: Enable TX_CLK PWM output 5-50MHz
//	config_pwm3();
//	enable_pwm3();
	config_pwm4();
	enable_pwm4();
	udelay(2000);

	// Initialize iMX6 LCD interface
	lcdif_init();

	// 4. PMIC: Enable V1P8_LDO and V2P9 load switch
	enable_MAX77696_V1P8_LDO();
	enable_MAX77696_V2P9_LOADSWITCH();

	// Delay to ensure MIPI_RSTn is high for >=250ms before reset.
	udelay(200000);

	// 5. IMX6: Set MIPI_RESET# low for >100us then set high
	gpio_direction_output(PINID_LCD_RSTN, 0);  	// LCD_RSTn
	gpio_direction_output(warp_cfg_info.pin_mipi_rstn, 0); 	// MIPI_RSTn
	udelay(100);
	gpio_direction_output(PINID_LCD_RSTN, 1);  	// LCD_RSTn
	gpio_direction_output(warp_cfg_info.pin_mipi_rstn, 1); 	// MIPI_RSTn
	udelay(100);

	// LCD power on sequence
	// 1. LCD - Enable 1.8V (always on, ignore step)
	// 2. Wait 10us
	// 3. Set reset to HIGH
	// 4. Wait 1ms
	// 5. LCD - Enable 2.9V (always on, ignore step)
	// 	Future rev uses V2P9_GATED rail instead of V2P9
	// 6. Wait 5ms
	// 7. LCD - Turn on High-speed clock
	// 8. Wait 10ms
	// 9. Exit sleep mode (send Register 0x11)
	// 10. Wait 120ms
	// 11. Wait 40ms (wait 2 frames)
	// 12. Send configuration register data
	// 13. Send image
	// 14. Send display on command
	lh154_init_panel();

	// Set LED Backlight drive strength
	i2c_reg_write(MAX77696_PMIC_ADDR, LED1CURRENT_1, 0xf0);
	i2c_reg_write(MAX77696_PMIC_ADDR, LED1CURRENT_2, 0x00);

	// Enable LED Backlight
	i2c_reg_write(MAX77696_PMIC_ADDR, LEDBST_CNTRL_1, 0x80);
}

int board_early_init_f(void)
{
	// Get board version from GPIO
	warp_cfg_info.rev = get_warp_rev();

	// Configure FXOS8700 addresss pads
	if(warp_cfg_info.rev == BRD_REV_1P10){
		imx_iomux_v3_setup_multiple_pads(bbi2c_pads, ARRAY_SIZE(bbi2c_pads));
		gpio_direction_output(BBI2C_ADDR0, 0);
		gpio_direction_output(BBI2C_ADDR1, 0);
	}

	setup_iomux_uart();
	setup_i2c(0, CONFIG_SYS_I2C_SPEED, 0x7f, &i2c_pad_info0);
	setup_i2c(1, CONFIG_SYS_I2C_SPEED, 0x7f, &i2c_pad_info1);

	return 0;
}

/******************************************************
 * On the development rev of WaRP FXOS8700
 * comes up in I2C mode by default. This workaround
 * sets the mode configuration pads as necessary and
 * bit-bangs the FXOS8700 I2C interface to initiate
 * a soft reset. This results in the FXOS8700 coming
 * up in the intended SPI mode which is then available
 * to interface with from the Linux/Android kernel.
 ******************************************************/
void fxos8700_init(void){

	unsigned char ret = 0;

	if(warp_cfg_info.rev == BRD_REV_1P10){

		BBI2C_Init();

		imx_iomux_v3_setup_multiple_pads(bbi2c_uncfg_addr, ARRAY_SIZE(bbi2c_uncfg_addr));
		gpio_direction_input(BBI2C_ADDR0);
		gpio_direction_input(BBI2C_ADDR1);

		// SEND RESET SEQUENCE ****************************
		// Send I2C Start Sequence
		BBI2C_Start();

		ret = BBI2C_TransmitByte((FXOS8700_I2C_ADDR << 1) | (BBI2C_WRITE));
		if(ret){
			printf("Failed sending device address to FXOS\n");
			goto stop_seq;
		}
		ret = BBI2C_TransmitByte(0x2B);
		if(ret){
			printf("Failed sending register address to FXOS\n");
			goto stop_seq;
		}

		ret = BBI2C_TransmitByte(0x40);
		if(ret){
		// Doesn't actually fail - device just resets immediately.
		//	printf("Failed sending data to FXOS\n");
		}
		stop_seq:
		BBI2C_Stop();

		// END RESET SEQUENCE ***********************
	} else{
		imx_iomux_v3_setup_multiple_pads(bbi2c_uncfg_addr, ARRAY_SIZE(bbi2c_uncfg_addr));
		gpio_direction_input(BBI2C_ADDR0);
		gpio_direction_input(BBI2C_ADDR1);
		imx_iomux_v3_setup_multiple_pads(fxos_rstn_pads, ARRAY_SIZE(fxos_rstn_pads));
		gpio_direction_output(FXOS8700_RSTN, 1);
		udelay(100);
		gpio_direction_output(FXOS8700_RSTN, 0);
		udelay(100);
	}
}

int board_init(void)
{
	/* address of boot parameters */
	gd->bd->bi_boot_params = PHYS_SDRAM + 0x100;

	// Set manual reset button hold time to 2 seconds (min value)
	i2c_reg_write(MAX77696_PMIC_ADDR, GLBLCNFG1, 0x22);

	// Initialize the FXOS8700 into SPI mode
	fxos8700_init();

	return 0;
}

#ifdef CONFIG_CMD_BMODE
static const struct boot_mode board_boot_modes[] = {
	// 8 bit bus width
	{"emmc8bitddr", MAKE_CFGVAL(0x60, 0xCA, 0x00, 0x00)},
	{"emmc8bit", MAKE_CFGVAL(0x60, 0x4A, 0x00, 0x00)},
	{NULL,	 0},
};
#endif

int board_late_init(void)
{
#ifdef CONFIG_CMD_BMODE
	add_board_boot_modes(board_boot_modes);
#endif

#ifdef CONFIG_ENV_IS_IN_MMC
	board_late_mmc_env_init();
#endif
	if(warp_cfg_info.rev >= 0)
		setup_display();

	return 0;
}

u32 get_board_rev(void)
{
	return get_cpu_rev();
}

#ifdef CONFIG_FASTBOOT

void board_fastboot_setup(void)
{
#if defined(CONFIG_FASTBOOT_STORAGE_MMC)
	if (!getenv("fastboot_dev"))
		setenv("fastboot_dev", "mmc0");
	if (!getenv("bootcmd"))
		setenv("bootcmd", "booti mmc0");
#else
	printf("unsupported boot devices\n");
#endif /*CONFIG_FASTBOOT_STORAGE_MMC*/
}

#ifdef CONFIG_ANDROID_RECOVERY
int check_recovery_cmd_file(void)
{
    return recovery_check_and_clean_flag();
}

void board_recovery_setup(void)
{
	int bootdev = get_boot_device();

	/*current uboot BSP only supports USDHC2*/
	switch (bootdev) {
#if defined(CONFIG_FASTBOOT_STORAGE_MMC)
	case SD1_BOOT:
	case MMC1_BOOT:
		if (!getenv("bootcmd_android_recovery"))
			setenv("bootcmd_android_recovery",
					"booti mmc0 recovery");
		break;
	case SD2_BOOT:
	case MMC2_BOOT:
		if (!getenv("bootcmd_android_recovery"))
			setenv("bootcmd_android_recovery",
					"booti mmc1 recovery");
		break;
	case SD3_BOOT:
	case MMC3_BOOT:
		if (!getenv("bootcmd_android_recovery"))
			setenv("bootcmd_android_recovery",
					"booti mmc2 recovery");
		break;
#endif /*CONFIG_FASTBOOT_STORAGE_MMC*/
	default:
		printf("Unsupported bootup device for recovery: dev: %d\n",
			bootdev);
		return;
	}

	printf("setup env for recovery..\n");
	setenv("bootcmd", "run bootcmd_android_recovery");
}

#endif /*CONFIG_ANDROID_RECOVERY*/

#endif /*CONFIG_FASTBOOT*/

int checkboard(void)
{
	puts("    _    _      ____________\n");
	puts("   | |  | |     | ___ | ___ \\ \n");
	puts("   | |  | | __ _| |_/ | |_/ /\n");
	puts("   | |/\\| |/ _` |    /|  __/\n");
	puts("   \\  /\\  | (_| | |\\ \\| |\n");
	puts("    \\/  \\/ \\__,_\\_| \\_\\_|\n");
	puts("\n");

	puts("Board: WaRP Board Revision ");
	setup_warp_rev_pads();

	return 0;
}

#ifdef CONFIG_IMX_UDC
void udc_pins_setting(void)
{
	imx_iomux_v3_setup_multiple_pads(otg_udc_pads,
			ARRAY_SIZE(otg_udc_pads));
}
#endif // CONFIG_IMX_UDC

#ifdef CONFIG_USB_EHCI_MX6
int board_ehci_hcd_init(int port)
{
	switch (port) {
	case 0:
		imx_iomux_v3_setup_multiple_pads(usb_otg1_pads,
			ARRAY_SIZE(usb_otg1_pads));
		break;
	case 1:
		imx_iomux_v3_setup_multiple_pads(usb_otg2_pads,
			ARRAY_SIZE(usb_otg2_pads));
		break;
	default:
		printf("MXC USB port %d not yet supported\n", port);
		return 1;
	}
	return 0;
}
#endif // CONFIG_USB_EHCI_MX6
