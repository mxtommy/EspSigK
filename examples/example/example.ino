
#include "EspSigK.h"



const String hostname  = "Tester";    //Hostname for network discovery
const String ssid      = "mywifi";     //SSID to connect to
const String ssidPass  = "superSecret";  // Password for wifi


EspSigK sigK(hostname, ssid, ssidPass); // create the object

void setup() {
  Serial.begin(115200);

  sigK.setPrintDebugSerial(true);       // default false, causes debug messages to be printed to Serial (connecting etc)
  sigK.setPrintDeltaSerial(true);       // default false, prints deltas to Serial.
  sigK.setServerHost("192.168.0.50");   // Optional. Sets the ip of the SignalKServer to connect to. If not set we try to discover server with mDNS
  //sigK.setServerPort(80);             // If manually setting host, this sets the port for the signalK Server (default 80);
  
  sigK.setServerToken("SUPERSECRETSTRING"); // if you have security enabled in node server, it wont accept deltas unles you auth
                                        // add a user via the admin console, and then run the "signalk-generate-token" script
                                        // included with signalk to generate the string. (or disable security)

  sigK.begin();                         // Start everything. Connect to wifi, setup services, etc...

}

void loop() {

  //Two ways to send deltas, you can do this to send multiple values in the same value. 
  sigK.addDeltaValue("some.signalk.path", 3.413);
  sigK.addDeltaValue("some.other.path", true);
  sigK.sendDelta();
  
  //Or send a single value
  sigK.sendDelta("some.signalk.path", 3.413);

  //try and use this delay function instead of built-in delay()
  //this function will continue handling connections etc instead of blocking
  sigK.safeDelay(1000);

  //if you don't use delays (or the delay function above) call this one every
  //loop in order to handle http/websocket connections etc...
  sigK.handle();


}
