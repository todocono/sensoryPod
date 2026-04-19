#include <Adafruit_TinyUSB.h>
#include <SlowSoftI2CMaster.h>

#define MPR121_I2C_ADDRESS 0x5A

// Using your exact I2C pins
SlowSoftI2CMaster si = SlowSoftI2CMaster(D5, D4, true);

void setup() {
  Serial.begin(115200);
  while (!Serial && millis() < 3000);

  if (!si.i2c_init()) {
    Serial.println("I2C Init error!");
    while(1);
  }

  // Basic MPR121 Initialization
  si.i2c_start((MPR121_I2C_ADDRESS << 1) | 0);
  si.i2c_write(0x5E); si.i2c_write(0x00); si.i2c_stop(); // Enter Stop Mode
  delay(1);
  si.i2c_start((MPR121_I2C_ADDRESS << 1) | 0);
  si.i2c_write(0x5E); si.i2c_write(0x0C); si.i2c_stop(); // Enter Run Mode, 12 Electrodes

  // Print the headers so the Serial Plotter can label the graph lines
  Serial.println("ELE0, ELE1, ELE5, ELE6"); 
}

void loop() {
  uint16_t filteredData[12];

  // The raw filtered data starts at register 0x04.
  // Each electrode takes 2 bytes (LSB and MSB), so we read 24 bytes total.
  if (si.i2c_start((MPR121_I2C_ADDRESS << 1) | 0)) {
    si.i2c_write(0x04); 
    
    if (si.i2c_rep_start((MPR121_I2C_ADDRESS << 1) | 1)) {
      for (int i = 0; i < 24; i++) {
        // Read the byte. Send a NACK (true) on the very last byte to end transmission.
        uint8_t val = si.i2c_read(i == 23); 
        
        if (i % 2 == 0) {
          filteredData[i/2] = val; // Least Significant Byte
        } else {
          // Most Significant Byte (only the bottom 2 bits matter for 10-bit data)
          filteredData[i/2] |= ((val & 0x03) << 8); 
        }
      }
    }
    si.i2c_stop();
  }

  
  Serial.print(filteredData[0]); Serial.print(",");
  Serial.print(filteredData[1]); Serial.print(",");
  Serial.print(filteredData[2]); Serial.print(",");
  Serial.print(filteredData[3]); Serial.print(",");
  Serial.print(filteredData[4]); Serial.print(",");
  Serial.print(filteredData[5]); Serial.print(",");
  Serial.print(filteredData[6]); Serial.print(",");
  Serial.print(filteredData[7]); Serial.print(",");
  Serial.print(filteredData[8]); Serial.print(",");
  Serial.println(filteredData[9]);

  delay(50); // Small delay to make the graph readable
}