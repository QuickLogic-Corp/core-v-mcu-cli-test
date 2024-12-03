
#include "target/core-v-mcu/include/core-v-mcu-config.h"
#include "target/core-v-mcu/include/core-v-mcu-system.h"
#include "hal/include/hal_timer_irq.h"
#include "hal/include/hal_fll.h"
#include "hal/include/hal_irq.h"
#include "drivers/include/udma_uart_driver.h"
#include "hal/include/hal_udma_i2cm_reg_defs.h"
#include <drivers/include/udma_i2cm_driver.h>
#include "hal/include/adv_timer_unit_reg_defs.h"

#include "drivers/include/himax.h"
#include "drivers/include/camera.h"
#include <stdarg.h>
#include <stdlib.h>
#include <stdio.h>

#define NUM_OF_OF_CAMERA_DATA_BITS_1	1
#define NUM_OF_OF_CAMERA_DATA_BITS_4	4
#define NUM_OF_OF_CAMERA_DATA_BITS_8	8

static uint8_t cam;
char gsPrintfBuffer[256] = {0};
uint8_t gDebugEnabledFlg = 0;

uint8_t camera_present = 0;

/* Prepare hardware to run the demo. */
static void prvSetupHardware( void );
void person_detection_task( void *pParameter );

int MicroVsnprintf(char* output, int len, const char* format, va_list args);
int oPrintf(const char* format, ...);
void CLI_printf( uint8_t aUartPortNum, const char *fmt, ... );
void _himaxRegWrite(unsigned int addr, unsigned char value);
void genHimaxCamClock(void);
void cam_interface_init (uint16_t x, uint16_t y, uint8_t aBitMode);

uint8_t gNumOfCamDataBits = NUM_OF_OF_CAMERA_DATA_BITS_4;

int main(void)
{
	volatile int i = 0;
	uint16_t retval = 0;
	cam = 0x48; // Himax address
    prvSetupHardware();

    _himaxRegWrite(SW_RESET, HIMAX_RESET);

    for(i=0;i<99900;i++);

    i = 0;
    udma_i2cm_16read8(0, cam, MODEL_ID_H, 2, &retval, 0);
	retval = (retval >> 8) & 0xff | (retval <<8);

	CLI_printf(0, "0x%04x\n",retval);

	if( retval == 0x01b0 )
	{
		for(i=0; i<(sizeof(himaxRegInit)/sizeof(reg_cfg_t)); i++) {
			if( himaxRegInit[i].addr == 0x3059 )
			{
				if( gNumOfCamDataBits == NUM_OF_OF_CAMERA_DATA_BITS_1 )
				{
					_himaxRegWrite(himaxRegInit[i].addr, 0x60);
				}
				else if( gNumOfCamDataBits == NUM_OF_OF_CAMERA_DATA_BITS_4 )
				{
					_himaxRegWrite(himaxRegInit[i].addr, 0x40);
				}
				else if( gNumOfCamDataBits == NUM_OF_OF_CAMERA_DATA_BITS_8 )
				{
					_himaxRegWrite(himaxRegInit[i].addr, 0x02);
				}
			}
			else
				_himaxRegWrite(himaxRegInit[i].addr, himaxRegInit[i].data);
		}
		cam_interface_init(PICTURE_X_SIZE,PICTURE_Y_SIZE, gNumOfCamDataBits);
		genHimaxCamClock();
		//CLI_printf(1,"ScReEn320\n");
		camera_present = 1;
	}

    person_detection_task(0);

	while(1)
	{
		;
	}
}
/*-----------------------------------------------------------*/

static void prvSetupHardware( void )
{
	/* Init board hardware. */
	system_init();
}
/*-----------------------------------------------------------*/

void vToggleLED( void )
{
	gpio_pin_toggle( 0x5 );
}
/*-----------------------------------------------------------*/
void CLI_Puts(uint8_t aPortNum, const char* s)
{
	uint8_t lChar = 0;
	uint8_t lCR = 0x0D;
	while(*s != '\0')
	{
		lChar = *s;
		if( lChar == '\n')
			udma_uart_writeraw(aPortNum,1,&lCR);
		udma_uart_writeraw(aPortNum,1,&lChar);
		s++;
	}
}

void DebugLog(const char* s)
{
	CLI_Puts(0,s);
}

/*
 * Use oPrintf to print debug messages as it can be turned off with gDebugEnabledFlg
 *
 *
 * */
int oPrintf(const char* format, ...)
{
	int lRetVal = 0;
	if( gDebugEnabledFlg == 0 )
		return 0;
	va_list args;
	va_start(args, format);
	lRetVal = MicroVsnprintf(gsPrintfBuffer, 256, format, args);
	DebugLog(gsPrintfBuffer);
	va_end(args);
	return lRetVal;
}

/* workhorse for printf() */
void CLI_vprintf(uint8_t aUartPortNum, const char *fmt, va_list ap )
{
    vsnprintf( gsPrintfBuffer, sizeof(gsPrintfBuffer)-1, fmt, ap );
    gsPrintfBuffer[ sizeof(gsPrintfBuffer) - 1 ] = 0;
    CLI_Puts(aUartPortNum,gsPrintfBuffer);
}

/*
 * Use CLI_printf to print normal messages to indicate to user and not for debug
 *
 *
 * */
/* printf for test purposes */
void CLI_printf(uint8_t aUartPortNum, const char *fmt, ... )
{
    va_list ap;

    va_start(ap,fmt);
    CLI_vprintf(aUartPortNum, fmt, ap );
    va_end(ap);
}

void _himaxRegWrite(unsigned int addr, unsigned char value)
{
	uint8_t naddr;
	uint16_t data;
	naddr = (addr>>8) & 0xff;
	data = (value << 8) | (addr & 0xff);
	udma_i2cm_write (0, cam, naddr, 2, &data, 0);
   //     i2c_16write8(cam,addr,value);
}



void genHimaxCamClock(void)
{
	AdvTimerUnit_t *adv_timer;

	hal_setpinmux(11, 1); 	//apbio47 (IO_11) Set the led to different IO
	hal_setpinmux(26, 0); 	//apbio32 (IO_26) JC8 is the clock input for camera module

	adv_timer = (AdvTimerUnit_t*) ADV_TIMER_START_ADDR;
	adv_timer->timer_0_cmd_register = 1 << REG_TIMER_0_CMD_REGISTER_RESET_COMMAND_LSB; // reset
	adv_timer->timer_0_config_register = 0; // FLL, up/done no prescaler
	adv_timer->timer_0_threshold_register = 0x20000;
	adv_timer->timer_0_threshold_channel_0_reg = 0x30001;
	adv_timer->adv_timer_cfg_register = 0x1; // enable clock for timer0
	adv_timer->timer_0_cmd_register = 1 << REG_TIMER_0_CMD_REGISTER_START_COMMAND_LSB; //start

}

