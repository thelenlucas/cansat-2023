/*
Libraries
*/
#include "Adafruit_BMP3XX.h"
#include "SparkFun_Ublox_Arduino_Library.h"
#include <Wire.h>
#include <Servo.h>
#include <Adafruit_BNO055.h>
#include <Math.h>
#include <utility/imumaths.h>


/*
Custom data types
*/

// Holds flight state
enum FlightState {
  LowPower,
  PreLaunch,
  Launch,
  Peak,
  Deployment,
  Parachute,
  Landed
};

// Holds deployment states
enum DeploymentState {
  NotDeployed,
  Deployed
};

// Holds toggle states
enum OptionalToggleState {
  ToggleOn,
  ToggleOff
};

// Holds computer mode
enum FlightMode {
  Flight,
  Simulation
};

// Holds time information
struct TimeStruct {
  int hours;
  int minutes;
  float seconds;
};

// Holds all information used in telemetry packets
struct TelemetryPacket {
  int teamId;
  int utcHours;
  int packetCount;
  TimeStruct missionTime;
  FlightMode computerMode;
  FlightState flightState;
  float altitude;
  DeploymentState probeState;
  DeploymentState heatShieldState;
  DeploymentState parachuteState;
  DeploymentState mastState;
  float temperature;
  float voltage;
  float pressure;
  TimeStruct gpsTime;
  float gpsAltitude;
  float latitude;
  float longitude;
  int satConnections;
  float tiltX;
  float tiltY;
  String cmdEcho;
  OptionalToggleState MRA;
  OptionalToggleState PRS;
  OptionalToggleState URA;
  OptionalToggleState FRA;
  OptionalToggleState BCS;
  OptionalToggleState CAM;
  float orX;
  float orY;
  float orZ;
  float accX;
  float accY;
  float accZ;
  float gsX;
  float gsY;
  float gsZ;
};

// Toggles transmitting
void transmit(String toTransmit) {
  // Prints to Serial
  Serial.println(toTransmit);
  // Prints to XBee
  Serial2.println(toTransmit);
  // Prints to OpenLog
  Serial1.println(toTransmit);
}

// Flight-time variables
FlightState flightState; // Holds the determinant for state change logic
FlightMode computerMode; // Holds the determinant for sim change logic

TelemetryPacket telemetry; // Holds telemetry packets

OptionalToggleState MRA;
OptionalToggleState PRS;
OptionalToggleState URA;
OptionalToggleState FRA;
OptionalToggleState BCS;
OptionalToggleState CAM;

TimeStruct currentTime; // Holds global time
TimeStruct gpsTime; // Time as seen by GPS

float altitude; // Altitude of cansat
float lastAltitude; // Altitude at last logic step
float vertVelocity; // Velocity upwards
float calibrationAltitude; // To find absolute

float temperature; // Temperature (C)
float pressure; // Pressure (hPa)
float voltage; // Voltage (volts)
int adcVol; // voltage (unitless)

float gpsAltitude; // Altitude as measured by GPS (ASL)
float latitude; // GPS read latitude
float longitude; // GPS read longitude

float orX;
float orY;
float orZ;
float accX;
float accY;
float accZ;
float gsX;
float gsY;
float gsZ;

SFE_UBLOX_GPS gps; // gps
Adafruit_BNO055 bno = Adafruit_BNO055(55, 0x28, &Wire);

Adafruit_BMP3XX bmp; // BMP388 Sensor for pressure/temperature
#define SEALEVELPRESSURE_HPA 1000

unsigned long currentCycleTime; // Used for time update every cycle
unsigned long lastCycleTime; // Used to calculate cycleTimeGap
unsigned long cycleTimeGap; // Time between cycles
unsigned long deltaTime; // How much time has passed between logic steps

bool transmitting = true; // Are we transmitting packets?
bool simulation = false; // Are we simulating?
bool simulationArmed = false;
float simulatedPressure; // Whats the simulated pressure?

// Definitions
#define PACKET_GAP_TIME 1000
#define BNO055_SAMPLERATE_DELAY_MS (50)
#define TEAM_ID 1073

// ADC pin for voltage
#define ADC_GPIO_PIN 26

// Release servo
Servo ReleaseServo;
#define RELEASE_PWM_PIN 15
#define RELEASE_MOS_PIN 14
#define RELEASE_CLOSED 0
#define RELEASE_FALL 40
#define RELEASE_PARACHUTE 100

int packetCount;

class CommandHandler {
  public:
  String buffer;
  String command;
  String arg;

  CommandHandler() {
    buffer = "";
  }

  void addToBuffer() {
    while (Serial2.available() > 0) {
      char c = (char)Serial2.read();
      Serial.print(c);
    if (c == ';') {
        Serial.print("\n");
        detectCommand(buffer);
        buffer = "";
      } else {
        buffer += (char)c;
      }
    }
  }

  void detectCommand(String buffer) {
    // find the locations of the commas
    int first_comma = buffer.indexOf(',');
    int second_comma = buffer.indexOf(',', first_comma + 1);
    int third_comma = buffer.indexOf(',', second_comma + 1);

    String cmd = buffer.substring(second_comma + 1, third_comma);
    Serial.print("COMMAND: ");
    Serial.print("|");
    Serial.print(cmd);
    Serial.println("|");
    String arg = "";
    if (third_comma != -1) {
      arg = buffer.substring(third_comma+1);
      Serial.print("ARG: ");
      Serial.println(arg);
    }

    if (cmd == "CX") {
      CX(arg);
    } else if (cmd == "CAL") {
      CAL(arg);
    } else if (cmd == "ECHO") {
      ECHO(arg);
    } else if (cmd == "ST") {
      ST(arg);
    } else if (cmd == "FSS") {
      FSS(arg);
    } else if (cmd == "ST") {
      ST(arg);
    } else if (cmd == "SIM") {
      SIM(arg);
    } else if (cmd == "SIMP") {
      SIMP(arg);
    } else if (cmd == "MRA") {
      MRA(arg);
    } else if (cmd == "PRS") {
      PRS(arg);
    } else if (cmd == "URA") {
      URA(arg);
    } else if (cmd == "FRA") {
      FRA(arg);
    } else if (cmd == "BCS") {
      BCS(arg);
    } else if (cmd == "CAM") {
      CAM(arg);
    }

    telemetry.cmdEcho = String(cmd);
  }

  void ECHO(String arg) {
    transmit(arg);
  }

  void CX(String arg) {
    if (arg == "ON") {
      transmitting = true;
    } else {
      transmitting = false;
    }
  }

  void CAL(String arg) {
    calibrationAltitude = bmp.readAltitude(SEALEVELPRESSURE_HPA);
  }

  void FSS(String arg) {
    if (arg == "LOW_POWER") {
      flightState = LowPower;
      digitalWrite(RELEASE_MOS_PIN, LOW);
    } else if (arg == "PRE_LAUNCH") {
      flightState = PreLaunch;
    } else if (arg == "LAUNCH") {
      flightState = Launch;
    } else if (arg == "PEAK") {
      flightState = Peak;
    } else if (arg == "DEPLOYMENT") {
      flightState = Deployment;
    } else if (arg == "PARACHUTE") {
      flightState = Parachute;
    } else if (arg == "LANDED") {
      flightState = Landed;
    }
  }

  void ST(String arg) {
    int hour = buffer.substring(0, 2).toInt();
    int minute = buffer.substring(3, 5).toInt();
    float second = buffer.substring(6).toFloat();

    currentTime.hours = hour;
    currentTime.minutes = minute;
    currentTime.seconds = second;
  }

  void SIM(String arg) {
    if (arg == "ENABLE") {
      simulationArmed = true;
    } else if (arg == "ACTIVATE" && simulationArmed) {
      simulation = true;
    } else if (arg == "DISABLE") {
      simulationArmed = false;
      simulation = false;
    }
  }

  void SIMP(String arg) {
    if (simulation) {
      simulatedPressure = arg.toFloat();
    }
  }

  // Manual Release Activation
  void MRA(String arg) {
    if (arg == "ON") {
      telemetry.MRA = ToggleOn;
      telemetry.heatShieldState = Deployed;
      digitalWrite(RELEASE_MOS_PIN, HIGH);
      ReleaseServo.write(RELEASE_FALL);
      digitalWrite(RELEASE_MOS_PIN, HIGH);
      digitalWrite(19, HIGH);
      digitalWrite(20, HIGH);
      delay(500);
      digitalWrite(19, LOW);
      digitalWrite(20, LOW);
    } else if (arg == "OFF") {
      telemetry.MRA = ToggleOff;
      digitalWrite(RELEASE_MOS_PIN, HIGH);
      ReleaseServo.write(RELEASE_CLOSED);
      digitalWrite(RELEASE_MOS_PIN, HIGH);
      digitalWrite(19, HIGH);
      digitalWrite(20, HIGH);
      delay(500);
      digitalWrite(19, LOW);
      digitalWrite(20, LOW);
    }
  }

  void PRS(String arg) {
    if (arg == "ON") {
      telemetry.PRS = ToggleOn;
      telemetry.parachuteState = Deployed;
      digitalWrite(RELEASE_MOS_PIN, HIGH);
      ReleaseServo.write(RELEASE_PARACHUTE);
      digitalWrite(RELEASE_MOS_PIN, HIGH);
      digitalWrite(19, HIGH);
      digitalWrite(20, HIGH);
      delay(500);
      digitalWrite(19, LOW);
      digitalWrite(20, LOW);
    } else if (arg == "OFF") {
      telemetry.PRS = ToggleOff;
      digitalWrite(RELEASE_MOS_PIN, HIGH);
      ReleaseServo.write(RELEASE_CLOSED);
      digitalWrite(RELEASE_MOS_PIN, HIGH);
      digitalWrite(19, HIGH);
      digitalWrite(20, HIGH);
      delay(500);
      digitalWrite(19, LOW);
      digitalWrite(20, LOW);
    }
  }

  void URA(String arg) {
    if (arg == "ON") {
      telemetry.URA = ToggleOn;
    }
    if (arg == "OFF") {
      telemetry.URA = ToggleOff;
    }
  }

  void FRA(String arg) {
    if (arg == "ON") {
      telemetry.FRA = ToggleOn;
      telemetry.mastState = Deployed;
    }
    if (arg == "OFF") {
      telemetry.FRA = ToggleOff;
      telemetry.mastState = NotDeployed;
    }
  }

  void CAM(String arg) {
    if (arg == "ON") {
      telemetry.CAM = ToggleOn;
    }
    if (arg == "OFF") {
      telemetry.CAM = ToggleOff;
    }
  }

  void BCS(String arg) {
    if (arg == "ON") {
      telemetry.BCS = ToggleOn;
      digitalWrite(19, HIGH);
      digitalWrite(20, HIGH);
    }
    if (arg == "OFF") {\
      telemetry.BCS = ToggleOff;
      digitalWrite(19, LOW);
      digitalWrite(20, LOW);
    }
  }
};


// Commander for the flight computer
CommandHandler commander;

void setup() {
  Serial.begin(9600); // Open serial line TODO: Remove this in favor of xbee
  Serial2.setTX(8);
  Serial2.setRX(9);
  Serial2.begin(9600); // Start Xbee Line

  // Release Servo Setup
  pinMode(RELEASE_MOS_PIN, OUTPUT);
  ReleaseServo.attach(RELEASE_PWM_PIN, 420, 2400);
  digitalWrite(RELEASE_MOS_PIN, LOW);
  ReleaseServo.write(RELEASE_CLOSED);

  flightState = LowPower;
  computerMode = Flight;

  MRA = ToggleOff;
  PRS = ToggleOff;
  URA = ToggleOff;
  FRA = ToggleOff;
  BCS = ToggleOff;
  CAM = ToggleOff;

  altitude = 0.0;
  lastAltitude = 0.0;
  vertVelocity = 0.0;

  temperature = 0;
  pressure = 0;

  packetCount = 0;

  lastCycleTime = millis(); // Prime cycle execution
  deltaTime = 0; // Immediatly start flight logic

  // Setup I2c
  Wire.setSDA(16);
  Wire.setSCL(17);
  Wire.begin();

  // Openlog Serial
  Serial1.setTX(12);
  Serial1.setRX(13);
  Serial1.begin(9600);

  // Begin BMP
  if (!bmp.begin_I2C(0x77, &Wire)) {
    while (true) {
      Serial.println("BMP ERROR");
      delay(1000);
    }
  }
  bmp.performReading();

  delay(100);

  // Begin GPS
  if (gps.begin(Wire) == false) //Connect to the Ublox module using Wire port
  {
    while (true) {
      Serial.println("GPS ERROR");
      delay(1000);
    }
  }
  gps.setI2COutput(COM_TYPE_UBX); //Set the I2C port to output UBX only (turn off NMEA noise)
  gps.saveConfiguration(); //Save the current settings to flash and BBR

  // Initialise bno
  if (!bno.begin())
  {
    while (true) {
      Serial.print("Ooops, no BNO055 detected ... Check your wiring or I2C ADDR!");
      delay(1000);
    }
  }

  bno.setExtCrystalUse(true);

  // Create command handler
  commander = CommandHandler();

  // Buzzer & LED pin
  pinMode(19, OUTPUT);
  pinMode(20, OUTPUT);

  digitalWrite(19, HIGH);
  digitalWrite(20, HIGH);
  delay(100);
  digitalWrite(19, LOW);
  digitalWrite(20, LOW);
  delay(100);
  digitalWrite(19, HIGH);
  digitalWrite(20, HIGH);
  delay(100);
  digitalWrite(19, LOW);
  digitalWrite(20, LOW);
  delay(100);
  digitalWrite(19, HIGH);
  digitalWrite(20, HIGH);
  delay(100);
  digitalWrite(19, LOW);
  digitalWrite(20, LOW);
  delay(100);
}

void loop() {
  // Update our timeing control
  currentCycleTime = millis();
  cycleTimeGap = currentCycleTime - lastCycleTime;
  deltaTime += cycleTimeGap;

  // If one second has passed, perform flight logic
  if (deltaTime >= PACKET_GAP_TIME) {
    // Calculate current time
    updateTime(currentTime, deltaTime);

    // Read pressure and temperature from BMP
    bmp.performReading();
    temperature = bmp.temperature;
    vertVelocity = (altitude - lastAltitude) / ((float)deltaTime / 1000.0); // calculate velocity

    if (!simulation) {
      altitude = bmp.readAltitude(SEALEVELPRESSURE_HPA) - calibrationAltitude;
      pressure = bmp.pressure;
    } else {
      altitude = pressureToAltitude(simulatedPressure);
      pressure = simulatedPressure;
    }

    // Read GPS values
    latitude = gps.getLatitude();
    longitude = gps.getLongitude();
    gpsAltitude = gps.getAltitude();

    gpsTime.hours = gps.getHour();
    gpsTime.minutes = gps.getMinute();
    gpsTime.seconds = gps.getSecond();

    sensors_event_t orientationData, accelerometerData, gravityData;

    imu::Vector<3> euler = bno.getVector(Adafruit_BNO055::VECTOR_EULER);
    imu::Vector<3> acc = bno.getVector(Adafruit_BNO055::VECTOR_ACCELEROMETER);
    imu::Vector<3> gyro = bno.getVector(Adafruit_BNO055::VECTOR_GYROSCOPE);

    orX = euler.x();
    orY = euler.y();
    orZ = euler.z();
    accX = acc.x();
    accY = acc.y();
    accZ = acc.z();
    gsX = gyro.x();
    gsY = gyro.y();
    gsZ = gyro.z();

    // Analog read
    adcVol = analogRead(ADC_GPIO_PIN);
    // ADC to V w/ Voltage Divider = (ADC Output / Maximum ADC Output) x Voltage Across R1
    voltage = (3.3 * (float)adcVol / 1023) * ((50 + 47) / 50);

    commander.addToBuffer();
    
    // Flight state changes
    if (flightState == LowPower) {
      digitalWrite(19, LOW);
      digitalWrite(20, LOW);
      // Do nothing, and don't do flight logic
    } else if (flightState == PreLaunch) {

      // When 10m passed and velocity greater tha3n 10m/s, move to launch
      if (altitude >= 10.0 && vertVelocity >= 0.0) {
        // Start camera recording (TODO)
        // Start transmitting packets
        transmitting = true;
        // Move into launch state
        flightState = Launch;
        digitalWrite(19, HIGH);
        digitalWrite(20, HIGH);
        delay(100);
        digitalWrite(19, LOW);
        digitalWrite(20, LOW);
      }

      // Keep servos on
      digitalWrite(RELEASE_MOS_PIN, LOW);
      ReleaseServo.write(RELEASE_CLOSED);
      digitalWrite(RELEASE_MOS_PIN, HIGH);

      } else if (flightState == Launch) {

      // When above 550m, transition to this stage (present to avoid accidental movment directly into landing stage)
      if (altitude >= 500.0) {
        flightState = Peak;
        digitalWrite(19, HIGH);
        digitalWrite(20, HIGH);
        delay(100);
        digitalWrite(19, LOW);
        digitalWrite(20, LOW);
      }

      // Servos on and closed
      digitalWrite(RELEASE_MOS_PIN, HIGH);
      ReleaseServo.write(RELEASE_CLOSED);

    } else if (flightState == Peak) {

      // Peak stage transitions to deployment when below 500m and velocity is <0m/s
      if (altitude < 500.0) {
        // Update telemetry
        telemetry.heatShieldState = Deployed;
        telemetry.MRA = ToggleOn;
        // Move to deploymment state
        flightState = Deployment;
        digitalWrite(19, HIGH);
        digitalWrite(20, HIGH);
        delay(100);
        digitalWrite(19, LOW);
        digitalWrite(20, LOW);
      }

      // Servos on and closed
      digitalWrite(RELEASE_MOS_PIN, HIGH);
      ReleaseServo.write(RELEASE_CLOSED);

    } else if (flightState == Deployment) {

      // Move to parachute state when below 200m and negative velocity
      if (altitude < 200.0) {
        // Move to chute stage
        flightState = Parachute;
        telemetry.PRS = ToggleOn;
        telemetry.parachuteState = Deployed;
        digitalWrite(19, HIGH);
        digitalWrite(20, HIGH);
        delay(100);
        digitalWrite(19, LOW);
        digitalWrite(20, LOW);
      }

      // Release servo open
      digitalWrite(RELEASE_MOS_PIN, HIGH);
      ReleaseServo.write(RELEASE_FALL);

    } else if (flightState == Parachute) {

      // Just wait until velocity is less than 2
      if (altitude < 10) {
        // Move to landed state
        flightState = Landed;
        // Execute wacky subroutine (TODO)
        digitalWrite(19, HIGH);
        digitalWrite(20, HIGH);
        delay(100);
        digitalWrite(19, LOW);
        digitalWrite(20, LOW);
      }

      // Chute servo open
      digitalWrite(RELEASE_MOS_PIN, HIGH);
      ReleaseServo.write(RELEASE_PARACHUTE);

    } else if (flightState == Landed) {
    // Beacons
    digitalWrite(19, HIGH);
    digitalWrite(20, HIGH);
    telemetry.BCS = ToggleOn;
    }
  }

  telemetry.teamId = TEAM_ID;
  telemetry.missionTime = currentTime;
  telemetry.packetCount = packetCount;
  telemetry.flightState = flightState;
  telemetry.altitude = altitude;
  telemetry.temperature = temperature;
  telemetry.voltage = voltage;
  telemetry.pressure = pressure;

  TimeStruct gpsTime;
  gpsTime.hours = gps.getHour();
  gpsTime.minutes = gps.getMinute();
  gpsTime.seconds = (float)gps.getSecond();
  
  telemetry.gpsTime = gpsTime;
  telemetry.gpsAltitude = gpsAltitude;
  telemetry.latitude = latitude;
  telemetry.longitude = longitude;
  telemetry.satConnections = gps.getSIV();
  telemetry.tiltX = orX;
  telemetry.tiltY = orY;
  telemetry.MRA = MRA;
  telemetry.PRS = PRS;
  telemetry.URA = URA;
  telemetry.FRA = FRA;
  telemetry.BCS = BCS;
  telemetry.CAM = CAM;
  telemetry.orX = orX;
  telemetry.orY = orY;
  telemetry.orZ = orZ;
  telemetry.accX = accX;
  telemetry.accY = accY;
  telemetry.accZ = accZ;
  telemetry.gsX = gsX;
  telemetry.gsY = gsY;
  telemetry.gsZ = gsZ;

  // Assemble packet
  String packet = assemblePacket(telemetry);

  // Convert to bytes
  // TODO: Convert all strings in here to bytes natively
  uint8_t packetBytes[packet.length()];
  packet.getBytes(packetBytes, packet.length());

  if (transmitting) {
    transmit(packet);

    packetCount++;
  }

  // Update timing
  deltaTime = 0;

  lastCycleTime = currentCycleTime;
};

// Creates a string from a time structure
String formatTime(TimeStruct tStruct) {
  String out = "";
  if (tStruct.hours < 10) {
    out += "0";
  }
  out += String(tStruct.hours);
  out += ":";

  if (tStruct.minutes < 10) {
    out += "0";
  }
  out += String(tStruct.minutes);
  out += ":";

  if (tStruct.seconds < 10) {
    out += "0";
  }
  out += String(tStruct.seconds, 2);

  return out;
};

// Function to assemble a telemetry packet
String assemblePacket(TelemetryPacket telemetry) {
  String packet = "";
  packet += telemetry.teamId;
  packet += ",";

  packet += formatTime(telemetry.missionTime);
  packet += ",";

  packet += String(telemetry.packetCount);
  packet += ",";

  if (telemetry.computerMode == Flight) {
    packet += "F";
  } else if (telemetry.computerMode == Simulation) {
    packet += "S";
  } else {
    packet += "MODE_ERROR";
  }
  packet += ",";

  if (telemetry.flightState == LowPower) {
    packet += "LOW_POWER";
  } else if (telemetry.flightState == PreLaunch) {
    packet += "PRE_LAUNCH";
  } else if (telemetry.flightState == Launch) {
    packet += "LAUNCH";
  } else if (telemetry.flightState == Peak) {
    packet += "PEAK";
  } else if (telemetry.flightState == Deployment) {
    packet += "DEPLOYMENT";
  } else if (telemetry.flightState == Parachute) {
    packet += "PARACHUTE";
  } else if (telemetry.flightState == Landed) {
    packet += "LANDED";
  }
  packet += ",";

  packet += String(telemetry.altitude, 1);
  packet += ",";

  if (telemetry.probeState == Deployed) {
    packet += "P";
  } else {
    packet += "N";
  }
  packet += ",";

  if (telemetry.parachuteState == Deployed) {
    packet += "C";
  } else {
    packet += "N";
  }
  packet += ",";

  if (telemetry.mastState == Deployed) {
    packet += "M";
  } else {
    packet += "N";
  }
  packet += ",";

  packet += String(telemetry.temperature, 1);
  packet += ",";

  packet += String(telemetry.voltage, 1);
  packet += ",";

  packet += String(telemetry.pressure, 2);
  packet += ",";

  packet += formatTime(telemetry.gpsTime);
  packet += ",";

  packet += String(telemetry.gpsAltitude, 1);
  packet += ",";

  packet += String((int)telemetry.latitude);
  packet += ",";

  packet += String((int)telemetry.longitude);
  packet += ",";

  packet += String(telemetry.satConnections);
  packet += ",";

  packet += String(telemetry.tiltX, 2);
  packet += ",";

  packet += String(telemetry.tiltY, 2);
  packet += ",";

  packet += telemetry.cmdEcho;
  packet += ",,";

  if (telemetry.MRA == ToggleOn) {
    packet += "On";
  } else {
    packet += "Off";
  }
  packet += ",";

  if (telemetry.PRS == ToggleOn) {
    packet += "On";
  } else {
    packet += "Off";
  }
  packet += ",";
  
  if (telemetry.URA == ToggleOn) {
    packet += "On";
  } else {
    packet += "Off";
  }
  packet += ",";

  if (telemetry.FRA == ToggleOn) {
    packet += "On";
  } else {
    packet += "Off";
  }
  packet += ",";

  if (telemetry.CAM == ToggleOn) {
    packet += "On";
  } else {
    packet += "Off";
  }
  packet += ",";
  
  if (telemetry.BCS == ToggleOn) {
    packet += "On";
  } else {
    packet += "Off";
  }
  packet += ",";

  packet += String(telemetry.orX);
  packet += ",";

  packet += String(telemetry.orY);
  packet += ",";

  packet += String(telemetry.orZ);
  packet += ",";

  packet += String(telemetry.accX);
  packet += ",";

  packet += String(telemetry.accY);
  packet += ",";

  packet += String(telemetry.accZ);
  packet += ",";

  packet += String(telemetry.gsX);
  packet += ",";
  
  packet += String(telemetry.gsY);
  packet += ",";
  
  packet += String(telemetry.gsZ);
  
  return packet;
};

// Function to conver pressure to altitude
// The standard pressure at sea level in pascals
const double p0 = 101325.0;

// The temperature lapse rate in K/m
const double lapse = -0.0065;

// The universal gas constant
const double R = 8.31447;

// The molar mass of dry air in kg/mol
const double M = 0.0289644;

// The gravitational acceleration in m/s^2
const double g = 9.80665;

// The standard temperature at sea level in K
const double T0 = 288.15;

double pressureToAltitude(float p) {
  p *= 100;
  return T0 / lapse * (1 - pow(p / p0, R * lapse / (g * M)));
}

// Creates a fresh time structure without logical garbage
TimeStruct createBlankTime() {
  TimeStruct t;
  t.hours = 0;
  t.minutes = 0;
  t.seconds = 0.0;
  return t;
};

// Keeps track of time using millis() updates
void updateTime(TimeStruct &toUpdate, unsigned long timeDeltaMillis) {
  float timeDeltaSeconds = (float)timeDeltaMillis / 1000.0;

  toUpdate.seconds += timeDeltaSeconds;
  if (toUpdate.seconds >= 60.0) {
    toUpdate.seconds -= 60.0;
    toUpdate.minutes += 1;
  }

  if (toUpdate.minutes >= 60) {
    toUpdate.minutes -= 60;
    toUpdate.hours += 1;
  }

  if (toUpdate.hours >= 24) {
    toUpdate.hours -= 24;
  }
}
