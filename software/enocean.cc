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
 functions for accessing an enocean TCM 310 module
 */

// NOTE: our prototypes' enocean IDs are:
//       FFD64680 (old PCB) and
//       FFD50500 (new PCB)

static  int8_t rx_idx = 0;
static uint8_t id2 = 0;
static uint8_t id3 = 0;
static uint8_t id4 = 0;
static bool id_valid = false;

static void
disable_enocean()
{
   // let UART and the TCM finish their transmission
   //
   sleep_ms(100);

   // disable UART so that normal GPIO is enabled on the serial TxD pin
   //
#if HARD_UART
   UCSRB = 0 << RXCIE
         | 0 << RXEN
         | 0 << TXEN;
#endif

   // make TxD an output and set it to Low. This is to protect the TCM 310's
   // RxD from having a high level when its power is switched off
   //
   output_pin(D, TCM_TxD);
   clr_pin(D, TCM_TxD);

   // disconnect the TCM 310 from Vcc (TCM_POWER is active low)
   //
   set_pin(B, TCM_POWER);

   // let the TCM 310 elko discharge. Wait long enough so that the power
   // to the TCM 310 can fully go down.
   //
   sleep_ms(10000);
}
//-----------------------------------------------------------------------------
static void
enable_enocean()
{
   // connect the TCM 310 to Vcc (TCM_POWER is active low)
   //
   clr_pin(B, TCM_POWER);

   // let the TCM 310 elko charge
   //
   sleep_ms(100);

   // enable (only) the transmitter
   //
#if HARD_UART
   UCSRB = 0 << RXCIE   // disable Rx interrupt
         | 0 << RXEN    // disable receiver
         | 1 << TXEN;   // enable transmitter
#endif

   // let TCM 310 start up (takes max. 500 ms)
   //
   sleep_ms(600);

   if (id_valid)   return;

   // first call of enable_enocean(), determine the TCM 310 enocean ID...

   // enable transmitter, receiver, and receiver interrupts
   //
   UCSRB = 1 << RXCIE   // enable Rx interrupt
         | 1 << RXEN    // enable receiver
         | 1 << TXEN;   // enable transmitter

static uint8_t RD_BASE_command[] = { 0x55, 0x00, 0x01, 0x00,
                                     0x05, 0x70, 0x08, 0x38 };
   for (uint8_t c = 0; c < sizeof(RD_BASE_command); ++c)
       print_byte(RD_BASE_command[c]);

   rx_idx = 0;
   clr_pin(B, LED_GREEN);
   set_pin(D, LED_RED);
   sei();
   for (uint8_t t = 0; t < 150; ++t)
       {
         _delay_ms(100);
         if (id_valid)   break;
         PINB = B_LED_GREEN;   // toggle green LED
         PIND = D_LED_RED;     // toggle red LED
       }
   cli();

#if HARD_UART
   // disable receiver and receiver interrupts, enable transmitter
   UCSRB = 0 << RXCIE   // disable Rx interrupt
         | 0 << RXEN    // dusable receiver
         | 1 << TXEN;   // enable transmitter
#endif
}
//-----------------------------------------------------------------------------
enum
{
   // header constants...
   //
   OLEN = 1             // subtel
        + 4             // dest ID
        + 1             // dBm
        + 1,            // ENCRYPTED

   // data constants...
   //
   RORG_VLD    = 0xD2,
                         // Jalu_server uses commands 0..11
   Gluco_VALUE   = 0x20,   // command: glucose value
   Change_BITMAP = 0x21,   // command: changed bytes
   Change_VALUES = 0x22,   // command: changed bytes
   ESTATUS     = 0,

   // optional data constants...
   //
   SubTelNum   = 0,
   DestID      = 0xFF,   // 4 times
   dBm         = 0xFF,
   ENCRYPTED   = 0,
};

static void
transmit_header(uint8_t dlen)
{
   enum {
          SYNC        = 0x55,
          RADIO_ERP1  = 1
        };

   print_byte(SYNC);     // sync

crc = 0;
   print_byte(0);      // upper byte of dlen
   print_byte(dlen);   // lower byte of dlen
   print_byte(OLEN);
   print_byte(RADIO_ERP1);
   print_byte(crc);
}
//-----------------------------------------------------------------------------
static void
transmit_common()
{
   // common part of data
   print_byte(0xFF);         // id1 (always FF)
   print_byte(id2);
   print_byte(id3);
   print_byte(id4);
   print_byte(ESTATUS);

   // optional data
   print_byte(SubTelNum);
   print_byte(DestID);
   print_byte(DestID);
   print_byte(DestID);
   print_byte(DestID);
   print_byte(dBm);
   print_byte(ENCRYPTED);
   print_byte(crc);
}
//-----------------------------------------------------------------------------
inline void
transmit_change_bitmap()
{
   // message has 1 + (CB_LEN + changed_idx) bytes:
   //
   // COMMAND sizeof(changed_bitmap) changed_values[0..changed_idx-1]
   //
   enum { CB_LEN = sizeof(changed_bitmap) };

const uint8_t MESSAGE_LEN = 1              // COMMAND
                          + CB_LEN;        // changed_bitmap
                          ;

const uint8_t DLEN = 1             // Rorg
                   + MESSAGE_LEN   // COMMAND, changed_bitmap, changed_values
                   + 4             // sender ID
                   + 1;            // status
   transmit_header(DLEN);

crc = 0;
   print_byte(RORG_VLD);        // VLD data...
      print_byte(Change_BITMAP);   // command
      for (int8_t b = 0; b < CB_LEN; ++b)       print_byte(changed_bitmap[b]);
   transmit_common();

   sleep_ms(100);   // time to finish transmission
}
//-----------------------------------------------------------------------------
inline void
transmit_changed_values()
{
   // message has 1 + (CB_LEN + changed_idx) bytes:
   //
   // COMMAND sizeof(changed_bitmap) changed_values[0..changed_idx-1]
   //
   enum { CB_LEN = sizeof(changed_bitmap) };

const uint8_t MESSAGE_LEN = 1              // COMMAND
                          + changed_idx;   // changed_values[]

const uint8_t DLEN = 1             // Rorg
                   + MESSAGE_LEN   // COMMAND, changed_bitmap, changed_values
                   + 4             // sender ID
                   + 1;            // status
   transmit_header(DLEN);

crc = 0;
   print_byte(RORG_VLD);        // VLD data...
      print_byte(Change_VALUES);   // command
      for (int8_t v = 0; v < changed_idx; ++v)  print_byte(changed_values[v]);
   transmit_common();

   sleep_ms(100);   // time to finish transmission
}
//-----------------------------------------------------------------------------
static void
transmit_glucose(uint8_t gluco_2)
{
   // message has 5 bytes:
   //
   //
   enum
      {
        MESSAGE_LEN = 1   // COMMAND
                    + 1   // gluco_2,
                    + 2   //  battery-high, battery-low,
                    + 1,  //  board status

        // header constants...
        //
        DLEN        = 1             // Rorg
                    + MESSAGE_LEN   // message
                    + 4             // sender ID
                    + 1,            // status
      };

   transmit_header(DLEN);

crc = 0;
   print_byte(RORG_VLD);           // data...
       print_byte(Gluco_VALUE);        // command
       print_byte(gluco_2);            // glucose/2
       print_byte(batt_result >> 8);   // battery high
       print_byte(batt_result);        // battery low
       print_byte(board_status);       // dito
   transmit_common();

   sleep_ms(100);   // time to finish transmission
}
//-----------------------------------------------------------------------------
static const uint8_t RD_BASE_response[] =
   { 0x55, 0x00, 0x05, 0x01, 0x02, 0xDB, 0x00, 0xFF };

ISR(USART0_RX_vect)
{
const uint8_t cc = UDR;

   if (!id_valid)
      {
        switch(rx_idx)
           {
             case 0 ... sizeof(RD_BASE_response) - 1:
                           if (cc != RD_BASE_response[rx_idx])   rx_idx = -1;
                           break;

             case 8:  id2 = cc;   break;
             case 9:  id3 = cc;   break;
             case 10: id4 = cc;
                      id_valid = true;
                      break;


             default: break;
           }
             ++rx_idx;
      }
}
//-----------------------------------------------------------------------------

