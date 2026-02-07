__attribute__((optimize("O0")))
// 1.26ms
void suck_loop(uint64_t loop) {
        loop = loop * 50 * 100;
        uint64_t a;
        while (loop > 0) {
                a = loop / (uint64_t)99;
                a = loop / (uint64_t)77;
                a = loop / (uint64_t)55;
                a = loop / (uint64_t)33;
                a = loop / (uint64_t)11;
                a = loop / (uint64_t)999;
                a = loop / (uint64_t)777;
                a = loop / (uint64_t)555;
                a = loop / (uint64_t)333;
                a = loop / (uint64_t)111;
                a = loop / (uint64_t)9999;
                a = loop / (uint64_t)7777;
                a = loop / (uint64_t)5555;
                a = loop / (uint64_t)3333;
                a = loop / (uint64_t)1111;
                a = loop / (uint64_t)99999;
                a = loop / (uint64_t)77777;
                a = loop / (uint64_t)55555;
                a = loop / (uint64_t)33333;
                a = loop / (uint64_t)11111;
                loop--;
        }
}

static inline void user_led_on(void) {
	uint32_t val;
        val = mmio_read_32(0x03020000); // GPIO0
        val |= (1 << 14);               // A14
        mmio_write_32(0x03020000, val);
}

static inline void user_led_off(void) {
	uint32_t val;
        val = mmio_read_32(0x03020000);
        val &= ~(1 << 14);
        mmio_write_32(0x03020000, val);
}

static inline void user_led_toggle(void) {
	uint32_t val;
        val = mmio_read_32(0x03020000);
        val ^= (1 << 14);
        mmio_write_32(0x03020000, val);
}

int cvi_board_init(void)
{
        uint32_t val;

	// user led
	mmio_write_32(0x03001038, 0x3); // GPIOA 14 GPIO_MODE
	val = mmio_read_32(0x03020004); // GPIOA DIR
        val |= (1 << 14); // output
        mmio_write_32(0x03020004, val);
	user_led_toggle();

        // wifi power reset
        mmio_write_32(0x0300104C, 0x3); // GPIOA 26
        val = mmio_read_32(0x03020004); // GPIOA DIR
        val |= (1 << 26); // output
        mmio_write_32(0x03020004, val);

        val = mmio_read_32(0x03020000); // signal level
        val &= ~(1 << 26); // set level to low
        mmio_write_32(0x03020000, val);

        suck_loop(50);
	user_led_toggle();

        val = mmio_read_32(0x03020000); // signal level
        val |= (1 << 26); // set level to high
        mmio_write_32(0x03020000, val);

        // wifi sdio pinmux
        mmio_write_32(0x030010D0, 0x0); // D3
        mmio_write_32(0x030010D4, 0x0); // D2
        mmio_write_32(0x030010D8, 0x0); // D1
        mmio_write_32(0x030010DC, 0x0); // D0
        mmio_write_32(0x030010E0, 0x0); // CMD
        mmio_write_32(0x030010E4, 0x0); // CLK

	user_led_toggle();
        // lcd backlight
        //mmio_write_32(0x030010EC, 0x0); // GPIOB 0 PWM0_BUCK
	// for licheervnano alpha
	val = mmio_read_32(0x03021000); // signal level
        val |= (1 << 0); // set level to high
        mmio_write_32(0x03021000, val);
        val = mmio_read_32(0x03021004); // GPIOB DIR
        val |= (1 << 0); // output
        mmio_write_32(0x03021004, val);
        mmio_write_32(0x030010EC, 0x3); // GPIOB 0 GPIO_MODE

	// for licheervnano beta
	mmio_write_32(0x030010ac, 0x0); // PWRGPIO 2 GPIO_MODE

        // touch i2c
        mmio_write_32(0x03001090, 0x5); // PWR_GPIO6 IIC4_SCL TP_SCL
        mmio_write_32(0x03001098, 0x5); // PWR_GPIO8 IIC4_SDA TP_SDA
        // touch function
        // IOPWR_SEQ1functionselect:
        // • 0:PWR_SEQ1(default)
        // • 3:PWR_GPIO[3]
        mmio_write_32(0x03001084, 0x3); // PWR_SEQ1 PWR_GPIO[3]
        mmio_write_32(0x03001088, 0x3); // PWR_SEQ2 PWR_GPIO[4]
        mmio_write_32(0x05027078, 0x11);// Unlock PWR_GPIO[3]
        mmio_write_32(0x0502707c, 0x11);// Unlock PWR_GPIO[4]

        // display
        mmio_write_32(0x0300103C, 0x03); // XGPIOA[15] GPIO_MODE DC
	mmio_write_32(0x03001058, 0x03); // XGPIOA[27] GPIO_MODE RST
	
        // SPI0 pinmux
        mmio_write_32(0x030011AC, 0x06); // MIPI_TXM1 XGPIOC[14] SPI0_SDO   18(FPC上的编号)
        mmio_write_32(0x030011B0, 0x06); // MIPI_TXP1 XGPIOC[15] SPI0_SDI   17(FPC上的编号)
        mmio_write_32(0x030011A4, 0x06); // MIPI_TXM2 XGPIOC[16] SPI0_SCK   15(FPC上的编号)
        mmio_write_32(0x030011A8, 0x06); // MIPI_TXP2 XGPIOC[17] SPI0_CS_X  14(FPC上的编号)

        /*
        命令行验证通信
        # devmem 0x030011AC 32 6
        # devmem 0x030011B0 32 6
        # devmem 0x030011A4 32 6
        # devmem 0x030011A8 32 6
        # spidev_test -D /dev/spidev0.0 -p 1234 -v
        spi mode: 0x0
        bits per word: 8
        max speed: 500000 Hz (500 kHz)
        TX | 31 32 33 34 __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __  |1234|
        RX | 31 32 33 34 __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __  |1234|
        */

        // CH347
        mmio_write_32(0x03001068, 0x03); // XGPIOA[18] GPIO_MODE
        mmio_write_32(0x03001064, 0x03); // XGPIOA[19] GPIO_MODE
        mmio_write_32(0x03001070, 0x03); // XGPIOA[28] GPIO_MODE

        // uint32_t ddr = mmio_read_32(0x03020004);
        // ddr |= (1 << 19); // XGPIOA[19] output
        // mmio_write_32(0x03020004, ddr);
        // val = mmio_read_32(0x03020000);  // GPIO0
        // val |= (1 << 19);                // XGPIOA[19] HIGH
        // mmio_write_32(0x03020000, val);

        // ddr = mmio_read_32(0x03020004);
        // ddr |= (1 << 28); // XGPIOA[28] output
        // mmio_write_32(0x03020004, ddr);
        // val = mmio_read_32(0x03020000);  // GPIO0
        // val |= (1 << 28);                // XGPIOA[28] HIGH
        // mmio_write_32(0x03020000, val);

        // ddr = mmio_read_32(0x03020004);
        // ddr |= (1 << 18); // XGPIOA[18] output
        // mmio_write_32(0x03020004, ddr);
        // val = mmio_read_32(0x03020000);  // GPIO0
        // val |= (1 << 18);                // XGPIOA[18] HIGH
        // mmio_write_32(0x03020000, val);

        // wait hardware bootup
        suck_loop(50);
	user_led_off();
        return 0;
}
