/*
    Copyright (C) 2018  Dr. Jürgen Sauermann

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

/*
 top-level functions for the OmFLA device
 */

#include "user_defined_parameters.hh"

// NOTE: 4313 Fuse settings:
//
// Extended Fuse: 0xFF (default)
// High Fuse:     0xDF (default)
// Low Fuse:      0x62 (default)
//

// the macro MODE defines if:
//
// * an ENOCEAN TCM 310 is installed on the PCB,
// * the UART is used at all (TCM 310 or debug output), and
// * formatted print functions are needed.
//
// This is needed so that the entire program fits into the 4k flash memory
// of an Atmel 4313 chip.
//
#if       MODE == 0    // debug output on TxD
#  define ENOCEAN      0
#  define HARD_UART    0
#  define SOFT_UART    1
#  define FANCY_PRINT  1
#  define EEPROM_CACHE 0
#elif     MODE == 1    // TxD connected to TCM 310
# define  ENOCEAN      1
# define  HARD_UART    1
# define SOFT_UART     0
# define  FANCY_PRINT  0
#  define EEPROM_CACHE 1
#elif     MODE == 2    // TxD not used
# define  ENOCEAN      0
# define  HARD_UART    0
# define SOFT_UART     0
# define  FANCY_PRINT  0
# define EEPROM_CACHE  1
#else                  // illegal
# error "MODE must be 0, 1, or 2 !"
#endif

#if HARD_UART
# define F_CPU 3686400      // with Xtal or calibration to 3.3634 MHz
# define  CPU_CALIB 0x3B
#else
# define F_CPU 4000000      // with internal 4.0 MHz RC oscillator
# define  CPU_CALIB 0x26
#endif

#if SOFT_UART
# define SOFT_BAUD 57600
# define SOFT_PORT_1 PORTB
# define SOFT_PBIT_1 B_PROG_MISO
# define SOFT_PORT_2 PORTD
# define SOFT_PBIT_2 D_TCM_TxD

enum CPU_cycles
   {
     DELAY_LOOP_LEN = 3,   // one iteration of _delay_loop_1()
     DELAY_PREAMBLE = 13,   // set/clear bit etc
                                                         // 9600   57600
     CYCLES_PER_BIT = F_CPU / SOFT_BAUD,                 //  384      64
     DELAY_PER_BIT  = CYCLES_PER_BIT - DELAY_PREAMBLE,   //  371      51
     SOFT_COUNT     = DELAY_PER_BIT/DELAY_LOOP_LEN,      //  123      17
   };

static uint16_t soft_count = SOFT_COUNT;

#endif

enum EERPROM_addresses
{
   USER_PARAMS  = 0,      // copy of user_params (14 bytes)
   SENSOR_Cache = 0x20,   // blocks 4..14 of the sensor (11 byte per block)
};

static uint16_t cache = 0;
#define F_IO  F_CPU

#include <avr/io.h>
#include <avr/eeprom.h>
#include <avr/interrupt.h>
#include <avr/pgmspace.h>
#include <avr/sleep.h>
#include <util/delay.h>

uint16_t pass = 0;

#if EEPROM_CACHE
static void
write_cache(uint8_t value)
{
   if (pass > 1)   eeprom_update_byte((uint8_t *)cache++, value);
}
#else
# define write_cache(value)
#endif

/// a 1:1 copy of the EEPROM data, initialized at startup
User_defined_parameters user_params;

#ifndef __AVR_ATtiny4313__
# error "__AVR_ATtiny4313__ is not defined !!!"
#endif

#if FANCY_PRINT
# define print_string(str)       _print_string(PSTR((str)), -1)
# define print_stringv(str, val) _print_string(PSTR((str)), val)
# define print_char(x)           _print_char(x)
# define print_byte(x)           FIXME!
# define error(str)              _error(PSTR(str), __LINE__, -1);
# define errorv(str, val)        _error(PSTR(str), __LINE__, val);
#else
# define print_string(str)
# define print_stringv(str, val)
# define print_char(x)
# define print_byte(x)           _print_char(x)
# define error(str)
# define errorv(str, val)
#endif

#define STR(x) #x
#define CATN(x, n) x STR(n)

#if ENOCEAN
static void disable_enocean();
static void transmit_glucose(uint8_t gluco_2);
static void transmit_block(uint8_t block);
static uint8_t crc = 0;
#else
# define disable_enocean()
# define  transmit_glucose(gluco_2)
#  define transmit_block(block)
#endif

#define get_pin(port, bit)    (PIN  ## port &    port ## _ ## bit ? 0xFF : 0x00)
#define set_pin(port, bit)    (PORT ## port |=   port ## _ ## bit)
#define clr_pin(port, bit)    (PORT ## port &= ~ port ## _ ## bit)
#define output_pin(port, bit) (DDR  ## port |=   port ## _ ## bit)
#define input_pin(port, bit)  (DDR  ## port &= ~ port ## _ ## bit)

enum IO_pins
{
   __A_XTAL_1  = 1 << PA0,   // XTAL1 (not used)
   __A_XTAL_2  = 1 << PA1,   // XTAL2 (not used)
   __A_RESET   = 1 << PA2,   // RESET (NEVER drive it  high!)
   outputs_A   = __A_XTAL_1
               | __A_XTAL_2,
   pullup_A  = 0,

   B_BTEST_OUT = 1 << PB0,   // battery test output
   B_BTEST_IN  = 1 << PB1,   // battery test input
   B_MOSI      = 1 << PB2,   // MOSI to RFID reader
   B_TCM_POWER = 1 << PB3,   // Power for TCM 310 radio module
   B_LED_GREEN = 1 << PB4,   // green LED
   B_PROG_MOSI = 1 << PB5,   // MOSI from serial programmer
   B_PROG_MISO = 1 << PB6,   // MISO to   serial programmer (not used)
   B_PROG_SCL  = 1 << PB7,   // SCL  from serial programmer, -EN for RFID

#if HARD_UART
   outputs_B   = B_BTEST_OUT
               | B_MOSI
               | B_TCM_POWER
               | B_LED_GREEN
               | B_PROG_MOSI
  ///          | __PROG_MISO // maybe connected to TxD
               | B_PROG_SCL,
#elif SOFT_UART
   outputs_B   = B_BTEST_OUT
               | B_MOSI
               | B_TCM_POWER
               | B_LED_GREEN
               | B_PROG_MOSI
               | B_PROG_MISO  // soft UART TxD
               | B_PROG_SCL,
#else
   outputs_B   = B_BTEST_OUT
               | B_MOSI
               | B_TCM_POWER
               | B_LED_GREEN
               | B_PROG_MOSI
               | B_PROG_MISO  // UART txD and PROG_MISO both driven low
               | B_PROG_SCL,
#endif
   pullup_B  = 0,

   D_TCM_RxD   = 1 << PD0,   // RxD from TCM 310 radio module
   D_TCM_TxD   = 1 << PD1,   // TxD to   TCM 310 radio module
   D_SCLK      = 1 << PD2,   // Data clock to RFID reader
   D_SSEL      = 1 << PD3,   // Slave SELect to RFID reader
   D_LED_RED   = 1 << PD4,   // red LED
   D_MISO      = 1 << PD5,   // MISO from RFID reader
   D_BEEPER    = 1 << PD6,   // beeper
   outputs_D   = D_TCM_TxD
               | D_SCLK
               | D_SSEL
               | D_LED_RED
               | D_BEEPER,
   pullup_D  = 0,
};

enum BOARD_STATUS
{
   BSTAT_RESET         = 0,
   BSTAT_RFID_ERROR    = 3,
   BSTAT_RUNNING       = 7,
   BSTAT_ABOVE_INITIAL = 6,
   BSTAT_BELOW_INITIAL = 4,
};

#if HARD_UART || SOFT_UART
static void
_print_char(uint8_t ch)
{
   // update CRC if needed
   //
# if ENOCEAN
   enum { polynom = 0x07 };   // (x^8) + x^2 + x^1 + x^0
   crc ^= ch;
   for (uint8_t i = 0; i < 8; i++)
       {
         const bool crc_high = crc & 0x80;
         crc <<= 1;
         if (crc_high)   crc ^= polynom;
       }
# endif

   // transmit the character
   //
# if HARD_UART
   while (!(UCSRA & 1 << UDRE))   ;   // wait for DR empty
   UDR = ch;
# else   // SOFT_UART
   //
   //             ╔╦═════════════════  stop bit(s)
   //             ║║     ╔╦══════════  8 data bits
   //             ║║     ║║     ╔════  start bit
int16_t bits = (0xFF00 | ch) << 1;

   for (uint8_t j = 0; j < 12; ++j)   // 1 start _ 8 data + 3 stop
       {
         if (bits & 1)
            {
              SOFT_PORT_1 |=  SOFT_PBIT_1;
              SOFT_PORT_2 |=  SOFT_PBIT_2;
            }
         else
            {
              SOFT_PORT_1 &= ~SOFT_PBIT_1;
              SOFT_PORT_2 &= ~SOFT_PBIT_2;
              SOFT_PORT_2 &= ~SOFT_PBIT_2;   // compensate sbrs skew
            }
         bits >>= 1;
         _delay_loop_1(soft_count);
       }

# endif
}
#endif

static uint8_t  board_status = BSTAT_RESET;
static uint8_t  trend_idx = 0;
static uint8_t  hist_idx = 0;

static uint16_t batt_result = 0;
static uint16_t initial_glucose_2 = 0;
static uint8_t  initial_B = 0;

//-----------------------------------------------------------------------------
/// wait for \b milli_secs ms, return time slept (which can be less than
/// requested if \b milli_secs is too large
static uint32_t
sleep_ms(uint32_t milli_secs)
{
   enum {
          // CTC frequencies after prescaler
          //
          PRE_1 = 1,   // prescaler: ÷1
          PRE_2 = 5,   // prescaler: ÷1024

          F_CTC_1   =  F_IO,          // 4 MHz,   Tmax = 16.384 msec
          F_CTC_2   =  F_IO / 1024,   // 3906 Hz, Tmax = 16.777 sec

          WGmode    = 4,              // alias CTC mode, p. 113
          WGmode_A  = (WGmode & 0x03) << WGM10,
          WGmode_B  = (WGmode >> 2)   << WGM12,
        };

   // timer in CRC mode with 20 ms interval
   //
   // WGM13..10 is 0100 p. 113, (split between TCCR1B and TCCR1A)
   TCCR1A = 0 << COM1A0   // normal mode, OC1A disconnected, see p. 111
          | 0 << COM1B0   // normal mode, OC1B disconnected, see p. 111
          | WGmode_A      // WGM11:WGM10 = 00  p. 111
          ;

   if (milli_secs < 16)   // short sleep
      {
        TCCR1B = WGmode_B        // WGM13:WGM12 = 01  p. 113
               | PRE_1 << CS10   // prescaler: io-clk
               ;

        //      (max. <  64,000,000)
        OCR1A = (F_CTC_1 * milli_secs) / 1000;
      }
   else
      {
        if (milli_secs > 16000)   // very long sleep
           {
             milli_secs = 16000;
           }

        TCCR1B = WGmode_B        // WGM13:WGM12 = 01  p. 113
               | PRE_2 << CS10   // prescaler: io-clk
               ;

        //      (max. <  64,000,000)
        OCR1A = (F_CTC_2 * milli_secs) / 1000;
      }

   TCNT1 = 0;
   TIFR  = 1 << OCIE1A;   // clear old interrupts
   TIMSK = 1 << OCIE1A;   // enable interrupts

   set_sleep_mode(SLEEP_MODE_IDLE);

   sleep_enable();
   sei();
   sleep_cpu();
   cli();
   sleep_disable();

   TIMSK = 0;             // disable timer interrupts
   return milli_secs;
}
//-----------------------------------------------------------------------------
#define MAX_FIFO 10

#include "RFID_functions.cc"

#if ENOCEAN
# include "enocean.cc"
#endif
//-----------------------------------------------------------------------------
static void
init_hardware()
{
   // set I/O ports to a preliminary value, which will be overridden below
   //
   DDRA = outputs_A;
   DDRB = outputs_B & ~B_PROG_MOSI;   // make PROG_MOSI an input
   DDRD = outputs_D;

   PORTA = pullup_A;
   PORTB = pullup_B | B_PROG_MOSI | B_TCM_POWER | B_PROG_SCL;
   PORTD = pullup_D | D_BEEPER;

   // CPU clock prescaler: x1
   //
   CLKPR = 0x80;   // enable write to CLKPR
   CLKPR = 0x00;   // x1 clock

   // timer 1 off
   TCCR1A = 0;
   TCCR1B = 0;

#if HARD_UART
   // UART
   //
   enum
      {
        BAUDRATE = 57600,
        F_IO_16 = F_IO/16,
        BAUD_DIVISOR = (F_IO_16 / BAUDRATE) - 1
      };

   UBRRH = BAUD_DIVISOR >> 8;
   UBRRL = BAUD_DIVISOR & 0xFF;

   disable_enocean();   // empty unless ENOCEAN is #defined as well
   UCSRB = 0 << RXEN | 1 << TXEN;
   UCSRC = 1 << USBS | 3 << UCSZ0;   // async, 2 stop, 8 data
#elif SOFT_UART
   SOFT_PORT_1 |= SOFT_PBIT_1;   // set TxD high
   SOFT_PORT_2 |= SOFT_PBIT_2;   // set TxD high
#endif

   // set up pins again AFTER all alternate functions have been enabled...

   initial_B = PINB;
   PORTB = pullup_B;   // disable pullup on PROG_MOSI

   // set data directions, see 10.1.1 Configuring the Pin
   //
   DDRA = outputs_A;
   DDRB = outputs_B;
   DDRD = outputs_D;

   // enable pullups on inputs and drive all outputs except TxD low,
   // see 10.1.1 Configuring the Pin
   //
   PORTA = pullup_A;
   PORTB = pullup_B | B_PROG_SCL;
   PORTD = pullup_D | D_BEEPER;

   // initialize SPI interface to RFID reader
   //
   set_pin(D, SSEL);
   clr_pin(D, SCLK);
   clr_pin(B, MOSI);

   // enable watchdog, prescaler = 0, no interrupts
   //
// WDTCR = 1 << WDE;

   // set analog comparator to inactive state

   ACSR = 1 << ACD     // DO     disable comparator
        | 0 << ACBG    // DO NOT use internal bandgap reference
        | 0 << ACI     // DO NOT clear interrupt
        | 0 << ACIE    // DO NOT enable interrupt
        | 0 << ACIC    // DO NOT enable input capture
        | 2 << ACIS0   // interrupt on falling edge
        ;

   DIDR = 1 << AIN0D    // disable AIN0 digital input
        | 1 << AIN1D;   // disable AIN1 digital input
}
//-----------------------------------------------------------------------------
ISR(TIMER1_COMPA_vect)
{
}
//-----------------------------------------------------------------------------
ISR(ANA_COMP_vect)
{
   batt_result = TCNT1;
}
//-----------------------------------------------------------------------------
static void
print_hex1(uint8_t ch)
{
   ch &= 0x0F;
   ch += (ch <= 9) ? '0' : '7';
   print_char(ch);
}
//-----------------------------------------------------------------------------
static void
print_hex2(uint8_t ch)
{
   print_hex1(ch >> 4);
   print_hex1(ch);
}

#if FANCY_PRINT
#include "print_functions.cc"
#endif

//-----------------------------------------------------------------------------
inline void
beep(uint8_t repeat, uint16_t ms_on, uint8_t ms_off)
{
   if ((initial_B & B_PROG_MOSI) == 0)   return;

// print_stringv("beep \x90\n", ms_on);
   for (int j = 0; j < repeat; ++j)
      {
        clr_pin(D, BEEPER);
        sleep_ms(ms_on);
        set_pin(D, BEEPER);
        sleep_ms(ms_off);
      }
}
//-----------------------------------------------------------------------------
static uint8_t gluco2_vec[15];   // glucose values (divided by 2!)
static uint8_t gluco_idx = 0;

/// store glucose (in mg% ÷ 2) in gluco2_vec
static void
gluco2(uint8_t rx_offset)
{
const uint16_t h = rx_data[rx_offset + 3] & 0x0F;   // upper 4 bits
const uint16_t l = rx_data[rx_offset + 2];          // lower 8 bits
const uint32_t raw_sensor = h << 8 | l;           // total 12 bits
const uint16_t glucose = user_params.sensor_offset
                       + ((raw_sensor * user_params.sensor_slope) / 1000);

   print_stringv("raw[\x90", gluco_idx);
   print_stringv("] = \x90",  raw_sensor);
   print_stringv(" -> \x90 mg%\n",  glucose);

   gluco2_vec[gluco_idx++] = glucose >> 1;
}
//-----------------------------------------------------------------------------
/// sort gluco2_vec (15 items)
void
gluco_sort()
{
   for (uint8_t base = 0; base < (sizeof(gluco2_vec) - 1); ++base)
       {
          uint8_t smallest = base;
          for (uint8_t j = base + 1; j < sizeof(gluco2_vec); ++j)
              {
                if (gluco2_vec[j] < gluco2_vec[smallest])   smallest = j;
              }

         // exchange gluco2_vec[base] and gluco2_vec[smallest]
         //
         const uint8_t base_val = gluco2_vec[base];
         gluco2_vec[base] = gluco2_vec[smallest];
         gluco2_vec[smallest] = base_val;
       }
}
//-----------------------------------------------------------------------------
static bool
decode_Block(uint8_t block)
{
const uint8_t FIFO_len = read_register(FIFO_STATUS) & 0x7F;
   if (FIFO_len > MAX_FIFO)
      {
        errorv(CATN("FIFO_len > ", MAX_FIFO), FIFO_len);
        goto error_out;
      }

   SPI_transfer(fifo, rx_data, FIFO_len + 1);

   // rx_data[0] is the leading SPI dummy, and
   // rx_data[1] is the ISO error code if != 0
   //
   if (FIFO_len == 2 || rx_data[1] != 0)   // ISO error code
      {
        errorv("ISO error ", rx_data[1]);
        goto error_out;
      }

   if (FIFO_len != 9)
      {
        errorv("bad FIFO length", FIFO_len);
        goto error_out;
      }

   // the trend table is contained in blocks 4...15
   //
   if (block < 3)     return false;   // OK
   if (block >= 16)   return false;   // OK

   print_stringv("blk \x90  [", block);
   print_stringv("\x90] ", 8*block);

#if !ENOCEAN
   for (int j = 9; j >= 2; --j)   print_hex2(rx_data[j]);
#endif

   if (block == 3)
      {
        hist_idx  = rx_data[5];    // mirrored!
        trend_idx = rx_data[4];    // mirrored!
        print_stringv("  trend_idx: #\x90",  trend_idx);
        print_stringv(", hist_idx: #\x90\n", hist_idx);
        return false;   // OK
      }

#if EEPROM_CACHE
   write_cache(hist_idx);
   write_cache(trend_idx);
   write_cache(block);
   for (int j = 9; j >= 2; --j)   write_cache(rx_data[j]);
#endif

   switch(block % 3)
      {
        case 1: // AAAA-BBBB-CCCC-AAAA
                print_string(" ABCA\n");
                gluco2(2);   // FIFO is mirrored!
                break;

        case 2: // CCCC-AAAA-BBBB-CCCC
                print_string(" CABC\n");
                gluco2(6);   // FIFO is mirrored!
                gluco2(0);   // FIFO is mirrored!
                break;

        case 0: // BBBB-CCCC-AAAA-BBBB
                print_string(" BCAB\n");
                gluco2(4);   // FIFO is mirrored!
      }

   return false;   // OK

error_out:
   beep(3, 200, 100);

   if ((block % 3) == 1)   gluco2_vec[gluco_idx++] = 0;   // 2 values per block
   gluco2_vec[gluco_idx++] = 0;                       // 1 more gluco2_vec
   print_stringv("    failed block: #\x90\n", block);
   return true;   // error
}
//-----------------------------------------------------------------------------
const uint8_t setup[] =
{
    CONT | CHIP_STATE_CONTROL,
    CHIP_STATE_RF_On,   // chip status:
    ISO_MODE,
    0x00,        // ISO_14443B_OPTIONS
    0x00,        // ISO_14443A_OPTIONS
    0xC1,        // TX_TIMER_EPC_HIGH
    0xBB,        // TX_TIMER_EPC_LOW
    0x00,        // TX_PULSE_LENGTH_CONTROL
    0x30,        // RX_NO_RESPONSE_WAIT_TIME
    0x1F,        // RX_WAIT_TIME
    0x01,        // MODULATOR_CONTROL
    0x40,        // RX_SPECIAL_SETTINGS = 424-kHz subcarrier for ISO 15693
    0x03,        // REGULATOR_CONTROL
};
//-----------------------------------------------------------------------------
inline void
setup_chip()
{
   SPI_transfer(setup, 0, sizeof(setup));
   sleep_ms(10);   // > 6 ms
}
//-----------------------------------------------------------------------------
uint8_t iso_read_block[8] =
   {
     CMD_reset_FIFO,
     CMD_send_with_CRC,
     CONT | TX_LENGTH_BYTE_1,   // write continuous from 1D
     0, 0x30,   // length (3)
     ISO_FLAGS, // ISO15693 flags
     0x20,      // ISO Read Single Block command code
     0,         // block number
   };

static bool
read_Block(uint8_t block)
{
   // at this point we expect: FIFO empty and interrupts off.
   //
   {
     const uint8_t len = read_register(FIFO_STATUS);
     if (len)
        {
          errorv("FIFO len > 0", len);
          Reset_FIFO();
        }

     const uint8_t stat = read_ISR();
     if (stat)
        {
          errorv("IRQ_STATUS != 0", stat);
        }
   }

   iso_read_block[sizeof(iso_read_block) - 1] = block;
   SPI_transfer(iso_read_block, 0, sizeof(iso_read_block));

   sleep_ms(30);

const uint8_t istat = read_ISR();
   if ((istat & 0xC0) != 0xC0)
      {
        errorv("missing Rx or Tx Interrupt", istat);
        errorv("block number", block);
        return true;
      }

   return false;
}
//-----------------------------------------------------------------------------
inline void
dump_registers()
{
const uint8_t which[32] =   // do not read ISR status (0x0C) or FIFO (0x1F)
   { 0x00,  0x01,  0x02,  0x03,  0x04,  0x05,  0x06,  0x07,
     0x08,  0x09,  0x0A,  0x0B,  0xFF,  0x0D,  0x0E,  0x0F,
     0x10,  0x11,  0x12,  0x13,  0x14,  0x15,  0x16,  0x17,
     0x18,  0x19,  0x1A,  0x1B,  0x1C,  0x1D,  0x1E,  0xFF };

int values[32];

   // read registers...
   //
   for (uint8_t w = 0; w < sizeof(which); ++w)
       {
         const Register reg = (Register)(which[w]);
          if (reg != 0xFF)   values[w] = read_register(reg);
          else               values[w] = -1;
       }

   // print registers...
   //
   print_string("\n     TRF-7970 register dump:\n"
                   "-----+0-+1-+2-+3-+4-+5-+6-+7");

   for (uint8_t w = 0; w < sizeof(which); ++w)
       {
         if ((w & 7) == 0)
            {
              print_string("\nr");
               print_hex2(w);
               print_char(':');
            }
         if      (values[w] == -1)    print_string(" --");
         else if (values[w] == -2)    print_string(" ??");
         else
            {
              print_char(' ');
              print_hex2(values[w]);
            }
       }

   print_string("\n\n");
}
//-----------------------------------------------------------------------------
void
LED_01(uint8_t val)
{
   enum { LED_ON = 600, LED_OFF = 200 };   // LED timing (ms)

   if (val & 1)   // 1: blink green LED
      {
        set_pin(B, LED_GREEN);    // green LED on
        sleep_ms(LED_ON);
        clr_pin(B, LED_GREEN);   // green LED off
      }
   else           // 0: blink red LED
      {
        set_pin(D, LED_RED);      // red LED on
        sleep_ms(LED_ON);
        clr_pin(D, LED_RED);     // red LED off
      }

   sleep_ms(LED_OFF);
}
//-----------------------------------------------------------------------------
void
battery_test()
{
   ACSR = 0 << ACD     // DO NOT disable comparator
        | 1 << ACBG    // DO     use internal bandgap reference
        | 0 << ACI     // DO NOT clear interrupt
        | 0 << ACIE    // DO NOT enable interrupt
        | 0 << ACIC    // DO NOT enable input capture
        | 2 << ACIS0   // interrupt on falling edge
        ;
   _delay_ms(1);       // wait 1 ms for bandgap reference to start up

   TCCR1A = 0;
   TCCR1B = 1 << CS10;       // clock source: io-clk

   batt_result = 9999;
   ACSR = 0 << ACD     // DO NOT disable comparator
        | 1 << ACBG    // DO     use internal bandgap reference
        | 1 << ACI     // DO     clear interrupt
        | 1 << ACIE    // DO     enable interrupt
        | 0 << ACIC    // DO NOT enable input capture
        | 2 << ACIS0   // interrupt on falling edge
        ;

   // make B_BTEST_OUT an input with pullup. The pullup then charges
   // capacitor C6.
   //
   input_pin(B, BTEST_OUT);  // BTEST_OUT: in: pullup charges capacitor
   set_pin(B, BTEST_OUT);    // enable pullup on AIN0

   TCNT1 = 0;
   sei();
   _delay_ms(1);          // DO NOT sleep_ms() !
   cli();

   ACSR = 1 << ACD     // DO     disable comparator
        | 0 << ACBG    // DO NOT use internal bandgap reference
        | 1 << ACI     // DO     clear interrupt
        | 0 << ACIE    // DO NOT enable interrupt
        | 0 << ACIC    // DO NOT enable input capture
        | 2 << ACIS0   // interrupt on falling edge
        ;

   // discharge capacitor for the next masurement
   //
   clr_pin(B, BTEST_OUT);      // disable pullup on AIN0
   output_pin(B, BTEST_OUT);   // BTEST_OUT direction = out
   clr_pin(B, BTEST_OUT);      // drive AIN0 low 
}
//-----------------------------------------------------------------------------
//
// one pass, return the number of ms to sleep after this pass
int32_t
doit()
{
   // blink according to board_status
   //
   LED_01(board_status >> 2);
   LED_01(board_status >> 1);
   LED_01(board_status);

   battery_test();

   // battery_test() sets batt_result to roughlu 800 (full battery) ...
   // 1200 (empty battery). To save bytes we scale batt_result down to uint8_t.
   if (pass == 0)   // power ON
      {
        const uint8_t br = batt_result >> 3;
        uint8_t battery_beeps = 5;

        if      (br >= user_params.battery_1__8)   battery_beeps = 1;
        else if (br >= user_params.battery_2__8)   battery_beeps = 2;
        else if (br >= user_params.battery_3__8)   battery_beeps = 3;
        else if (br >= user_params.battery_4__8)   battery_beeps = 4;

        beep(battery_beeps, 200, 200);
      }

   print_stringv("pass=\x90", pass);
   print_stringv(" iniB=\x80", initial_B);
   print_stringv(" stat=\x80", board_status);
   print_stringv(" batt=\x90\n", batt_result);

   setup_chip();
   gluco_idx = 0;

bool errors;
   cache = SENSOR_Cache;
   for (uint8_t b = 3; b < 15; ++b)
       {
         if ((errors = read_Block(b)
                    || decode_Block(b)))   break;
         read_ISR();   // clear interrupt register
       }
   RF_Off();

   if (errors)
      {
         board_status = BSTAT_RFID_ERROR;
         transmit_glucose(0);
         disable_enocean();
         beep(3, 100, 100);
         return 8000*int32_t(user_params.read_error_retry__8);
      }

#if EEPROM_CACHE and ENOCEAN
   enable_enocean();
   for (uint8_t b = 4; b < 15; ++b)   transmit_block(b);
   disable_enocean();
#endif

   gluco_sort();

   /// compute the average of the 9 middle glucose values...
   //
   enum { end = sizeof(gluco2_vec) - 3 };
uint16_t aver_2 = 0;
   for (int8_t j = 3; j < end; ++j)  aver_2 += gluco2_vec[j];
   aver_2 /= (sizeof(gluco2_vec) - 6);

#if EEPROM_CACHE
   write_cache(pass);
   write_cache(aver_2);
   write_cache(initial_glucose_2);
   write_cache(user_params.alarm_LOW__2);
   write_cache(user_params.alarm_HIGH__2);
#endif

   print_stringv("glucose: \x90\n", 2*aver_2);   // aver_2 is halved!

bool raise_alarm = false;
   if (initial_glucose_2 == 0)   // first glucose measurement
      {
        initial_glucose_2 = aver_2;
        board_status = BSTAT_RUNNING;

        // maybe adjust the alarm thresholds so that aver_2 lies well between
        // them. This is to avoid an alarm if the initial glucose level is too
        // high or too low when the device is switched on. That is, we want:
        //
        // alarm_LOW + margin_LOW  <=  aver_2  <=  alarm_HIGH - margin_HIGH
        //
        const uint8_t amax_2 = initial_glucose_2 + user_params.margin_HIGH__2;
        const uint8_t amin_2 = initial_glucose_2 - user_params.margin_LOW__2;

        if (user_params.alarm_LOW__2 > amin_2)
           {
             user_params.alarm_LOW__2 = amin_2;
           }
        if (user_params.alarm_HIGH__2 < amax_2)
           {
             user_params.alarm_HIGH__2 = amax_2;
           }

        print_stringv("ini-low: \x90\n", 2*user_params.alarm_LOW__2);
        print_stringv("ini-high: \x90\n", 2*user_params.alarm_HIGH__2);
      }
   else if (aver_2 >= initial_glucose_2)   // glucose has increased
      {
        // maybe decrease the upper alarm thresholds again
        //
        const uint8_t new_limit = aver_2 + user_params.margin_HIGH__2;
        if (user_params.alarm_HIGH__2 > ALARM_HIGH__2 &&
            user_params.alarm_HIGH__2 > new_limit)
           {
             user_params.alarm_HIGH__2 = new_limit;
             print_stringv("new-high: \x90\n", 2*new_limit);
           }

        board_status = BSTAT_ABOVE_INITIAL;
        if (aver_2 > user_params.alarm_HIGH__2)
           {
             set_pin(D, LED_RED);      // red LED on
             raise_alarm = true;
           }
      }
   else   // glucose has decreased
      {
        // maybe increase the lower alarm threshold again
        //
        const uint8_t new_limit = aver_2 - user_params.margin_LOW__2;
        if (user_params.alarm_LOW__2 < ALARM_LOW__2 &&
            user_params.alarm_LOW__2 < new_limit)
           {
             user_params.alarm_LOW__2 = new_limit;
             print_stringv("new-low: \x90\n", 2*new_limit);
           }

        board_status = BSTAT_BELOW_INITIAL;
        if (aver_2 < user_params.alarm_LOW__2)
           {
             set_pin(B, LED_GREEN);    // green LED on
             raise_alarm = true;
           }
      }

   // transmit_glucose() also transmits board_status, so that we must not
   // call it before board_status was updated
   //
   transmit_glucose(aver_2);
   disable_enocean();

   if (raise_alarm)   // glucose is too low or too high
      {
         beep(172, 500, 200);   // beep for ~ 2 minutes
         return 2000;           // then wait 2 seconds
      }

   return 8000*int32_t(user_params.read_interval__8);
}
//-----------------------------------------------------------------------------
int
main(int, char *[])
{
   // read user definable parameters from EEPROM
   {
     uint8_t * up = (uint8_t *)&user_params;
     for (uint16_t e = USER_PARAMS; e < sizeof(user_params); ++e)
         up[e] = eeprom_read_byte((const uint8_t *)e);
   }

   init_hardware();
   sleep_ms(50);

   init_RFID_reader();

#if HARD_UART || SOFT_UART
const uint8_t calib = user_params.oscillator_calibration;
   if (calib != 0xFF)   OSCCAL = calib;        // calibration override
   else                 OSCCAL = CPU_CALIB;
#endif

#if ENOCEAN
   for (const char * p = "\n\nENO-RES\n\n"; *p;)   print_byte(*p++);
#endif

   print_stringv("\n\n\nosc=x\x80\n", OSCCAL);
   print_stringv("slope=\x90\n",       user_params.sensor_slope);
   print_stringv("offset=\x90\n",      user_params.sensor_offset);
   print_stringv("alarm_HIGH=\x90\n",  user_params.alarm_HIGH__2  << 1);
   print_stringv("alarm_LOW=\x90\n",   user_params.alarm_LOW__2   << 1);
   print_stringv("margin_HIGH=\x90\n", user_params.margin_HIGH__2 << 1);
   print_stringv("margin_LOW=\x90\n",  user_params.margin_LOW__2  << 1);
   print_stringv("batt_1=\x90\n",      user_params.battery_1__8   << 3);
   print_stringv("batt_2=\x90\n",      user_params.battery_2__8   << 3);
   print_stringv("batt_3=\x90\n",      user_params.battery_3__8   << 3);
   print_stringv("batt_4=\x90\n",      user_params.battery_4__8   << 3);
   print_stringv("batt_5=\x90\n",      user_params.battery_5__8   << 3);
   print_stringv("read_error_retry=\x90 sec\n",
                                       user_params.read_error_retry__8 << 3);
   print_stringv("read_interval=\x90 sec\n\n",
                                       user_params.read_interval__8 << 3);


   // transmit a glucose value of 0 as a restart indication and to
   // inform receiver(s) about the battery status.
   //
   transmit_glucose(0);

   for (pass = 0;; ++pass)
       {
//       dump_registers();
         int32_t wait = doit();
         while (wait > 0)   wait -= sleep_ms(wait);
       }
}
//-----------------------------------------------------------------------------
