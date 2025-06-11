#include <TinyGPS++.h>
#include <SoftwareSerial.h>
#include <EEPROM.h>
// AESLib and SHA256 not needed for initial testing
// #include <AESLib.h>
// #include <SHA256.h>

// Pin definitions
#define GPS_RX_PIN 8        // Connect to GPS TX
#define GPS_TX_PIN 9        // Connect to GPS RX
#define RELAY_PIN 4         // Relay control pin
#define GPS_LED_PIN 2       // LED orange for GPS status indicator
#define RELAY_LED_PIN 3     // LED blue for relay status indicator

// Message markers for simple communication
#define MSG_START '#'
#define MSG_END '$'

// GPS update intervals in milliseconds
#define GPS_UPDATE_INTERVAL 1000   // Check GPS every second
#define PI_UPDATE_INTERVAL 10000   // Send to Pi every 10 seconds (reduced from 60s)

// EEPROM addresses for storing last valid location
#define EEPROM_VALID_FLAG 0    // 1 byte
#define EEPROM_LAT_ADDR 1      // 4 bytes
#define EEPROM_LNG_ADDR 5      // 4 bytes

// The TinyGPS++ object
TinyGPSPlus gps;

// The serial connection to the GPS device
SoftwareSerial gpsSerial(GPS_RX_PIN, GPS_TX_PIN);

// Variables to store GPS data
double latitude = 0.0;
double longitude = 0.0;
bool hasValidLocation = false;
bool currentGpsValid = false;
bool relayState = false;
unsigned long lastGpsUpdate = 0;
unsigned long lastPiUpdate = 0;
unsigned long lastDebugOutput = 0;

// Buffer for incoming messages
char buffer[64];
int bufferIndex = 0;
bool messageStarted = false;

// Function to store location in EEPROM
void storeLocation(double lat, double lng) {
  // Store valid flag
  EEPROM.write(EEPROM_VALID_FLAG, 1);
  
  // Store latitude
  byte* latBytes = (byte*)&lat;
  for (int i = 0; i < sizeof(double); i++) {
    EEPROM.write(EEPROM_LAT_ADDR + i, latBytes[i]);
  }
  
  // Store longitude
  byte* lngBytes = (byte*)&lng;
  for (int i = 0; i < sizeof(double); i++) {
    EEPROM.write(EEPROM_LNG_ADDR + i, lngBytes[i]);
  }
}

// Function to load location from EEPROM
bool loadStoredLocation() {
  // Check if we have valid stored data
  if (EEPROM.read(EEPROM_VALID_FLAG) != 1) {
    return false;
  }
  
  // Read latitude
  byte latBytes[sizeof(double)];
  for (int i = 0; i < sizeof(double); i++) {
    latBytes[i] = EEPROM.read(EEPROM_LAT_ADDR + i);
  }
  latitude = *((double*)latBytes);
  
  // Read longitude
  byte lngBytes[sizeof(double)];
  for (int i = 0; i < sizeof(double); i++) {
    lngBytes[i] = EEPROM.read(EEPROM_LNG_ADDR + i);
  }
  longitude = *((double*)lngBytes);
  
  return true;
}

void setup() {
  // Initialize hardware serial for Pi communication
  Serial.begin(9600);
  
  // Initialize GPS module
  gpsSerial.begin(9600);
  
  // Initialize pins
  pinMode(RELAY_PIN, OUTPUT);
  pinMode(GPS_LED_PIN, OUTPUT);
  pinMode(RELAY_LED_PIN, OUTPUT);
  
  // Set initial states
  digitalWrite(RELAY_PIN, LOW);
  digitalWrite(GPS_LED_PIN, LOW);
  digitalWrite(RELAY_LED_PIN, LOW);
  
  // Try to load last known location from EEPROM
  hasValidLocation = loadStoredLocation();
  
  // If no stored location, use a default location for testing
  if (!hasValidLocation) {
    latitude = 35.501853;
    longitude = 11.054234;
    hasValidLocation = true;
    storeLocation(latitude, longitude);
  }
  
  // Flash LEDs to indicate startup
  for (int i = 0; i < 3; i++) {
    digitalWrite(GPS_LED_PIN, HIGH);
    digitalWrite(RELAY_LED_PIN, HIGH);
    delay(100);
    digitalWrite(GPS_LED_PIN, LOW);
    digitalWrite(RELAY_LED_PIN, LOW);
    delay(100);
  }
  
  // Clear any leftover data in the serial buffer
  while (Serial.available()) {
    Serial.read();
  }
  
  // Send ready message
  Serial.println("NiatoShield CutOff Board Ready");
  Serial.println("Available commands:");
  Serial.println("relayon - Turn relay on");
  Serial.println("relayoff - Turn relay off");
  Serial.println("gps - Request GPS data");
}

void loop() {
  // Read GPS data silently
  while (gpsSerial.available() > 0) {
    if (gps.encode(gpsSerial.read())) {
      if (gps.location.isValid()) {
        latitude = gps.location.lat();
        longitude = gps.location.lng();
        storeLocation(latitude, longitude);
        digitalWrite(GPS_LED_PIN, HIGH);
        delay(20);
        digitalWrite(GPS_LED_PIN, LOW);
      }
    }
  }
  
  // Check for commands from Serial Monitor or Pi
  readSerialCommands();
}

void readSerialCommands() {
  // Check if data is available
  if (Serial.available() > 0) {
    // Wait a bit for the complete command
    delay(10);
    
    // Read from Serial - two methods:
    
    // Method 1: For formatted protocol (from Pi)
    if (Serial.peek() == MSG_START) {
      // Protocol format with markers
      messageStarted = false;
      bufferIndex = 0;
      
      while (Serial.available() > 0) {
        char c = Serial.read();
        
        if (c == MSG_START) {
          messageStarted = true;
          bufferIndex = 0;
        } 
        else if (messageStarted && c == MSG_END) {
          buffer[bufferIndex] = '\0'; // Null terminate the string
          processCommand(buffer);
          return;
        }
        else if (messageStarted && bufferIndex < sizeof(buffer) - 1) {
          buffer[bufferIndex++] = c;
        }
      }
    }
    // Method 2: For direct Serial Monitor input
    else {
      String command = Serial.readStringUntil('\n');
      command.trim();
      
      // Print the received command for debugging
      Serial.print("Received: ");
      Serial.println(command);
      
      // Convert to char array for processing
      command.toCharArray(buffer, sizeof(buffer));
      processCommand(buffer);
    }
  }
}

void sendGpsData() {
  Serial.println("GPS Data:");
  Serial.print("Latitude: ");
  Serial.println(latitude, 6);
  Serial.print("Longitude: ");
  Serial.println(longitude, 6);
  Serial.print("Status: ");
  Serial.println(gps.location.isValid() ? "VALID" : "STORED");
  
  // Also send in protocol format for the Pi
  Serial.print(MSG_START);
  Serial.print("GPS:");
  Serial.print(latitude, 6);
  Serial.print(",");
  Serial.print(longitude, 6);
  Serial.print(",");
  Serial.print(gps.location.isValid() ? "VALID" : "STORED");
  Serial.print(MSG_END);
  Serial.flush();
}

void processCommand(const char* command) {
  // Convert to lowercase for case-insensitive comparison
  char lowerCommand[64];
  strncpy(lowerCommand, command, sizeof(lowerCommand));
  lowerCommand[sizeof(lowerCommand)-1] = '\0';
  
  // Convert to lowercase
  for (int i = 0; lowerCommand[i]; i++) {
    lowerCommand[i] = tolower(lowerCommand[i]);
  }
  
  // Process commands
  if (strstr(lowerCommand, "relayon") != NULL) {
    digitalWrite(RELAY_PIN, HIGH);
    digitalWrite(RELAY_LED_PIN, HIGH);
    relayState = true;
    Serial.println("Relay turned ON");
    
    // For Pi protocol
    Serial.print(MSG_START);
    Serial.print("ACK:RELAY_ON");
    Serial.print(MSG_END);
  } 
  else if (strstr(lowerCommand, "relayoff") != NULL) {
    digitalWrite(RELAY_PIN, LOW);
    digitalWrite(RELAY_LED_PIN, LOW);
    relayState = false;
    Serial.println("Relay turned OFF");
    
    // For Pi protocol
    Serial.print(MSG_START);
    Serial.print("ACK:RELAY_OFF");
    Serial.print(MSG_END);
  }
  else if (strstr(lowerCommand, "gps") != NULL) {
    sendGpsData();
  }
  else {
    Serial.print("Unknown command: ");
    Serial.println(command);
  }
  
  Serial.flush();
} 