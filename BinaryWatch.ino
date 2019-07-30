/****************************************************************************
 *  binarywatch.inc
 *
 *  Sa Dec 2 19:13:53 2017
 *  Copyright 2017 Dirk Broßwick
 *  Email: dirk.brosswick@googlemail.com
 ****************************************************************************
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 *
 ****************************************************************************
 *
 *  Benötigt Arduino IDE 1.8.2 und die MiniCore Erweiterung, zu finden unter
 *  https://github.com/MCUdude/MiniCore
 *  
 *  Programmierung des ATmega88 erfolgt per ISP
 *
 ****************************************************************************
 *
 * \author Dirk Broßwick
 *
 */
 
#include <avr/io.h>
#include <avr/sleep.h>
#include <avr/interrupt.h>

/*
 * Definition der Zeitbasis für die Uhr
 * 
 * ( 32768 / Prescaler ) / 256
 */
#define TICK_CLOCK      32768
#define TICK_PRESCALER  2             // Prescaller 8
#define TICK_COUNT      8
#define CAL_BASE        170L
#define CAL_PRECIS      100L
#define CAL_KORR        ( 60L * 8L ) * CAL_PRECIS   //4800

/*
 * statische Variablen für die Uhrzeit
 */
// #define RTC_TEST
static long CAL=18;
static long CAL_COUNTER=0;
static int counter=0;
static int SEC=0;
static int MIN=23;
static int HOUR=5;

/*
 * definitionen und Variablen für Blink
 */
#define BLINK_TIME  7 * TICK_COUNT;
static int blink=0;

/*
 * Definitionen und Variables für die Set-Taste
 * KEY_LONG_LEN, zeit in ms um in den SET_MODE zu kommen
 * wie ist die Taste gedrück worden?
 * 0 = keine Tastendruck
 * 1 = kurzer Tastendruck kleiner KEY_LONG_LEN
 * 2 = langer Tastendruck größer KEY_LONG_LEN
 */
#define KEY_LONG_LEN   3 * TICK_COUNT
#define KEY_NONE  0
#define KEY_SHORT 1
#define KEY_LONG  2
static int set_mode=0;
static int set_count=0;
static int set_key=0;

/*
 * Pin-Beschreibung für die Uhrzeit-LED
 */
#define MIN_PIN   0   // Pin der ersten LED für die Minuten
#define MIN_LEN   6   // wie viele Pins
#define HOUR_PIN  14   // Pin der ersten LED für die Stunden
#define HOUR_LEN  4   // wie viele Pins
#define D_LED     19  // Pin für die D LED
#define T_LED     18  // Pin für die T LED
#define TRIGGER   11  // Pin für die D LED
#define CAL_PORT  10  // Pin für die D LED

/*
 * Pin-Beschreibung für die Tasten Set und Blink
 */
#define KEY_SET   6  // Pin für die Set-Taste
#define KEY_BLINK 7  // Pin für die Blink-Taste

/*
 * Setup routine, wird einmal beim start durchlaufen und richtet
 * alles so ein was wichtig ist. z.B. Ausgänge, Eingänge, Timer und so
 * weiter
 */
void setup( void ) {
  /*
   * 32768Hz Timer einrichten auf 1/8 Sekunde ( Datenblatt Kapitel 15, 15.8 und 15.8 )
   */
  TIMSK2  = 0;                                  /* Timerinterrupts sperren */
  ASSR  = (1<<AS2);                             /* 32kHz Quarz einschalten */
  TCNT2=0;                                      /* Timer2 auf 0 stellen */
  TCCR2B = TICK_PRESCALER;                      /* Prescaler einstellen */
    
  /*
   * Pins für LED auf Output stellen und als Test alle einmal beim Batterie einlegen
   * blinken lassen
   */
  for ( int i = MIN_PIN ; i < MIN_PIN+MIN_LEN ; i++ ) {
    pinMode( i, OUTPUT );
    digitalWrite( i, HIGH );
    delay( 50 );
    digitalWrite( i, LOW );
  }
  for ( int i = HOUR_PIN ; i < HOUR_PIN+HOUR_LEN ; i++ ) {
    pinMode( i, OUTPUT );
    digitalWrite( i, HIGH );
    delay( 50 );
    digitalWrite( i, LOW );
  }

  pinMode( TRIGGER, OUTPUT );
  pinMode( CAL_PORT, INPUT );
  digitalWrite( TRIGGER, LOW );

  pinMode( D_LED, OUTPUT );
  digitalWrite( D_LED, HIGH );
  delay( 50 );
  digitalWrite( D_LED, LOW );

  pinMode( T_LED, OUTPUT );
  digitalWrite( T_LED, HIGH );
  delay( 50 );
  digitalWrite( T_LED, LOW );

  /*
   * Pins für Tasten auf Input stellen
   */
  pinMode( KEY_SET, INPUT_PULLUP );
  pinMode( KEY_BLINK, INPUT_PULLUP );

  /*
   * Teil 2 32768hz Timer einrichten
   */
  while (ASSR & ((1<<TCN2UB)|(1<<TCR2BUB)));    /* Warten bis alles fertig */
  TIFR2  = (1<<TOV2);                           /* Timer2 Overflow freigeben */
  TIMSK2  = (1<<TOIE2);                         /* Timer2 Overflow Interrupt freigeben */
  
  blink=70;
}

/*
 * Gibt eine beliebige Zahl (value) binär ab einem Pin (pin) mit der Anzahl (len) der Bits aus
 */
void show_binary( int pin, int len, int value ) {
  /* LED beim Startport durchzählen bis len erreicht ist */
  while( len ) {
    /*
     * Wenn Value maskiert mit 1 gleich 1 ergibt ist das niederwertigste Bit gesetzt -> LED einschalten
     * sonst ausschalten
     */
    if ( value & 1 ) {
      digitalWrite( pin, HIGH );
    }
    else {
      digitalWrite( pin, LOW );
    }

    /*
     * Bits in Value um ein nach rechts schieben, Pin erhöhen
     * len verringern zum zählen
     */
    value=value>>1;
    pin++;
    len--;  
  }
}

/*
 * Die Hauptinterrupt Funktion, diese wird alle 1/8s aufgeruffen
 * Ausgelöst durch den Timer2 der mit 32.768kHz getaktet wird. Siehe Setup();
 */
ISR( TIMER2_OVF_vect) {
  /* 
   * Zeitbasis für die Sekunden bilden, base on 1/8s interrup 
   */
  OCR2A=128; 
   
  CAL_COUNTER += CAL_PRECIS;
  
  if ( CAL_COUNTER >= CAL_KORR )
  {
    digitalWrite( TRIGGER, HIGH );
    digitalWrite( TRIGGER, LOW );
    MIN++;
    if ( MIN == 60 ) {
      MIN=0;
      HOUR++;
      if ( HOUR == 12 ) {
        HOUR=0;
      }
    }
    CAL_COUNTER -= ( CAL_KORR + CAL_BASE + CAL );   
  }


  counter++;

  if ( counter == TICK_COUNT ) {
    SEC++;
    if ( SEC == 60 ) {
      SEC=0;        
    }
    counter=0;
  }

  /*
   * Wenn die Uhrzeit mit Blink angezeigt wird, den Sekundentakt mit ausgeben
   */
  if ( counter <= 2 && set_mode == 0  && blink != 0 ) {
    digitalWrite( T_LED, HIGH );
    digitalWrite( D_LED, HIGH );
  }
  else {
    digitalWrite( T_LED, LOW );
    digitalWrite( D_LED, LOW );
  }
  
  /*
   * wenn blink ungleich 0 oder set_mode ungleich 0, dann Uhrzeit anzeigen, sonst anzeige löschen
   */
  if ( blink != 0 || set_mode != 0 ) {
    if ( set_mode == 3 ) {
      show_binary( MIN_PIN , MIN_LEN, CAL );
      show_binary( HOUR_PIN, HOUR_LEN, 0 );
    }
    else {
      show_binary( MIN_PIN , MIN_LEN, MIN );
      show_binary( HOUR_PIN, HOUR_LEN, HOUR );      
    }
  }
  else {
    show_binary( MIN_PIN , MIN_LEN, 0 );
    show_binary( HOUR_PIN, HOUR_LEN, 0 );    
  }

  /*
   * blink Taste abfragen und blink auf 70 für 70x100ms (7sek) setzen, wenn Taste
   * nicht gedrücke blink bei jeden durchlauf bis auf 0 um 1 dekrementieren
   */
  if ( digitalRead( KEY_BLINK ) == LOW ) {
    blink=BLINK_TIME;
  }
  else {
    if ( blink != 0 ) blink--;
  }

  /*
   * set Taste abfragen und wenn gedrückt set_count um 1 inkrementieren
   * wenn taste losgelassen, feststellen ob lang oder kurz gedrück
   */
  if ( digitalRead( KEY_SET ) == LOW ) {
    set_count++;
  }
  else {
    if ( set_count > KEY_LONG_LEN ) set_key = KEY_LONG;
    else if( set_count > 2 && set_key < KEY_LONG_LEN ) set_key = KEY_SHORT;
    else set_key = KEY_NONE;
    set_count=0;
  }

  /*
   * set_count schon 3 sekunden gedrückt? wenn ja, set mode auf 1 für programmieren setzen
   */
  if ( set_key == KEY_LONG || set_count > KEY_LONG_LEN ) {
    set_mode = 1;
  }

  /*
   * Wenn im Programmiermode und die set-Taste kurz gedrück, mode weiter schalten
   */
  if ( set_mode != 0 && set_key == KEY_SHORT ) {
    switch( set_mode ) {
      case 1:     set_mode = 2;
                  break;
      case 2:     if ( digitalRead( CAL_PORT ) == HIGH ) {
                    set_mode = 3;
                  }
                  else {
                    set_mode = 0;
                  }
                  break;
      default:    set_mode = 0;
                  blink=BLINK_TIME;
                  break;
    }
  }

  /*
   * Programmier-LED je nach mode ansteuern
   */
  switch( set_mode ) {
    case  1:      digitalWrite( T_LED, HIGH );
                  digitalWrite( D_LED, LOW );
                  break;
    case  2:      digitalWrite( T_LED, LOW );
                  digitalWrite( D_LED, HIGH );
                  break;
    case  3:      digitalWrite( T_LED, HIGH );
                  digitalWrite( D_LED, HIGH );
                  break;
    default:      digitalWrite( T_LED, LOW );
                  digitalWrite( D_LED, LOW );
                  break;
  }

  /*
   * Wenn die die Uhr im Set-Mode befindet, je nach Set-Mode
   * die entsprechende Zeit einstellen
   */
  if ( set_mode != 0 && digitalRead( KEY_BLINK ) == LOW && counter % 2 ) {
    switch( set_mode ) {
      case  1:    MIN++;
                  if ( MIN == 60 ) MIN=0;
                  break;
      case  2:    HOUR++;
                  if ( HOUR == 12 ) HOUR=0;
                  break;
      case  3:    CAL++;
                  if ( CAL == 40 ) CAL=0;
                  break;
      default:    break;
    }
  }
}

/*
 * Moin-Loop, hier wird nichts weiter gemacht außer den Controller in 
 * den Schlafmodus zu bringen wenn der Interrupt abgearbeitet worden ist
 */
void loop( void ) {

  /*
   * Power-Save einstellen
   */
  SMCR|=(1<<SM1)|(1<<SM0);

  /*
   * Interrupts freigeben ( Datenblatt Kapiter 7.1 )
   */
  sei();

  /*
   * Endlosschleife zum wieder einschlafen wenn der COntroller durch den Timer2
   * aus den Power-Save geholt wird
   */
  while(1)
  {
        while((ASSR & (1<< OCR2AUB)));  // Warte auf das Ende des Zugriffs
        set_sleep_mode(SLEEP_MODE_PWR_SAVE);
        sleep_mode();                   // in den Schlafmodus wechseln       
  }
}

