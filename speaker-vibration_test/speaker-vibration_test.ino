/*
 * Simple "Bling-Bling" Chime for XIAO nRF52840
 * Speaker/Buzzer connected to Pin D8 
 *  We need to add 1 lowpass filter (a C10uF )
 */

#include <Adafruit_TinyUSB.h>

const int speakerPin = D8; //Or D3, depending on configuration

void playBlingBling() {
  // First note: A high B6 (1976 Hz) for 50ms
  tone(speakerPin, 1976, 50);
  delay(60); // Short gap between notes
  
  // Second note: An even higher E7 (2637 Hz) for 150ms
  tone(speakerPin, 2637, 150);
  delay(150);
  
  noTone(speakerPin); // Ensure the sound stops
}

void setup() {
  pinMode(speakerPin, OUTPUT);
  
  // Play the sound once on startup
  playBlingBling();
}

void loop() {
  // To play it again, you could trigger it with a button or a timer.
  // For this sample, we'll just play it once every 5 seconds.
  delay(5000);
  playBlingBling();
}
