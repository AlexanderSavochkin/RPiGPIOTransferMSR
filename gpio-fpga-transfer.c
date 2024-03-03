/* -*- mode: c; c-basic-offset: 2; indent-tabs-mode: nil; -*-
 * Copyright (c) 2015, Henner Zeller <h.zeller@acm.org>
 * This is provided as-is to the public domain.
 *
 * This is not meant to be useful code, just an attempt
 * to understand GPIO performance if sent with DMA (which is: too low).
 * It might serve as an educational example though.
 */

#include <assert.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

// Physical Memory Allocation, from raspberrypi/userland demo.
#include "mailbox.h"

// GPIO which we want to toggle in this example.
#define TOGGLE_GPIO 14

// Raspberry Pi 2 or 1 ? Since this is a simple example, we don't
// bother auto-detecting but have it a compile-time option.
#ifndef PI_VERSION
#  define PI_VERSION 4
#endif

#define BCM2708_PI1_PERI_BASE  0x20000000
#define BCM2709_PI2_PERI_BASE  0x3F000000
#define BCM2711_PI4_PERI_BASE  0xFE000000

// --- General, Pi-specific setup.
#if PI_VERSION == 1
#  define PERI_BASE BCM2708_PI1_PERI_BASE
#elif PI_VERSION == 2 || PI_VERSION == 3
#  define PERI_BASE BCM2709_PI2_PERI_BASE
#else
#  define PERI_BASE BCM2711_PI4_PERI_BASE
#endif

#define PAGE_SIZE 4096

// ---- GPIO specific defines
#define GPIO_REGISTER_BASE 0x200000
#define GPIO_FSEL0_OFFSET 0x00  // Function select register
                               // 3 bits per pin: 000=input, 001=output
                               // 100=alt0, 101=alt1, 110=alt2, 111=alt3, 011=alt4
                               // 010=alt5
#define GPIO_FSEL1_OFFSET 0x04  // Function select register
#define GPIO_FSEL2_OFFSET 0x08  // Function select register
                                
#define GPIO_SET_OFFSET 0x1C   //Output set register
#define GPIO_CLR_OFFSET 0x28   //Output clear register
#define GPIO_LEV_OFFSET 0x34   //Input level register
#define PHYSICAL_GPIO_BUS (0x7E000000 + GPIO_REGISTER_BASE)

// ---- Memory mappping defines
#define BUS_TO_PHYS(x) ((x)&~0xC0000000)

// ---- Memory allocating defines
// https://github.com/raspberrypi/firmware/wiki/Mailbox-property-interface
#define MEM_FLAG_DIRECT           (1 << 2)
#define MEM_FLAG_COHERENT         (2 << 2)
#define MEM_FLAG_L1_NONALLOCATING (MEM_FLAG_DIRECT | MEM_FLAG_COHERENT)

// ---- DMA specific defines
#define DMA_CHANNEL       5   // That usually is free.
#define DMA_BASE          0x007000

// BCM2385 ARM Peripherals 4.2.1.2
#define DMA_CB_TI_NO_WIDE_BURSTS (1<<26)
#define DMA_CB_TI_SRC_INC        (1<<8)
#define DMA_CB_TI_DEST_INC       (1<<4)
#define DMA_CB_TI_TDMODE         (1<<1)

#define DMA_CS_RESET    (1<<31)
#define DMA_CS_ABORT    (1<<30)
#define DMA_CS_DISDEBUG (1<<28)
#define DMA_CS_END      (1<<1)
#define DMA_CS_ACTIVE   (1<<0)

#define DMA_CB_TXFR_LEN_YLENGTH(y) (((y-1)&0x4fff) << 16)
#define DMA_CB_TXFR_LEN_XLENGTH(x) ((x)&0xffff)
#define DMA_CB_STRIDE_D_STRIDE(x)  (((x)&0xffff) << 16)
#define DMA_CB_STRIDE_S_STRIDE(x)  ((x)&0xffff)

#define DMA_CS_PRIORITY(x) ((x)&0xf << 16)
#define DMA_CS_PANIC_PRIORITY(x) ((x)&0xf << 20)



static int mbox_fd = -1;   // used internally by the UncachedMemBlock-functions.


// Return a pointer to a periphery subsystem register.
static void *mmap_bcm_register(off_t register_offset) {
  const off_t base = PERI_BASE;

  int mem_fd;
  if ((mem_fd = open("/dev/mem", O_RDWR|O_SYNC) ) < 0) {
    perror("can't open /dev/mem: ");
    fprintf(stderr, "You need to run this as root!\n");
    return NULL;
  }

  uint32_t *result =
    (uint32_t*) mmap(NULL,                  // Any adddress in our space will do
                     PAGE_SIZE,
                     PROT_READ|PROT_WRITE,  // Enable r/w on GPIO registers.
                     MAP_SHARED,
                     mem_fd,                // File to map
                     base + register_offset // Offset to bcm register
                     );
  close(mem_fd);

  if (result == MAP_FAILED) {
    fprintf(stderr, "mmap error %p\n", result);
    return NULL;
  }
  return result;
}

void initialize_gpio_for_output(volatile uint32_t *gpio_registerset, int bit) {
  //AlexSavo: Looks like the logic here:
  // 1. Each pin has 3 bits in the FSEL register, so 32-bit register can handle 10 pins (+2 extra bits, probably unused)
  //     That's why the offset is bit/10, and the shift is (bit%10)*3
  // 2. 7 is 111 in binary, so we clear the 3 bits for the pin we want to set as output
  // 3. 1 is 001 in binary, so we set the 3 bits for the pin we want to set as output as 001
  //
  *(gpio_registerset+(bit/10)) &= ~(7<<((bit%10)*3));  // prepare: set as input
  *(gpio_registerset+(bit/10)) |=  (1<<((bit%10)*3));  // set as output.
}

void initialize_gpio_for_input(volatile uint32_t *gpio_registerset, int bit) {
  //AlexSavo: Looks like the logic here:
  // 1. Each pin has 3 bits in the FSEL register, so 32-bit register can handle 10 pins (+2 extra bits, probably unused)
  //     That's why the offset is bit/10, and the shift is (bit%10)*3
  // 2. 7 is 111 in binary, so we clear the 3 bits for the pin we want to set as input
  *(gpio_registerset+(bit/10)) &= ~(7<<((bit%10)*3));  // set as input
}


void poll_data_from_gpio(uint32_t *buffer, uint32_t size) {
    // Prepare GPIO
    volatile uint32_t *gpio_port = mmap_bcm_register(GPIO_REGISTER_BASE);


    //Set GPIO0-GPIO23 as input
    // GPIO0-GPIO23 are the data lines
    for (int i = 0; i < 24; i++) {
        initialize_gpio_for_input(gpio_port, i);
    }

    // GPIO25 is the DATA_REQUEST signal to the device (output)
    initialize_gpio_for_output(gpio_port, 25);
    //Set GPIO26 as output (RESET signal to the device)
    initialize_gpio_for_output(gpio_port, 26);
    // GPIO27 is the DATA_READY signal from the device
    initialize_gpio_for_input(gpio_port, 27);


    //Rest the device
    *(gpio_port + (GPIO_SET_OFFSET / sizeof(uint32_t))) = (1<<26);
    usleep(1);
    *(gpio_port + (GPIO_CLR_OFFSET / sizeof(uint32_t))) = (1<<26);

    for (int i = 0; i < size; ++i) {
        //Set the DATA_REQUEST signal to the device
        *(gpio_port + (GPIO_SET_OFFSET / sizeof(uint32_t))) = (1<<25);

        //Wait for the DATA_READY signal from the device
        while((*(gpio_port + (GPIO_LEV_OFFSET / sizeof(uint32_t))) & (1<<24)) == 0);

        //Read the data from the device
        buffer[i] = *(gpio_port + (GPIO_LEV_OFFSET / sizeof(uint32_t))) & 0xFFFFFF;

        //Clear the DATA_REQUEST signal to the device
        *(gpio_port + (GPIO_CLR_OFFSET / sizeof(uint32_t))) = (1<<25);

        //Wait for the DATA_READY signal from the device to be cleared
        while((*(gpio_port + (GPIO_LEV_OFFSET / sizeof(uint32_t))) & (1<<24)) != 0);
    }
}


int main(int argc, char *argv[]) {
    //Reading 500Mb of data from the GPIO
    uint32_t *buffer = (uint32_t*) malloc(500000000);
    poll_data_from_gpio(buffer, 500000000);

    // Write to file
    FILE *file = fopen("data.bin", "wb");
    fwrite(buffer, 1, 500000000, file);
    fclose(file);

    return 0;
}
