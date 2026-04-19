//using https://forum.seeedstudio.com/t/xiao-sense-nrf52840-a-guide-on-how-to-set-battery-charging-current-0ma-vs-50ma-vs-100ma-non-mbed/276351


#include <Adafruit_TinyUSB.h>

// Explicitly define pins for the Seeed XIAO nRF52840 non-mbed core
#ifndef VBAT_ENABLE
  #define VBAT_ENABLE 14
#endif
#ifndef PIN_VBAT
  #define PIN_VBAT 32
#endif

#define HIGH_CHARGE_PIN 22  // Pin 22 handles charge current on non-mbed

void setup() {
  Serial.begin(115200);
  

  pinMode(HIGH_CHARGE_PIN, OUTPUT);
  digitalWrite(HIGH_CHARGE_PIN, LOW);  // LOW sets 100mA charge, HIGH sets 50mA
    // Configure the battery enable pin
  pinMode(VBAT_ENABLE, OUTPUT);

  Serial.println("Starting test");
  delay(100);

  // Wait up to 3 seconds for Serial Monitor to connect
  while (!Serial && millis() < 3000); 

  // Force 12-bit resolution (0-4095) to keep our raw numbers predictable
  analogReadResolution(12);
  

  Serial.println("--- Battery Reader Started ---");
}

void loop() {
  // 1. Turn ON the voltage divider (LOW = ON for this specific circuit)
  digitalWrite(VBAT_ENABLE, LOW); 
  delay(10); // Give the circuit a few milliseconds to stabilize
  
  // 2. Read the raw analog value
  int rawVbat = analogRead(PIN_VBAT);
  
  // 3. Immediately turn OFF the voltage divider to save power
  digitalWrite(VBAT_ENABLE, HIGH); 
  
  // 4. Calculate approximate real-world voltage
  // 12-bit ADC means max value is 4095 against a 3.3V internal reference.
  // The XIAO has a built-in voltage divider (1M ohm & 510k ohm) giving a ~2.96 multiplier.
  float voltage = rawVbat * (3.3 / 4095.0) * 2.96;
  
  // 5. Spit it out to the Serial Monitor
  Serial.print("Raw ADC: ");
  Serial.print(rawVbat);
  Serial.print("  |  Estimated Voltage: ");
  Serial.print(voltage);
  Serial.println(" V");
  
  delay(1000); // Read and print once per second
}