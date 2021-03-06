// This simplified demo scrolls the text of the Jaberwoky poem directly from flash memory
// Full article at  http://wp.josh.com/2016/05/20/huge-scrolling-arduino-led-sign/

// Change this to be at least as long as your pixel string (too long will work fine, just be a little slower)

#define PIXELS 60*2  // Number of pixels in the string. I am using 4 meters of 96LED/M

// These values depend on which pins your 8 strings are connected to and what board you are using 
// More info on how to find these at http://www.arduino.cc/en/Reference/PortManipulation

// PORTD controls Digital Pins 0-7 on the Uno

// You'll need to look up the port/bit combination for other boards. 

// Note that you could also include the DigitalWriteFast header file to not need to to this lookup.

#define PIXEL_PORT  PORTD  // Port of the pin the pixels are connected to
#define PIXEL_DDR   DDRD   // Port of the pin the pixels are connected to


static const uint8_t onBits=0b11111110;   // Bit pattern to write to port to turn on all pins connected to LED strips. 
                                          // If you do not want to use all 8 pins, you can mask off the ones you don't want
                                          // Note that these will still get 0 written to them when we send pixels
                                          // TODO: If we have time, we could even add a variable that will and/or into the bits before writing to the port to support any combination of bits/values                                  

// These are the timing constraints taken mostly from 
// imperically measuring the output from the Adafruit library strandtest program

// Note that some of these defined values are for refernce only - the actual timing is determinted by the hard code.

#define T1H  814    // Width of a 1 bit in ns - 13 cycles
#define T1L  438    // Width of a 1 bit in ns -  7 cycles

#define T0H  312    // Width of a 0 bit in ns -  5 cycles
#define T0L  936    // Width of a 0 bit in ns - 15 cycles 

// Phase #1 - Always 1  - 5 cycles
// Phase #2 - Data part - 8 cycles
// Phase #3 - Always 0  - 7 cycles


#define RES 50000   // Width of the low gap between bits to cause a frame to latch

// Here are some convience defines for using nanoseconds specs to generate actual CPU delays

#define NS_PER_SEC (1000000000L)          // Note that this has to be SIGNED since we want to be able to check for negative values of derivatives

#define CYCLES_PER_SEC (F_CPU)

#define NS_PER_CYCLE ( NS_PER_SEC / CYCLES_PER_SEC )

#define NS_TO_CYCLES(n) ( (n) / NS_PER_CYCLE )


// Sends a full 8 bits down all the pins, represening a single color of 1 pixel
// We walk though the 8 bits in colorbyte one at a time. If the bit is 1 then we send the 8 bits of row out. Otherwise we send 0. 
// We send onBits at the first phase of the signal generation. We could just send 0xff, but that mught enable pull-ups on pins that we are not using. 

/// Unforntunately we have to drop to ASM for this so we can interleave the computaions durring the delays, otherwise things get too slow.

// OnBits is the mask of which bits are connected to strips. We pass it on so that we
// do not turn on unused pins becuase this would enable the pullup. Also, hopefully passing this
// will cause the compiler to allocate a Register for it and avoid a reload every pass.

static inline void sendBitx8(  const uint8_t row , const uint8_t colorbyte , const uint8_t onBits ) {  
              
    asm volatile (


      "L_%=: \n\r"  
      
      "out %[port], %[onBits] \n\t"                 // (1 cycles) - send either T0H or the first part of T1H. Onbits is a mask of which bits have strings attached.

      // Next determine if we are going to be sending 1s or 0s based on the current bit in the color....
      
      "mov r0, %[bitwalker] \n\t"                   // (1 cycles) 
      "and r0, %[colorbyte] \n\t"                   // (1 cycles)  - is the current bit in the color byte set?
      "breq OFF_%= \n\t"                            // (1 cycles) - bit in color is 0, then send full zero row (takes 2 cycles if branch taken, count the extra 1 on the target line)

      // If we get here, then we want to send a 1 for every row that has an ON dot...
      "nop \n\t  "                                  // (1 cycles) 
      "out %[port], %[row]   \n\t"                  // (1 cycles) - set the output bits to [row] This is phase for T0H-T1H.
                                                    // ==========
                                                    // (5 cycles) - T0H (Phase #1)


      "nop \n\t nop \n\t "                          // (2 cycles) 
      "nop \n\t nop \n\t "                          // (2 cycles) 
      "nop \n\t nop \n\t "                          // (2 cycles) 
      "nop \n\t "                                   // (1 cycles) 

      "out %[port], __zero_reg__ \n\t"              // (1 cycles) - set the output bits to 0x00 based on the bit in colorbyte. This is phase for T0H-T1H
                                                    // ==========
                                                    // (8 cycles) - Phase #2
                                                    
      "ror %[bitwalker] \n\t"                      // (1 cycles) - get ready for next pass. On last pass, the bit will end up in C flag
                  
      "brcs DONE_%= \n\t"                          // (1 cycles) Exit if carry bit is set as a result of us walking all 8 bits. We assume that the process around us will tak long enough to cover the phase 3 delay

      "nop \n\t \n\t "                             // (1 cycles) - When added to the 5 cycles in S:, we gte the 7 cycles of T1L
            
      "jmp L_%= \n\t"                              // (3 cycles) 
                                                   // (1 cycles) - The OUT on the next pass of the loop
                                                   // ==========
                                                   // (7 cycles) - T1L
                                                   
                                                          
      "OFF_%=: \n\r"                                // (1 cycles)    Note that we land here becuase of breq, which takes takes 2 cycles

      "out %[port], __zero_reg__ \n\t"              // (1 cycles) - set the output bits to 0x00 based on the bit in colorbyte. This is phase for T0H-T1H
                                                    // ==========
                                                    // (5 cycles) - T0H

      "ror %[bitwalker] \n\t"                      // (1 cycles) - get ready for next pass. On last pass, the bit will end up in C flag
                  
      "brcs DONE_%= \n\t"                          // (1 cycles) Exit if carry bit is set as a result of us walking all 8 bits. We assume that the process around us will tak long enough to cover the phase 3 delay

      "nop \n\t nop \n\t "                          // (2 cycles) 
      "nop \n\t nop \n\t "                          // (2 cycles) 
      "nop \n\t nop \n\t "                          // (2 cycles)             
      "nop \n\t nop \n\t "                          // (2 cycles)             
      "nop \n\t "                                   // (1 cycles)             
            
      "jmp L_%= \n\t"                               // (3 cycles) 
                                                    // (1 cycles) - The OUT on the next pass of the loop      
                                                    // ==========
                                                    //(15 cycles) - T0L 
      
            
      "DONE_%=: \n\t"

      // Don't need an explicit delay here since the overhead that follows will always be long enough
    
      ::
      [port]    "I" (_SFR_IO_ADDR(PIXEL_PORT)),
      [row]   "d" (row),
      [onBits]   "d" (onBits),
      [colorbyte]   "d" (colorbyte ),     // Phase 2 of the signal where the actual data bits show up.                
      [bitwalker] "r" (0x80)                      // Alocate a register to hold a bit that we will walk down though the color byte

    );
                                  
    // Note that the inter-bit gap can be as long as you want as long as it doesn't exceed the reset timeout (which is A long time)
    
} 




// Just wait long enough without sending any bots to cause the pixels to latch and display the last sent frame

void show() {
  delayMicroseconds( (RES / 1000UL) + 1);       // Round up since the delay must be _at_least_ this long (too short might not work, too long not a problem)
}


// Send 3 bytes of color data (R,G,B) for a signle pixel down all the connected stringsat the same time
// A 1 bit in "row" means send the color, a 0 bit means send black. 

static inline void sendRowRGB( uint8_t row ,  uint8_t r,  uint8_t g,  uint8_t b ) {

  sendBitx8( row , g , onBits);    // WS2812 takes colors in GRB order
  sendBitx8( row , r , onBits);    // WS2812 takes colors in GRB order
  sendBitx8( row , b , onBits);    // WS2812 takes colors in GRB order
  
}

// This nice 5x7 font from here...
// http://sunge.awardspace.com/glcd-sd/node4.html

// Font details:
// 1) Each char is fixed 5x7 pixels. 
// 2) Each byte is one column.
// 3) Columns are left to right order, leftmost byte is leftmost column of pixels.
// 4) Each column is 8 bits high.
// 5) Bit #7 is top line of char, Bit #1 is bottom.
// 6) Bit #0 is always 0, becuase this pin is used as serial input and setting to 1 would enable the pull-up.

// defines ascii characters 0x20-0x7F (32-127)
// PROGMEM after variable name as per https://www.arduino.cc/en/Reference/PROGMEM

#define FONT_WIDTH 5      
#define INTERCHAR_SPACE 1
#define ASCII_OFFSET 0x20    // ASSCI code of 1st char in font array

const uint8_t Font5x7[] PROGMEM = {
0x00,0x00,0x00,0x00,0x00,//  
0x00,0x00,0xfa,0x00,0x00,// !
0x00,0xe0,0x00,0xe0,0x00,// "
0x28,0xfe,0x28,0xfe,0x28,// #
0x24,0x54,0xfe,0x54,0x48,// $
0xc4,0xc8,0x10,0x26,0x46,// %
0x6c,0x92,0xaa,0x44,0x0a,// &
0x00,0xa0,0xc0,0x00,0x00,// '
0x00,0x38,0x44,0x82,0x00,// (
0x00,0x82,0x44,0x38,0x00,// )
0x10,0x54,0x38,0x54,0x10,// *
0x10,0x10,0x7c,0x10,0x10,// +
0x00,0x0a,0x0c,0x00,0x00,// ,
0x10,0x10,0x10,0x10,0x10,// -
0x00,0x06,0x06,0x00,0x00,// .
0x04,0x08,0x10,0x20,0x40,// /
0x7c,0x8a,0x92,0xa2,0x7c,// 0
0x00,0x42,0xfe,0x02,0x00,// 1
0x42,0x86,0x8a,0x92,0x62,// 2
0x84,0x82,0xa2,0xd2,0x8c,// 3
0x18,0x28,0x48,0xfe,0x08,// 4
0xe4,0xa2,0xa2,0xa2,0x9c,// 5
0x3c,0x52,0x92,0x92,0x0c,// 6
0x80,0x8e,0x90,0xa0,0xc0,// 7
0x6c,0x92,0x92,0x92,0x6c,// 8
0x60,0x92,0x92,0x94,0x78,// 9
0x00,0x6c,0x6c,0x00,0x00,// :
0x00,0x6a,0x6c,0x00,0x00,// ;
0x00,0x10,0x28,0x44,0x82,// <
0x28,0x28,0x28,0x28,0x28,// =
0x82,0x44,0x28,0x10,0x00,// >
0x40,0x80,0x8a,0x90,0x60,// ?
0x4c,0x92,0x9e,0x82,0x7c,// @
0x7e,0x88,0x88,0x88,0x7e,// A
0xfe,0x92,0x92,0x92,0x6c,// B
0x7c,0x82,0x82,0x82,0x44,// C
0xfe,0x82,0x82,0x44,0x38,// D
0xfe,0x92,0x92,0x92,0x82,// E
0xfe,0x90,0x90,0x80,0x80,// F
0x7c,0x82,0x82,0x8a,0x4c,// G
0xfe,0x10,0x10,0x10,0xfe,// H
0x00,0x82,0xfe,0x82,0x00,// I
0x04,0x02,0x82,0xfc,0x80,// J
0xfe,0x10,0x28,0x44,0x82,// K
0xfe,0x02,0x02,0x02,0x02,// L
0xfe,0x40,0x20,0x40,0xfe,// M
0xfe,0x20,0x10,0x08,0xfe,// N
0x7c,0x82,0x82,0x82,0x7c,// O
0xfe,0x90,0x90,0x90,0x60,// P
0x7c,0x82,0x8a,0x84,0x7a,// Q
0xfe,0x90,0x98,0x94,0x62,// R
0x62,0x92,0x92,0x92,0x8c,// S
0x80,0x80,0xfe,0x80,0x80,// T
0xfc,0x02,0x02,0x02,0xfc,// U
0xf8,0x04,0x02,0x04,0xf8,// V
0xfe,0x04,0x18,0x04,0xfe,// W
0xc6,0x28,0x10,0x28,0xc6,// X
0xc0,0x20,0x1e,0x20,0xc0,// Y
0x86,0x8a,0x92,0xa2,0xc2,// Z
0x00,0x00,0xfe,0x82,0x82,// [
0x40,0x20,0x10,0x08,0x04,// (backslash)
0x82,0x82,0xfe,0x00,0x00,// ]
0x20,0x40,0x80,0x40,0x20,// ^
0x02,0x02,0x02,0x02,0x02,// _
0x00,0x80,0x40,0x20,0x00,// `
0x04,0x2a,0x2a,0x2a,0x1e,// a
0xfe,0x12,0x22,0x22,0x1c,// b
0x1c,0x22,0x22,0x22,0x04,// c
0x1c,0x22,0x22,0x12,0xfe,// d
0x1c,0x2a,0x2a,0x2a,0x18,// e
0x10,0x7e,0x90,0x80,0x40,// f
0x10,0x28,0x2a,0x2a,0x3c,// g
0xfe,0x10,0x20,0x20,0x1e,// h
0x00,0x22,0xbe,0x02,0x00,// i
0x04,0x02,0x22,0xbc,0x00,// j
0x00,0xfe,0x08,0x14,0x22,// k
0x00,0x82,0xfe,0x02,0x00,// l
0x3e,0x20,0x18,0x20,0x1e,// m
0x3e,0x10,0x20,0x20,0x1e,// n
0x1c,0x22,0x22,0x22,0x1c,// o
0x3e,0x28,0x28,0x28,0x10,// p
0x10,0x28,0x28,0x18,0x3e,// q
0x3e,0x10,0x20,0x20,0x10,// r
0x12,0x2a,0x2a,0x2a,0x04,// s
0x20,0xfc,0x22,0x02,0x04,// t
0x3c,0x02,0x02,0x04,0x3e,// u
0x38,0x04,0x02,0x04,0x38,// v
0x3c,0x02,0x0c,0x02,0x3c,// w
0x22,0x14,0x08,0x14,0x22,// x
0x30,0x0a,0x0a,0x0a,0x3c,// y
0x22,0x26,0x2a,0x32,0x22,// z
0x00,0x10,0x6c,0x82,0x00,// {
0x00,0x00,0xfe,0x00,0x00,// |
0x00,0x82,0x6c,0x10,0x00,// }
0x10,0x10,0x54,0x38,0x10,// ~
0x10,0x38,0x54,0x10,0x10,// 
};

// Send the pixels to form the specified char, not including interchar space
// skip is the number of pixels to skip at the begining to enable sub-char smooth scrolling

// TODO: Subtract the offset from the char before starting the send sequence to save time if nessisary
// TODO: Also could pad the begining of the font table to aovid the offset subtraction at the cost of 20*8 bytes of progmem
// TODO: Could pad all chars out to 8 bytes wide to turn the the multiply by FONT_WIDTH into a shift 

static inline void sendChar( uint8_t c ,  uint8_t skip , uint8_t r,  uint8_t g,  uint8_t b ) {

  const uint8_t *charbase = Font5x7 + (( c -' ')* FONT_WIDTH ) ; 

  uint8_t col=FONT_WIDTH; 

  while (skip--) {
      charbase++;
      col--;    
  }
  
  while (col--) {
      sendRowRGB( pgm_read_byte_near( charbase++ ) , r , g , b );
  }    
  
  col=INTERCHAR_SPACE;
  
  while (col--) {

    sendRowRGB( 0 , r , g , b );    // Interchar space
    
  }
  
}


// Show the passed string. The last letter of the string will be in the rightmost pixels of the display.
// Skip is how many cols of the 1st char to skip for smooth scrolling


static inline void sendString( const char *s , uint8_t skip ,  const uint8_t r,  const uint8_t g,  const uint8_t b ) {

  unsigned int l=PIXELS/(FONT_WIDTH+INTERCHAR_SPACE); 

  sendChar( *s , skip ,  r , g , b );   // First char is special case becuase it can be stepped for smooth scrolling
  
  while ( *(++s) && l--) {

    sendChar( *s , 0,  r , g , b );

  }

  show();
}

void setup() {

  //Initialize serial and wait for port to open:
  //Serial.begin(9600);
  
  PIXEL_DDR |= onBits;         // Set used pins to output mode
}

String displayWord;

/*(
static char jabberText[] = 
      "                    " 
      "Validated Learning"  
      
      ;
*/

void loop() {
  
  displayWord = "   ";
  
  int randNumber = random(1, 174);
  
  switch (randNumber) {
    case 1:
      displayWord = F("champion"); 
      break;
    case 2:
      displayWord = F("bag of cash");
      break;
    case 3:
      displayWord = F("old man walk");
      break;
    case 4:
      displayWord = F("leveraged buyout");
      break;
    case 5:
      displayWord = F("beer");
      break;
    case 6:
      displayWord = F("outcome");
      break;
    case 7:
      displayWord = F("team");
      break;
    case 8:
      displayWord = F("puppy");
      break;
    case 9:
      displayWord = F("trust fund");
      break;
    case 10:
      displayWord = F("coffee");
      break;
    case 11:
      displayWord = F("hackathon");
      break;
    case 12:
      displayWord = F("velociraptor");
      break;
    case 13:
      displayWord = F("Azure Cloud");
      break;
    case 14:
      displayWord = F("Kumquat");
      break;
    case 15:
      displayWord = F("zoltron3000");
      break;
    case 16:
      displayWord = F("office corgi");
      break;
    case 17:
      displayWord = F("thousand monkeys with typewriters");
      break;
    case 18:
      displayWord = F("hot javascript framework");
      break;
    case 19:
      displayWord = F("theme song");
      break;
    case 20:
      displayWord = F("baby monkey");
      break;
    case 21:
      displayWord = F("dunit");
      break;
    case 22:
      displayWord = F("splashing gorilla");
      break;
    case 23:
      displayWord = F("Special Codename");
      break;
    case 24:
      displayWord = F("covfefe");
      break;
    case 25:
      displayWord = F("covfefe");
      break;
    case 26:
      displayWord = F("whiteboard");
      break;
    case 27:
      displayWord = F("coffee walk");
      break;
    case 28:
      displayWord = F("Flowdock");
      break;
    case 29:
      displayWord = F("Tester");
      break;
    case 30:
      displayWord = F("Greyhound");
      break;
    case 31:
      displayWord = F("Spelcheker");
      break;
    case 32:
      displayWord = F("stalk of broccoli");
      break;
    case 33:
      displayWord = F("what what");
      break;
    case 34:
      displayWord = F("formatted_id");
      break;
    case 35:
      displayWord = F("I, a d, an e, and an a");
      break;
    case 36:
      displayWord = F("hierarchical requirement");
      break;
    case 37:
      displayWord = F("team of interns");
      break;
    case 38:
      displayWord = F("champion");
      break;
    case 39:
      displayWord = F("hierarchical requirement");
      break;
    case 40:
      displayWord = F("lunchtime sporting");
      break;
    case 41:
      displayWord = F("voice");
      break;
    case 42:
      displayWord = F("VC to fund it");
      break;
    case 43:
      displayWord = F("chunky cheetah");
      break;
    case 44:
      displayWord = F("zombocom");
      break;
    case 45:
      displayWord = F("pad of paper");
      break;
    case 46:
      displayWord = F("sharp pencil");
      break;
    case 47:
      displayWord = F("a new perspective");
      break;
    case 48:
      displayWord = F("a Splunk query");
      break;
    case 49:
      displayWord = F("exploding sparkle box");
      break;
    case 50:
      displayWord = F("hooptie mvp");
      break;
    case 51:
      displayWord = F("saber tooth tiger");
      break;
    case 52:
      displayWord = F("dreamer");
      break;
    case 53:
      displayWord = F("a roll of duct tape");
      break;
    case 54:
      displayWord = F("digimon");
      break;
    case 55:
      displayWord = F("werther's original");
      break;
    case 56:
      displayWord = F("horcrux");
      break;
    case 57:
      displayWord = F("health care plan");
      break;
    case 58:
      displayWord = F("word");
      break;
    case 59:
      displayWord = F("Hot Bare Metal");
      break;
    case 60:
      displayWord = F("Psilocybe cubensis");
      break;
    case 61:
      displayWord = F("Dream Crusher");
      break;
    case 62:
      displayWord = F("Keep It Classy");
      break;
    case 63:
      displayWord = F("champion");
      break;
    case 64:
      displayWord = F("champion");
      break;
    case 65:
      displayWord = F("champion");
      break;
    case 66:
      displayWord = F("emoticon");
      break;
    case 67:
      displayWord = F("gear");
      break;
    case 68:
      displayWord = F("parachute");
      break;
    case 69:
      displayWord = F("speling");
      break;
     case 70:
      displayWord = F("best of times, worst of times");
      break;
    case 71:
      displayWord = F("Theme Song");
      break;
    case 72:
      displayWord = F("final countdown");
      break;
    case 73:
      displayWord = F("Excel Add-In");
      break;
    case 74:
      displayWord = F("GOAT");
      break;
    case 75:
      displayWord = F("crazy person");
      break;
    case 76:
      displayWord = F("montage");
      break;
    case 77:
      displayWord = F("Starbuck");
      break;
    case 78:
      displayWord = F("patent");
      break;
    case 79:
      displayWord = F("wall decal");
      break;
    case 80:
      displayWord = F("chocolate factory");
      break;
    case 81:
      displayWord = F("thunderdome");
      break;
    case 82:
      displayWord = F("PowerPoint");
      break;
    case 83:
      displayWord = F("UML Diagram");
      break;
    case 84:
      displayWord = F("business cat troll");
      break;
    case 85:
      displayWord = F("head fake and drive to the basket");
      break;
    case 86:
      displayWord = F("n Arduino");
      break;
    case 87:
      displayWord = F("hellfish chart");
      break;
    case 88:
      displayWord = F("champion");
      break;
    case 89:
      displayWord = F("meme");
      break;
    case 90:
      displayWord = F("goat rodeo");
      break;
    case 91:
      displayWord = F("barrier?");
      break;
    case 92:
      displayWord = F("stolen subnet");
      break;
    case 93:
      displayWord = F("VPN Tunnel");
      break;
    case 94:
      displayWord = F("champion");
      break;
    case 95:
      displayWord = F("pint of platypus milk");
      break;
    case 96:
      displayWord = F("more cow bell");
      break;
    case 97:
      displayWord = F("champion");
      break;
    case 98:
      displayWord = F("sugar, spice, and everything nice");
      break;
    case 99:
      displayWord = F("The Macarena");
      break;
    case 100:
      displayWord = F("deploting pigeon");
      break;
    case 101:
      displayWord = F("flow");
      break;
    case 102:
      displayWord = F("ruckus");
      break;
    case 103:
      displayWord = F("rumpus");
      break;
    case 104:
      displayWord = F("README.md");
      break;
    case 105:
      displayWord = F("grammar");
      break;
    case 106:
      displayWord = F("lead fall");
      break;
    case 107:
      displayWord = F("hootenanny");
      break;
    case 108:
      displayWord = F("pow right in the face");
      break;
    case 109:
      displayWord = F("shenanigan");
      break;
    case 110:
      displayWord = F("command hook");
      break;
    case 111:
      displayWord = F("Individuals and Interactions");
      break;
    case 112:
      displayWord = F("WARNING LED FAILURE");
      break;
    case 113:
      displayWord = F("list");
      break;
    case 114:
      displayWord = F("plumbus");
      break;
    case 115:
      displayWord = F("Functional Network");
      break;
    case 116:
      displayWord = F("disintermediation");
      break;
    case 117:
      displayWord = F("fleeb");
      break;
    case 118:
      displayWord = F("lot of Flurbos");
      break;
    case 119:
      displayWord = F("me, Mario");
      break;
    case 120:
      displayWord = F("Yeti Betty");
      break;
    case 121:
      displayWord = F("supported infrastructure");
      break;
    case 122:
      displayWord = F("empowered team");
      break;
    case 123:
      displayWord = F("ping pong table");
      break;
    case 124:
      displayWord = F("hub of productivity");
      break;
    case 125:
      displayWord = F("Disambiguate");
      break;
    case 126:
      displayWord = F("no barking from the dog, no smog");
      break;
    case 127:
      displayWord = F("imagination machine");
      break;
    case 128:
      displayWord = F("drop table champion_words");
      break;
    case 129:
      displayWord = F("ringing of the bell");
      break;
    case 130:
      displayWord = F("roll of duct tape");
      break;
    case 131:
      displayWord = F("help");
      break;
    case 132:
      displayWord = F("sell tech services");
      break;
    case 133:
      displayWord = F("procrastinator");
      break;
    case 134:
      displayWord = F("demogorgon");
      break;
    case 135:
      displayWord = F("Cthulhu");
      break;
    case 136:
      displayWord = F("sql injection");
      break;
    case 137:
      displayWord = F("infinite loop");
      break;
    case 138:
      displayWord = F("1000's of OR clauses");
      break;
    case 139:
      displayWord = F("interdimensional portal");
      break;
    case 140:
      displayWord = F("servant leader");
      break;
    case 141:
      displayWord = F("avocado");
      break;
    case 142:
      displayWord = F("UML diagram");
      break;
    case 143:
      displayWord = F("anagram");
      break;
    case 144:
      displayWord = F("snack drawer");
      break;
    case 145:
      displayWord = F("new guy/gal pie");
      break;
    case 146:
      displayWord = F("tiny amount of French");
      break;
    case 147:
      displayWord = F("jvm");
      break;
    case 148:
      displayWord = F("software that makes empty boxes sing");
      break;
    case 149:
      displayWord = F("jarvis champion");
      break;
    case 150:
      displayWord = F("collection of artwork");
      break;
    case 151:
      displayWord = F("loaf of bread a jug of wine and thou");
      break;
    case 152:
      displayWord = F("verse in iambic pentameter");
      break;
    case 153:
      displayWord = F("bb8");
      break;
    case 154:
      displayWord = F(":shitip:");
      break;
    case 155:
      displayWord = F("baguette");
      break;
    case 156:
      displayWord = F("tradition");
      break;
    case 157:
      displayWord = F("magic wand");
      break;
    case 158:
      displayWord = F("It's polite, it's right, and it's sneezy, deezy, mc... deluxe");
      break;
    case 159:
      displayWord = F("Mr. Meeseeks");
      break;
    case 160:
      displayWord = F("f005ba11-f005-ba11-f005-ba11f005ba11");
      break;
    case 161:
      displayWord = F("speculative execution");
      break;
    case 162:
      displayWord = F("mini me");
      break;
    case 163:
      displayWord = F("septuacentennial cupcake in a cup");
      break;
    case 164:
      displayWord = F("disintermediate");
      break;
    case 165:
      displayWord = F("vgri gri");
      break;
    case 166:
      displayWord = F("airplane mode");
      break;
    case 167:
      displayWord = F("pull request review");
      break;
    case 168:
      displayWord = F("choice");
      break;
    case 169:
      displayWord = F("developer begging colleagues for a PR review");
      break;
    case 170:
      displayWord = F("k-bar");
      break;
    case 171:
      displayWord = F("pneumonoultramicroscopicsilicovolcanoconiosis");
      break;
    case 172:
      displayWord = F("shared vision displayed prominently");
      break;
    case 173:
      displayWord = F("despagettify");
      break;
    case 174:
      displayWord = F("steamed ham");
      break;    
    default: 
      displayWord = F("champion");
    break;
  }

  int loopLength = 19;
  int delayLength = 15000;

  if (displayWord.length() > 19)
  {
    loopLength = displayWord.length() + 20;
    delayLength = 100;
  } 

  displayWord = "                    " + displayWord + "                    ";

  int worldLength = displayWord.length() + 1;

  char jabberText[worldLength];

  displayWord.toCharArray(jabberText,worldLength);
  
  const char *m = jabberText;

  int i = 0;
              
  while (i < loopLength) {      


      for( uint8_t step=0; step<FONT_WIDTH+INTERCHAR_SPACE  ; step++ ) {   // step though each column of the 1st char for smooth scrolling

         cli();
  
         sendString( m , step , 0x15, 0x15 , 0x15 );    // Nice and not-too-bright blue hue
        
         sei();

         _delay_ms(50);   // Slows down scrolling by pausing 10 milliseconds between horizontal steps   
         
  
      }

    m++;

    i++;

  }

delay(delayLength); //delay



}



