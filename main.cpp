/* File: main.cpp
 * Contains base main function and usually all the other stuff that avr does...
 */
/* Copyright (c) 2012-2013 Domen Ipavec (domen.ipavec@z-v.si)
 *
 * Permission is hereby granted, free of charge, to any person
 * obtaining a copy of this software and associated documentation files
 * (the "Software"), to deal in the Software without restriction,
 * including without limitation the rights to use, copy, modify, merge,
 * publish, distribute, sublicense, and/or sell copies of the Software,
 * and to permit persons to whom the Software is furnished to do so,
 * subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */
 
 #define F_CPU 8000000UL  // 8 MHz
//#include <util/delay.h>

#define SHUTDOWN_TIMEOUT 1500
#define ADC_TIMEOUT 200
#define ADC_STARTUP 10
#define ADC_TIMES 16

#include <avr/io.h>
#include <avr/interrupt.h>
//#include <avr/pgmspace.h>
//#include <avr/eeprom.h> 

#include <stdint.h>

#include "bitop.h"
//#include "shiftOut.h"
#include "io.h"

volatile uint8_t flags = 0;
#define VOLTAGE_WARNING 0
#define VOLTAGE_CUTOFF 1

volatile uint8_t ir_count = 0;
volatile uint16_t ms_count = 0;

int main() {
	// INIT
	// soft power button
	SETBIT(DDRA, PA3);
	SETBIT(PORTA, PA3);
	SETBIT(PORTA, PA1);
	
	// timer 1 for 76khz interrupt and pwm for one led
	TCCR1A = 0b01000000;
	TCCR1B = 0b00001001; // CTC mode 
	OCR1A = 105; // clear
	TIMSK1 = 0b00000010; // ocie1a interrupt
	
	// pwm for leds
	// 0
	//SETBIT(DDRA, PA7);
	// 1
	//SETBIT(DDRB, PB2);
	// 2
	//SETBIT(DDRA, PA6);
	TCCR0A = 0b01010010;
	TCCR0B = 0b00000001;
	OCR0A = 105;
	OCR0B = 105;

	// adc measurement
	ADMUX = 0b10000010;
	ADCSRA = 0b10001111;
	DIDR0 = BIT(ADC2D);

	// led output pin
	avr_cpp_lib::OutputPin ledOut(&DDRA, &PORTA, PA0);
	ledOut.set();

	// enable interrupts
	sei();

	uint8_t button_state = 0;
	uint8_t led_state = 0;

	uint8_t adc_timeout = ADC_STARTUP;
	uint16_t shutdown_timeout = SHUTDOWN_TIMEOUT;
	
	uint8_t ir_state = 0;

	for (;;) {
		
		// ir state machine
		switch (ir_state) {
			case 0: // start first led
				SETBIT(DDRA, PA7);
				ir_state = 1;
				ir_count = 0;
				break;
			case 1: // stop led
				if (ir_count >= 20) {
					CLEARBIT(DDRA, PA7);
					ir_state = 2;
				}
				break;
			case 2: // wait till after possible beginning (just)
				if (ir_count >= 64) {
					ir_state = 3;
				}
				break;
			case 3: // check for signal and timeout
				if (BITCLEAR(PINB, PB1)) {
					ir_state = 4;
				}
				if (ir_count >= 130) {
					ir_count = 0;
					ir_state = 15;
				}
				break;
			case 4: // wait till end of signal
				if (BITSET(PINB, PB1)) {
					ir_state = 5;
					ir_count = 0;
				}
				break;
			case 5: // wait for required time and transmit
				if (ir_count >= 24) {
					ir_state = 6;
					SETBIT(DDRB, PB2);
					ir_count = 0;
				}
				break;
			case 6: // stop signal
				if (ir_count >= 20) {
					CLEARBIT(DDRB, PB2);
					ir_state = 7;
				}
				break;
			case 7: // wait till just after possible start
				if (ir_count >= 64) {
					ir_state = 8;
				}
				break;
			case 8: // check for signal and timeout
				if (BITCLEAR(PINB, PB0)) {
					ir_state = 9;
				}
				if (ir_count >= 130) {
					ir_count = 0;
					ir_state = 15;
				}
				break;
			case 9: // wait till end of signal
				if (BITSET(PINB, PB0)) {
					ir_state = 10;
					ir_count = 0;
				}
				break;
			case 10: // wait required time, transmit
				if (ir_count >= 24) {
					ir_state = 11;
					SETBIT(DDRA, PA6);
					ir_count = 0;
				}
				break;
			case 11:
				if (ir_count >= 20) { // stop led
					CLEARBIT(DDRA, PA6);
					ir_state = 12;
				}
				break;
			case 12:
				if (ir_count >= 64) { // wait to make sure signal stops at other end
					ir_state = 0;
					shutdown_timeout = SHUTDOWN_TIMEOUT;
				}
				break;
				
			case 15: // timeout
				if (ir_count >= 100) {
					ir_state = 0;
				}
				break;
		}
		
		// execute every 50ms
		if (ms_count >= 3810) {
			ms_count = 0;
						
			// shutdown after 75s
			if (shutdown_timeout > 0) {
				shutdown_timeout--;
			} else {
				break;
			}
						
			// adc every 10s
			if (adc_timeout > 0) {
				adc_timeout--;
			} else {
				adc_timeout = ADC_TIMEOUT;
				SETBIT(ADCSRA, ADSC);
			}
			
			// if anything in flags
			if (flags > 0) {
				// voltage cutoff
				if (BITSET(flags, VOLTAGE_CUTOFF)) {
					break;
				}
				
				// blinking led
				if (BITSET(flags, VOLTAGE_WARNING)) {
					led_state++;
					if (led_state == 6) {
						ledOut.set();
						if (led_state == 8) {
							ledOut.clear();
							led_state = 0;
						}
					}
				}
			}
			
			// button
			if (BITSET(PINA, PA1)) {
				button_state = 0;
			} else {
				shutdown_timeout = SHUTDOWN_TIMEOUT;
			
				// shutdown if pressed for 1s				
				if (button_state >= 20) {
					break;
				}

				// button pressed
				button_state++;
			}
		}
	}
	
	// going to shutdown, turn off led
	ledOut.clear();
	
	// wait for button release
	while (BITCLEAR(PINA, PA1));
	CLEARBIT(PORTA, PA3);
}

ISR(TIM1_COMPA_vect) {
	ir_count++;
	ms_count++;
}

ISR(ADC_vect) {
	static uint8_t count = 0;
	static uint16_t accumulator = 0;

	accumulator += ADC;
	count++;
	if (count > ADC_TIMES) {
		if (accumulator < 900*ADC_TIMES) {
			SETBIT(flags, VOLTAGE_WARNING);
			if (accumulator < 850*ADC_TIMES) {
				SETBIT(flags, VOLTAGE_CUTOFF);
			}
		}
		
		count = 0;
		accumulator = 0;
	} else {
		SETBIT(ADCSRA, ADSC);
	}
}
