/*
 ============================================================================
 Name        : main.c
 Author      : 
 Version     :
 Copyright   : Your copyright notice
 Description : Hello RISC-V World in C
 ============================================================================
 */

#include <stdio.h>

/*
 * Demonstrate how to print a greeting message on standard output
 * and exit.
 *
 * WARNING: This is a build-only project. Do not try to run it on a
 * physical board, since it lacks the device specific startup.
 *
 * If semihosting is not available, use `--specs=nosys.specs` during link.
 */
#include "core-v-mcu-config.h"
#include <hal_apb_soc_ctrl_reg_defs.h>
#include <string.h>
#include "../include/flash.h"
#include "../include/dbg.h"

uint16_t udma_uart_open (uint8_t uart_id, uint32_t xbaudrate);
uint16_t udma_uart_writeraw(uint8_t uart_id, uint16_t write_len, uint8_t* write_buffer) ;



#define PLP_L2_DATA      __attribute__((section(".ram")))

PLP_L2_DATA static boot_code_t    bootCode;

static void load_section(boot_code_t *data, flash_v2_mem_area_t *area) {
  unsigned int flash_addr = area->start;
  unsigned int area_addr = area->ptr;
  unsigned int size = area->size;
  unsigned int i;

  int isL2Section = area_addr >= 0x1C000000 && area_addr < 0x1D000000;

  for (i = 0; i < area->blocks; i++) { // 4KB blocks

    unsigned int iterSize = data->blockSize;
    if (iterSize > size) iterSize = (size + 3) & 0xfffffffc;

    if (isL2Section) {
      udma_flash_read(flash_addr, area_addr, iterSize);
    } else {
      udma_flash_read(flash_addr, (unsigned int)(long)data->flashBuffer, iterSize);
      memcpy((void *)(long)area_addr, (void *)(long)data->flashBuffer, iterSize);
    }

    area_addr  += iterSize;
    flash_addr += iterSize;
    size       -= iterSize;
  }

}

static inline void __attribute__((noreturn)) jump_to_address(unsigned int address) {
  void (*entry)() = (void (*)())((long)address);
  entry();
  while(1);
}

static inline void __attribute__((noreturn)) jump_to_entry(flash_v2_header_t *header) {

  //apb_soc_bootaddr_set(header->bootaddr);
  jump_to_address(header->entry);
  while(1);
}

__attribute__((noreturn)) void changeStack(boot_code_t *data, unsigned int entry, unsigned int stack);

static void getMemAreas(boot_code_t *data)
{
  udma_flash_read(0, (unsigned int)(long)&data->header, sizeof(data->header));

  int nbArea = data->header.nbAreas;
  if (nbArea >= MAX_NB_AREA) {
    nbArea = MAX_NB_AREA;
  }

  if (nbArea) udma_flash_read(sizeof(flash_v2_header_t), (unsigned int)(long)data->memArea, nbArea*sizeof(flash_v2_mem_area_t));
}

static __attribute__((noreturn)) void loadBinaryAndStart(boot_code_t *data)
{

  getMemAreas(data);

  unsigned int i;
  for (i=0; i<data->header.nbAreas; i++) {
	char string[32];
	dbg_str("\nLoading Section ");
	dbg_hex32(i);
	dbg_str(" to ");
	dbg_hex32(data->memArea[i].ptr);
    load_section(data, &data->memArea[i]);
  }
  dbg_str("\nJumping to ");
  dbg_hex32(data->header.entry);
  jump_to_entry(&data->header);
}

static __attribute__((noreturn)) void loadBinaryAndStart_newStack(boot_code_t *data)
{
  changeStack(data, (unsigned int)(long)loadBinaryAndStart, ((unsigned int)(long)data->stack) + BOOT_STACK_SIZE);
}


static boot_code_t *findDataFit(boot_code_t *data)
{
  unsigned int addr = 0x1c000000;
  unsigned int i;

  for (i=0; i<data->header.nbAreas; i++) {
    flash_v2_mem_area_t *area = &data->memArea[i];
    if ((addr >= area->ptr && addr < area->ptr + area->size)
      || (addr < area->ptr && addr + sizeof(boot_code_t) > area->ptr)) {
	addr = ((area->ptr + area->size) + data->blockSize - 1) & ~(data->blockSize - 1);
      }
  }
  return (boot_code_t *)(long)addr;
}

static void bootFromRom(int hyperflash, int qpi)
{
  boot_code_t *data = &bootCode;

  data->hyperflash = hyperflash;
  data->step = 0;
  if (hyperflash) data->blockSize = HYPER_FLASH_BLOCK_SIZE;
  else data->blockSize = FLASH_BLOCK_SIZE;
  data->qpi = qpi;

  getMemAreas(data);

  boot_code_t *newData = findDataFit(data);
  newData->hyperflash = hyperflash;
  newData->qpi = qpi;
  if (hyperflash) newData->blockSize = HYPER_FLASH_BLOCK_SIZE;
  else newData->blockSize = FLASH_BLOCK_SIZE;

  loadBinaryAndStart_newStack(newData);

}


int main(void)
{
 int id = 1;
 unsigned int bootsel, flash_present;
 char tstring[8];

 volatile SocCtrl_t* psoc = (SocCtrl_t*)SOC_CTRL_START_ADDR;
 udma_uart_open (id,115200);
 dbg_str("\nA2 Bootloader Bootsel=");
 bootsel = psoc->bootsel & 0x1;
 if (bootsel == 1) dbg_str("1");
 else dbg_str("0");
 udma_qspim_open(0, 1000000);
 udma_flash_readid(tstring);
 if (tstring[0] == 0x20) flash_present = 1;
 else flash_present = 0;
 if (bootsel == 0)
	 tstring[0] = '.';
 else if (flash_present == 0)
	 tstring[0] = '!';
 tstring[1] = 0;
 if ((bootsel == 1) && (flash_present == 1)) { //boot from SPI flash
	 bootFromRom(0,0);
 } else
 while (1) {
	 for (bootsel = 0; bootsel < 1000000; bootsel++);
	 dbg_str(tstring);
 }

}
