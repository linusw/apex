/* clcdc-companion.c

   written by Marc Singer
   12 Oct 2006

   Copyright (C) 2006 Marc Singer

   -----------
   DESCRIPTION
   -----------


   Initialization of the TD035TTEA1 from the programming guide for the
   panel.

   o Vdd valid
   o < 1ms -> Vddi valid
   o > 100ns -> XRES goes high
   o > 5ms -> RGB valid
   o > 20ms -> SPI initialization

   The Toshiba controller chip describes other paramters.

   o Reset release time is 1ms.

*/

#include <config.h>
#include <apex.h>
#include <asm/bootstrap.h>
#include "hardware.h"

#include <debug_ll.h>


#define SSP_CR0		__REG(SSP_PHYS + 0x00)
#define SSP_CR1		__REG(SSP_PHYS + 0x04)
#define SSP_IIR		__REG(SSP_PHYS + 0x08)
#define SSP_ROEOI	__REG(SSP_PHYS + 0x08)
#define SSP_DR		__REG(SSP_PHYS + 0x0c)
#define SSP_CPR		__REG(SSP_PHYS + 0x10)
#define SSP_SR		__REG(SSP_PHYS + 0x14)

#define SSP_CR0_SCR_SHIFT (8)
#define SSP_CR0_SSE	  (1<<7)
#define SSP_CR0_FRF_SHIFT (4)
#define SSP_CR0_FRF_SPI	  (0<<SSP_CR0_FRF_SHIFT)
#define SSP_CR0_DSS_SHIFT (0)
#define SSP_CR0_DSS(b)	  ((b)-1)

#define SSP_SR_RFF	(1<<8)	/* Receive FIFO full */
#define SSP_SR_TFE	(1<<7)	/* Transmit FIFO full */
#define SSP_SR_ROR	(1<<6)	/* Receive overrun */
#define SSP_SR_RHF	(1<<5)	/* Receive FIFO half full */
#define SSP_SR_THE	(1<<4)	/* Transmit FIFO half empty */
#define SSP_SR_BSY	(1<<3)	/* Receive or transmit active */
#define SSP_SR_RNE	(1<<2)	/* Receive FIFO not empty */
#define SSP_SR_TNF	(1<<1)	/* Transmit FIFO not full */

static inline void cs_enable (void)
{
  GPIO_PGD &= ~(1<<5);		/* Enable nLCD_SPI_GPIO_CS */
}

static inline void cs_disable (void)
{
  GPIO_PGD |= (1<<5);		/* Disable nLCD_SPI_GPIO_CS */
}

#if 0
static void write_fifo (const char* rgb, int cb)
{
  int cmddata = 0;		/* Cmd/Data bit */
  int bits = 0;			/* Count of bits in accumulator */
  unsigned long accumulator = 0;

  for (; cb; --cb) {
    accumulator = (accumulator << 1) | cmddata;
    cmddata = 1;		/* Reset of bytes are data */
    ++bits;
//    printf ("%s: 0x%02x\n", __FUNCTION__, *rgb);
    accumulator = (accumulator << 8) | *rgb++;
    bits += 8;

    if (bits >= 16) {
      printf ("%s: 0x%04lx %d\n", __FUNCTION__,
	      (accumulator >> (bits - 16)) & 0xffff, 16);
      SSP_DR = (accumulator >> (bits - 16)) & 0xffff;
      bits -= 16;
    }
  }
  if (bits) {
    printf ("%s: 0x%04lx %d (%x)\n", __FUNCTION__,
	    (accumulator << (16 - bits))
	    & (((1<<bits) - 1) << (16 - bits)), bits,
	    (((1<<bits) - 1) << (16 - bits)));
    SSP_DR = (accumulator << (16 - bits))
      & (((1<<bits) - 1) << (16 - bits));
  }
}
#endif

static void write_fifo_9 (const char* rgb, int cb)
{
  int cmddata = 0;		/* Cmd/Data bit */

  while (cb--) {
    while (!(SSP_SR & SSP_SR_TNF))	/* Wait for room in FIFO */
      ;
//    printf ("(0x%x) ", *rgb | (cmddata << 8));
    SSP_DR = *rgb++ | (cmddata << 8);
    cmddata = 1;
  }
}

static void spi_read_flush (void)
{
  while (SSP_SR & SSP_SR_RNE)
    SSP_DR;
}

static void spi_write (const char* rgb, int cb)
{
  cs_enable ();
//  mdelay (1);

  write_fifo_9 (rgb, cb);

  while (SSP_SR & SSP_SR_BSY)
    ;

//  mdelay (1);

  cs_disable ();
//  mdelay (1);
  spi_read_flush ();
}

void companion_clcdc_setup (void)
{
  printf ("%s\n", __FUNCTION__);

  /* *** FIXME: this is a work-around for a problem with the EVT2
     *** schematic.  The DDS chip, when powered off, pulls the SPI
     *** clock line low, preventing this code from initializing the
     *** LCD panel.  By powering up the DDS chip (via the MODEM_GATE),
     *** we prevent the DDS chip from interfering with the SPI
     *** clock. */

//  GPIO_PCDD &= ~(1<<7);		// nCM_ECM_RESET output
  GPIO_PGDD &= ~(1<<0);		// MODEM_GATE output

  //  GPIO_PCD  &= ~(1<<7);		// nCM_ECM_RESET -> LOW
  GPIO_PGD  &= ~(1<<0);		// MODEM_GATE -> LOW  (Power Off)
  GPIO_PGD  |=  (1<<0);		// MODEM_GATE -> HIGH (Power On)
  usleep (20*1000);		// Wait 500ms
//  GPIO_PCD  |=  (1<<7);		// nCM_ECM_RESET -> HIGH

  cs_disable ();
  GPIO_PGDD &= ~(1<<5);		/* Force to output */

//  SSP_CPR = 2;			/* /2 for 7.3728 MHz master clock*/
  SSP_CPR = 74;			/* /74 for 99KHz clock*/

  SSP_CR1 = 0
    | (1<<6)			/* FEN: FIFO enable  */
    | (0<<4)			/* SPH: Framing high for one SSPCLK period */
    | (0<<3)			/* SPO */
    ;

  SSP_CR0 = 0
    | (0<<4)			/* SPI frame format  */
    | SSP_CR0_SSE
    | (0<<8)			/* SCR == 0  */
//    | SSP_CR0_DSS (16)		/* 16 bit frame format */
    | SSP_CR0_DSS (9)		/* 9 bit frame format */
    ;

	/* Perform main setup of the LCD panel controller */
//  mdelay (1);		/* Paranoid */
  spi_write ("\xb0\x02",     2);  /* Blanking period: Use DE */
  spi_write ("\xb4\x01",     2);  /* Display mode */
  spi_write ("\x36\x08",     2);  /* Memory access control: BGR mode */
  spi_write ("\xb7\x03",     2);  /* DCCK & DCEV timing setup */
  spi_write ("\xbe\x38",     2);  /* ASW signal slew rate adjustment */
  spi_write ("\xc0\x08\x08", 3);  /* CKV1,2 timing control */
  spi_write ("\xc2\x18\x18", 3);  /* OEV timing control */
  spi_write ("\xc4\x30\x30", 3);  /* ASW timing control */
  spi_write ("\xc5\x0c",     2);  /* ASW timing control */
  spi_write ("\xed\x04",     2);  /* Valid display lines: 256 */
  spi_write ("\x26\x04",     2);  /* Gamma set */
  spi_write ("\xba\x45",     2);  /* Booster operation setup */
  spi_write ("\xd6\x77\x35", 3);  /* gamma 3 (2) fine tuning */
  spi_write ("\xd7\x01",     2);  /* Gamma 3 (1) fine tuning */
  spi_write ("\xd8\x00",     2);  /* Gamma 3 inclination adjustment */
  spi_write ("\xd9\x00",     2);  /* Gamma 3 blue offset adjustment */

  GPIO_PGD  &= ~(1<<0);		// MODEM_GATE -> LOW  (Power Off)
}

void companion_clcdc_wake (void)
{
  printf ("%s\n", __FUNCTION__);
  spi_write ("\x11", 1);
  /* 6 frames (2.7ms), one frame ~450us */
  mdelay (10);
  spi_write ("\x29", 1);
}

void companion_clcdc_sleep (void)
{
  printf ("%s\n", __FUNCTION__);
  spi_write ("\x28", 1);
  spi_write ("\x10", 1);
//  mdelay (1);
}
