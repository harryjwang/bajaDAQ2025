#include <SD.h>
#include <sd_defines.h>
#include <sd_diskio.h>

/*  KNOWN ISSUES:
- If the serial monitor ever says "SD card initialization failed" then you must hard reset using pin
- You can reset the board by sending "reset" to the monitor and "stop" will stop transmission of the code

Serial monitor functions -> Should try to figure out how to do this with a digital connection
- Reset: reset the esp32 
- Stop: Stop acquiring any data from the mpu and esp
- Restart: delete the file and remake it

IF YOU WANT TO DELETE ALL FILES FROM STOPPED STATE -> Hard reset the esp board and then send a "restart message"

*/

// Libraries for MPU sensor to let us get acceleration, gyroscope, temp data
#include <Adafruit_MPU6050.h>
#include <Adafruit_Sensor.h>
#include <Wire.h>

// Libraries that relate to storing acceleration data to SD Card
#include <SPI.h>
#include <SD.h>

// Libraries for IR Tachometer sensor
#include <Arduino.h>
const int irSensorPin = 33;

// Set the chip select pin for the sd card to be GPIO 5
#define SD_CS_PIN 5  
#define MPU6050_ADDR 0x68        // -> IMPORTANT: MPU6050 I2C address (can use this for multiple sensors later on)
#define SDA_PIN 22
#define SCL_PIN 21

// Init objects for MPU6050 and SD card so we can work on them
Adafruit_MPU6050 mpu;
File dataFile;

const char* filename = "/accel_data.txt";
char receivedChars[32];       // buffer to let us take in the information text sent to the esp via serial monitor

// Bool variable to be updated once the setup statment gets printed (not pringint right now idk why not)
float secondsCounter = 0;
bool newData = false;
bool acquiringData = true;

// used for heading calcs -> need to figure this out still
const float threshold = 0.15;
float heading = 0.0;

// used for rpm related calculations
int irReading;
volatile float counter;
volatile float rpm;
volatile float numHoles = 8;
volatile float numRotations;

/* ISR */

void IRAM_ATTR isr(){
  counter++;
}

  /* Functions */
void checkMessage() {
  String command = Serial.readStringUntil('\n');

  command.trim();
  command.toLowerCase();

  if (command == "reset"){
    Serial.println("Reseting esp");
    ESP.restart();
  }

  if (command == "stop"){
    Serial.println("Stopping data acquisition");
    MPUSleep();
    acquiringData = false;
  }


  if (command == "restart"){
    Serial.println("Deleting + Remaking File");
    SD.remove(filename);
    Serial.println("Restarting esp");
    ESP.restart();
    dataFile = SD.open(filename, FILE_APPEND);
  }
}

void WakeUpMPU() {
  Wire.beginTransmission(MPU6050_ADDR);
  Wire.write(0x6B);  
  Wire.write(0x01);  // Set clock source to Gyro X (stabilizes readings)
  Wire.endTransmission(true);

  Wire.beginTransmission(MPU6050_ADDR);
  Wire.write(0x6C);  
  Wire.write(0x00);  // Ensure accelerometer and gyroscope are fully enabled
  Wire.endTransmission(true);
}

void MPUSleep() {
  Wire.beginTransmission(MPU6050_ADDR);
  Wire.write(0x6B);  // PWR_MGMT_1 register
  Wire.write(0x40);  // Set SLEEP bit (bit 6) to 1
  Wire.endTransmission(true);
  delay(500);
}

void calculateAndPrintRpm() {
  numRotations = counter / numHoles;
  rpm = numRotations * 60;
  Serial.print("RPM: ");
  Serial.println(rpm);
}

void calculateAndSaveRpm() {
  numRotations = counter / numHoles;
  rpm = numRotations * 60;
  dataFile.println(rpm);
}

/* SETUP */
void setup(void) {
  Serial.begin(9600);  // *IMPORTANT: IN SERIAL MONITOR SET BAUD RATE TO 9600

  pinMode(irSensorPin, INPUT_PULLUP);      // input relative to the esp32
  attachInterrupt(digitalPinToInterrupt(irSensorPin), isr, FALLING);

  Wire.begin(SDA_PIN, SCL_PIN);
  WakeUpMPU();

  while (!Serial) {
    delay(10);  // waits until Serial is ready to be used
  }

  // Try to initialize!
  if (!mpu.begin()) {
    Serial.println("Failed to find MPU6050 chip");
    while (1) {
      delay(10);        // need to reset esp each time to get values again
    }
  }
  Serial.println("MPU6050 Found!");

  if (!SD.begin(SD_CS_PIN)) {
    Serial.println("SD Card initialization failed :(");
    while (1) {
      delay(10);        // need to reset esp each time to get values again
    }
  }
  Serial.println("SD Card Initialized");

  mpu.setAccelerometerRange(MPU6050_RANGE_4_G);
  Serial.print("Accelerometer range set to: ");
  switch (mpu.getAccelerometerRange()) {
    case MPU6050_RANGE_2_G:
      Serial.println("+-2G");
      break;
    case MPU6050_RANGE_4_G:
      Serial.println("+-4G");
      break;
    case MPU6050_RANGE_8_G:
      Serial.println("+-8G");
      break;
    case MPU6050_RANGE_16_G:
      Serial.println("+-16G");
      break;
  }

  mpu.setGyroRange(MPU6050_RANGE_2000_DEG);
  Serial.print("Gyro range set to: ");
  switch (mpu.getGyroRange()) {
  case MPU6050_RANGE_250_DEG:
    Serial.println("+- 250 deg/s");
    break;
  case MPU6050_RANGE_500_DEG:
    Serial.println("+- 500 deg/s");
    break;
  case MPU6050_RANGE_1000_DEG:
    Serial.println("+- 1000 deg/s");
    break;
  case MPU6050_RANGE_2000_DEG:
    Serial.println("+- 2000 deg/s");
    break;
  }

  mpu.setFilterBandwidth(MPU6050_BAND_260_HZ);
  Serial.print("Filter bandwidth set to: ");
  switch (mpu.getFilterBandwidth()) {
    case MPU6050_BAND_260_HZ:
      Serial.println("260 Hz");
      break;
    case MPU6050_BAND_184_HZ:
      Serial.println("184 Hz");
      break;
    case MPU6050_BAND_94_HZ:
      Serial.println("94 Hz");
      break;
    case MPU6050_BAND_44_HZ:
      Serial.println("44 Hz");
      break;
    case MPU6050_BAND_21_HZ:
      Serial.println("21 Hz");
      break;
    case MPU6050_BAND_10_HZ:
      Serial.println("10 Hz");
      break;
    case MPU6050_BAND_5_HZ:
      Serial.println("5 Hz");
      break;
  }



  dataFile = SD.open(filename, FILE_APPEND);
  if (!dataFile) {
    Serial.println("Failed to open file for writing");
    while (1) {
      delay(10);
    }
  }

  dataFile.println("");
  dataFile.println("-----------NEW TEST RUN-----------");

  for (int i = 0; i < 2; i++){
      Serial.println("");
  }
  Serial.println("-----------NEW TEST RUN-----------");

  dataFile.println("Seconds, Accel_X, Accel_Y, Accel_Z, Temp, rpm");
  dataFile.close();

  Serial.println("");
  delay(1000);
}

/* LOOP FUNC */
void loop() {
  secondsCounter += 0.5;

  irReading = digitalRead(irSensorPin);
  dataFile = SD.open(filename, FILE_APPEND);

  /* Get new sensor events with the readings */
  sensors_event_t a, g, temp;
  mpu.getEvent(&a, &g, &temp);

  /* NEED TO FIGURE OUT HEADING CALCS -> WHY GYRO ISN'T WORKING AS EXPECTED */
  // if (abs(a.acceleration.x - 0.1) < threshold && abs(a.acceleration.y + 0.15) < threshold) {
  //     heading = 0;  // Reset heading if the car is stationary in X and Y directions
  //     Serial.println("Reset Heading");
  // }
  // heading -= g.gyro.z;        // subtract because gyro data is opposite than expected

  // if (heading >= 360){
  //   heading -= 360;
  // }

  // if (heading < 0) {
  //   heading += 360; 
  // }

  /* Store data to the SD Card and update if successful */
  if (acquiringData){
    if (dataFile) {
      Serial.println("Data Sent to SD Card");
      dataFile.print(secondsCounter);
      dataFile.print('\t');
      dataFile.print(-(a.acceleration.x + 0.05));
      dataFile.print('\t');
      dataFile.print(-(a.acceleration.y - 0.05));
      dataFile.print('\t');
      dataFile.print(a.acceleration.z + 0.8);
      dataFile.print('\t');
      // dataFile.print(-g.gyro.x);
      // dataFile.print('\t');
      // dataFile.print(-g.gyro.y);
      // dataFile.print('\t');
      // dataFile.print(-g.gyro.z);
      // dataFile.print('\t');
      // dataFile.print(heading);
      // dataFile.print('\t');
      dataFile.print(temp.temperature);
      dataFile.print('\t');
      calculateAndSaveRpm();
      dataFile.close();
    } else {
    Serial.println("Failed to open file for appending");
    }
  } else {
    dataFile.println("Data acquisition stopped");
    delay(100000);
  }
  

  /* Print out the values to Serial Monitor */
  if (acquiringData){
    Serial.print(secondsCounter);
    Serial.print("      ->      X: ");
    Serial.print(-(a.acceleration.x + 0.05));
    Serial.print(", Y: ");
    Serial.print(-(a.acceleration.y - 0.05));
    Serial.print(", Z: ");
    Serial.println(a.acceleration.z + 0.8);
    // Serial.print("Rotation X: ");
    // Serial.print(-g.gyro.x);
    // Serial.print(", Y: ");
    // Serial.print(-g.gyro.y);
    // Serial.print(", Z: ");
    // Serial.print(-g.gyro.z );
    // Serial.println(" degs/s");
    // Serial.print("Relative Heading: ");
    // Serial.println(heading);
    Serial.print("Temperature: ");
    Serial.print(temp.temperature);
    Serial.println(" degC");
    calculateAndPrintRpm();
    Serial.println(" ");
  } else {
    Serial.println("MPU Data stopped");
    delay(100000);
  }

  if (Serial.available() > 0){
    checkMessage();
  }

  Serial.println("");
  counter = 0;
  delay(500);
}