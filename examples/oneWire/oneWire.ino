#include "EspSigK.h"

#include <OneWire.h>
#include <DallasTemperature.h>
#include <Wire.h>




// set these together! Precision for OneWire
// 9  is 0.5C in 94ms
// 10 is 0.25C in 187ms
// 11 is 0.125C in 375ms
// 12 is 0.0625C in 750ms
#define TEMPERATURE_PRECISION 10
#define ONEWIRE_READ_DELAY 187
#define ONE_WIRE_BUS 13   // D7 pin on ESP
OneWire oneWire(ONE_WIRE_BUS);
DallasTemperature sensors(&oneWire);



const String hostname  = "Tester";    //Hostname for network discovery
const String ssid      = "mywifi";     //SSID to connect to
const String ssidPass  = "superSecret";  // Password for wifi

WiFiClient wiFiClient;
EspSigK sigK(hostname, ssid, ssidPass, &wiFiClient); // create the object

DeviceAddress connectedSensors[10];
uint8_t numberOfDevices = 0;


void setup() {
  Serial.begin(115200);
  sigK.setPrintDeltaSerial(true);
  sigK.begin();                         // Start everything. Connect to wifi, setup services, etc...

  // OneWire
  sensors.begin();
  sensors.setWaitForConversion(false);

  Serial.print("Parasite power is: "); 
  if (sensors.isParasitePowerMode()) Serial.println("ON");
  else Serial.println("OFF");

  Serial.print("1Wire Device precision currently: ");
  Serial.print(sensors.getResolution());
  Serial.print(" setting to ");
  Serial.print(TEMPERATURE_PRECISION);
  sensors.setResolution(TEMPERATURE_PRECISION);
  Serial.println(" Done!");

  oneWireScanBus();

}

void loop() {
  float tempK;
  char strAddress[32];
  String pathT = "environment.inside.refrigerator.temperature.";
  
  //1Wire
  sensors.requestTemperatures();
  sigK.safeDelay(ONEWIRE_READ_DELAY);
  for (uint8_t i = 0; i < numberOfDevices; i++) {
    tempK = sensors.getTempC(connectedSensors[i]) + 273.15;
    addrToString(strAddress, connectedSensors[i]);

    sigK.addDeltaValue(pathT + strAddress, tempK);
  }

  sigK.sendDelta();
  sigK.safeDelay(1000-ONEWIRE_READ_DELAY);
}




void oneWireScanBus() {
  uint8_t tempDeviceAddress[8];
  char strAddress[32];
  sensors.begin(); //needed so the library searches for new sensors that came up since boot
  numberOfDevices = sensors.getDeviceCount();

  for(int i=0;i<numberOfDevices; i++) {
    if(sensors.getAddress(tempDeviceAddress, i))
    {
      addrToString(strAddress, tempDeviceAddress);
      memcpy(connectedSensors[i], tempDeviceAddress, 8);
      Serial.print("OneWire Sensor found: ");
      Serial.print(strAddress);
      Serial.println("");
    }
  }
}


void addrToString(char *strAddress, uint8_t *deviceAddress) {
  // convert to string
  sprintf(strAddress, "%02X:%02X:%02X:%02X:%02X:%02X:%02X:%02X",
    deviceAddress[0],
    deviceAddress[1],
    deviceAddress[2],
    deviceAddress[3],
    deviceAddress[4],
    deviceAddress[5],
    deviceAddress[6],
    deviceAddress[7] );  
}
