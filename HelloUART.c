/********************************************************************

 Author        : A. Contryman

 Date          : April 2014
 
 File          : HelloUART.c

 Description   : Based on Analog Devices example UART1.c.
 
				This code demonstrates basic UART functionality.
				 The baudrate is calculated with the following formula:
 
					DL = HCLK										   
						_______										   
						Baudrate * 2 *16	

********************************************************************/

#include <ADuC7026.h>
//#include <stdio.h>
#include <stdbool.h>
 	
/* This program relies on serial.c. These declare the functions defined in 
 * that file. */
extern int write (int file, char * ptr, int len);	
extern int getchar (void);							 
extern int putchar(int);                     		  

/* Function prototypes */
int strlen(char *);	// Our implementation of strlen
void getline(char *);
void delay(volatile int);
void IRQ_HANDLER(void) __attribute__ ((interrupt ("IRQ")));	/* This attribute alerts the compiler 
that this function will be called when an IRQ interrupt occurs. Extra setup and teardown steps
are needed for an interrupt function - look at the assembly! */

void myStrcpy(char *dst, char *src); //my implementation of strcpy

unsigned int message_times[13];
unsigned int times_index = 0;

unsigned short bit_buffer = 0;
unsigned short bit_index = 0;
bool msg_valid = false;

unsigned char CORRECT_MODE = 0x01;
unsigned char button;
unsigned char mode;

char translation_table[128];  //size = 2^7 (button is really 7-bytes long)
// Important that this table, as a global, is pre-filled with zeros, because that doesn't come up as anything when sent through UART
// So if I enter a button whose translation I haven't defined in this array, the corresponding character is the null character '\0', and nothing gets sent through UART
// or at least nothing shows up on my terminal.  Perhaps the null-character will still be sent to the Raspberry Pi?


void FillTranslationTable(){

	translation_table[0x00] = '1';  //Button 1
	translation_table[0x01] = '2';  //Button 2
	translation_table[0x02] = '3';  //Button 3
	translation_table[0x03] = '4';  //Button 4
	translation_table[0x04] = '5';  //Button 5
	translation_table[0x05] = '6';  //Button 6
	translation_table[0x06] = '7';  //Button 7
	translation_table[0x07] = '8';  //Button 8
	translation_table[0x08] = '9';  //Button 9
	translation_table[0x09] = '0';  //Button 0
	
	translation_table[0x10] = '^';  //Channel Up
	translation_table[0x11] = 'v';  //Channel Down
	translation_table[0x12] = '+';  //Volume up
	translation_table[0x13] = '-';  //Volume Down	
	translation_table[0x14] = 'm';  //Toggle Mute
	translation_table[0x15] = 'p';  //Toggle Power
	translation_table[0x0B] = '\n'; //Enter, make '\r' if I want putchar to write "\r\n", looks nicer on serial terminal GUI, but more annoying for processing on RPi
	translation_table[0x3B] = 'c';  //Prev. Ch button, but use as a clear (opposite of enter, when entering channel numbers)
	
}


int main (void)  {
	
	/* Set clock speed */
	POWKEY1 = 0x01;				// Setting POWKEY1 and POWKEY2 as shown allow you to modify POWCON
	POWCON = 0x00;      	// Set clock speed (41.78 MHz)
	POWKEY2 = 0xF4;
	
	/* Populate Global translation_table */
	FillTranslationTable();
	
	/* Begin GPIO Pin Setup */
	GP0CON = 0x13300000;
	/*
	 * P0.7 is ECLK //indeed a ~40 MHz clock
	 * P0.6 is PLAO[3], will be output of PLA AND, and used as clock input for Timer1
	 * P0.5 is PLA0[2], will be the NOT/negate output 
	 * P0.4 is GPIO IRQ0
 	 */
	
	GP1CON = 0x3311;
	/*
	 * P1.0 is Serial IN
	 * P1.1 is Serial OUT
	 * P1.2 is PLAI[2]
	 * P1.3 is PLAI[3]
	 */
	 

	 /* Will need more for PLA inputs and outputs */
	 /* End GPIO Setup */
	
	
	
	 /* Begin PLA Setup */
	 PLAELM2 = 0b101011;  //NOT B, and bypass flip-flop, B should be IR receiver output, use Element 2 input (P1.2)
	 //Works, with P1.2 as input and P0.5 as output, it inverts
	 //So output of IR Receiver is connected to P1.2
	 
	 
	 PLAELM3 = 0b01001110001;  //A AND B, and bypass flip-flop, A output of PLA element 2, B should be P0.7 ECLK (connected by wire on board to P1.3)
	 //Input is P1.3, output is P0.6 (which will be clock input for Timer1)
	 //Works, with one input as output of PLA Element 2, and other as P1.3
	 //P0.7 (ECLK) is connected to P1.3 via a wire, output on P0.6 (should be able to use on Timer1)
	 
	 PLADIN = 0b1100;
	 PLADOUT = 0b1100;
	 
	 PLAIRQ = 0x12;  //Enable PLA IRQ0 and use PLA Element 2 as the source
	 /* End PLA Setup */
	 
		
	
	 /* Begin Timer1 Setup */
	   /* Try both T1CON's, which would require different usages in the interrupt handler function, but the latter could be simpler */
	 T1CON = 0x7C0;  //P0.6 clock, counting up, periodic (ie loadable), binary format, no pre-scale
	 //Actually, this event time capture, I think just sets reads to the capture register, and won't get me the duration, which is what I want.
	 //In fact, since the PLA IRQ0 interrupt is asserted continuously through the character, the capture register would be continually updated and useless
	 //T1CON = 0x31180;  //Enable event time capture, PLA IRQ0 Event, Core clock, counting up, free-running mode, binary format, no pre-scale
	 T1LD = 0;  //load a 0
	 /* End Timer1 Setup */
	
	
	/* 
	 * UART setup
	 */
	
	/* Setup serial tx & rx pins on P1.0 and P1.1 */
	GP1CON = 0x011;

	/* Start setting up UART at 9600bps */
	COMCON0 = 0x080;				// Setting DLAB
	COMDIV0 = 0x088;				// Setting DIV0 and DIV1 to DL calculated
	COMDIV1 = 0x000;
	COMCON0 = 0x01F;				// Clearing DLAB, enable parity, select even parity

	
	
	GP4DAT = 0x04000000;			// P4.2 configured as an output. LED is turned on
	
	 //Enable interrupts only after everything else has been setup properly
	 /* Begin Interrupt Setup */
	 IRQEN = PLA_IRQ0_BIT;	//PLA IRQ0, which should be active high when the IR Receiver output is LOW (ie a character)
	 /* End Interrupt Setup */


	while(1)
	{
		GP4DAT ^= 0x00040000;		// Complement P4.2
		//putchar('k');  //non-blocking, just writes char to COMTX register, and then UART peripheral will send that char when it can
		//putchar('a');  //start is low, stop is high, LSB sent first, MSB sent last
		//char ch = getchar();
		//putchar('\n');
		//putchar(ch);
		
		//write(0, output1, 2);
		//getline(buffer);
		//write(0, buffer, strlen(buffer));
		//myStrcpy(display, buffer);  //For part 3 of the lab, doesn't use UART, rather sends characters to a display, configured as external memory
		
		//GP4DAT ^= 0x00040000;		// Complement P4.2
		delay(10000);
		//printf("Button: %x, Mode: %x", button, mode);
	}
}

/* Special implementation of strcpy to write to display (configured as external memory) */
void myStrcpy(char *dst, char *src){
	int index = 0;
	while( (src[index] != '\r') && (src[index] != '\n') && (src[index] != '\0') ){
		dst[index] = src[index];
		index++;
	}
}


/* Returns the number of characters in a string, not including the null character */
int strlen(char *strptr) {
	int length = 0;
	while( strptr[length] != '\0' )
		length++;
	return length;
}

/* Receives an entire line of characters and stores them at ptr */
void getline(char *ptr) {
	// Implement this!
	char ch;
	int index = 0;
	while( (ch = getchar()) != '\r' )  //the simulator/UART #1 window only sends a \r when ENTER is hit, so check for \r instead of \n
		ptr[index++] = ch;
	ptr[index++] = '\r';
	ptr[index] = '\0';
}

/* Utility functions */
void delay(volatile int time)
{
	while (time >=0)			// wait loop
    time--;
}

/* **** Interrupt handler ****
 * While this function is running, all interrupts are disabled. 
 */
void IRQ_HANDLER(void) {
	/* If the PLA IRQ0 interrupt was triggered */
	if ((IRQSTA & PLA_IRQ0_BIT) != 0) {			
		while ((IRQSIG & PLA_IRQ0_BIT)) {}  //wait for interrupt to stop being asserted
	  unsigned int timer1_value = T1VAL;
		
		//message_times[times_index++] = timer1_value;
		//Test timer1_value, if 1.2ms, set bit as 1, if 0.6ms, set bit as 0, if 2.4ms, do nothing
		//Will need to do translation once all 13 bits are in
		//Translate 12-bits of message to a character (lookup table?) 
		timer1_value = (timer1_value * 24) / 1000000;  //make timer1_value an integer representation of the time in ms
		switch(timer1_value) {
			case 2:
				//Start of new message
				bit_buffer = 0;
				bit_index = 0;
				msg_valid = true;  //ie, start bit was detected
				break;
			case 1:
				if (!msg_valid) break;  //if the start-bit wasn't detected, bit positioning is off, so skip this message until a start bit is detected
				bit_buffer |= (1 << bit_index++);
				break;
			case 0:
				if (!msg_valid) break;
				bit_index++;
				break;
			default:
				//Do nothing
				break;			
		}
		
		if (bit_index > 11) { //Last bit of message has been received
			button = bit_buffer & 0b1111111;
			mode = (bit_buffer >> 7) & 0b11111;
			if (mode == CORRECT_MODE) { //Then go about translating and sending to UART
					putchar(translation_table[button]);
			}
			
			/* Wait some amount of time, perhaps 100ms, 200ms, before being able to process another button press */
			/* Without this, even my faster push of the button sends that button message 3 times */
			//int time = 700000;
			//while (time >=0)			// wait loop
			//	time--;
			delay(500000);
			//When I do this, the button doesn't get updated properly when I press it
			//It just remains the same as the first button I pressed
			
			
			msg_valid = false;  //reset to false after message has been handled
			bit_index = 0;  //so that if the next message received is caught in the middle (ie start-bit not detected), then it won't re-enter this if stmt
		}
		
		/* Reset Timer1 Load to 0; must be the last line of the function */
		T1LD = 0;  //reset to 0 (will load on first clock tick of next counting cycle, i.e next character in remote message)
	}	
	
}
