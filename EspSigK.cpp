#include "EspSigK.h"

#define SERIAL_DEBUG_MESSAGE_PREFIX "SigK: "
#define JSON_DESERIALIZE_DELTA_SIZE 384
#define JSON_DESERIALIZE_HTTP_RESPONSE_SIZE 384
#define PREFERENCES_NAMESPACE "EspSigK"


// Server variables
ESP8266WebServer server(80);
websockets::WebsocketsClient webSocketClient;

bool printDeltaSerial;
bool printDebugSerial;
    
bool wsClientConnected;

// Simple web page to view deltas
const char * EspSigKIndexContents = R"foo(
<html>
<head>
  <title>Deltas</title>
  <meta charset="utf-8">
  <script type="text/javascript">
    var WebSocket = WebSocket || MozWebSocket;
    var lastDelta = Date.now();
    var serverUrl = "ws://" + window.location.hostname + ":81";

    connection = new WebSocket(serverUrl);

    connection.onopen = function(evt) {
      console.log("Connected!");
      document.getElementById("box").innerHTML = "Connected!";
      document.getElementById("last").innerHTML = "Last: N/A";
    };

    connection.onmessage = function(evt) {
      var msg = JSON.parse(evt.data);
      document.getElementById("box").innerHTML = JSON.stringify(msg, null, 2);
      document.getElementById("last").innerHTML = "Last: " + ((Date.now() - lastDelta)/1000).toFixed(2) + " seconds";
      lastDelta = Date.now();
    };

    setInterval(function(){
      document.getElementById("age").innerHTML = "Age: " + ((Date.now() - lastDelta)/1000).toFixed(1) + " seconds";
    }, 50);
  </script>
</head>
<body>
  <h3>Last Delta</h3>
  <pre width="100%" height="50%" id="box">Not Connected yet</pre>
  <div id="last"></div>
  <div id="age"></div>

  <p>
    <ul>
      <li><a href="reset_auth">Reset authentication tokens</a></li>
    </ul>
  </p>
</body>
</html>
)foo";

// Response to SignalK authentication reset
const char * EspSigKAuthResetContent = R"foo(
<html>
<head>
  <title>Authentication Reset</title>
  <meta charset="utf-8">
</head>
<body>
<h3>Authentication Reset</h3>
<p>Authentication tokens were removed. The sensor starts the authentication process.</p>
<a href="/">Go back</a>
</body>
</html>
)foo";






/* ******************************************************************** */
/* ******************************************************************** */
/* ******************************************************************** */
/* Constructor/Settings                                                 */
/* ******************************************************************** */
/* ******************************************************************** */
/* ******************************************************************** */
EspSigK::EspSigK(String hostname, String ssid, String ssidPass, WiFiClient * client)
{
  myHostname = hostname;
  mySSID = ssid;
  mySSIDPass = ssidPass;
  wiFiClient = client;

  wsClientConnected = false;

  signalKServerHost = "";
  signalKServerPort = 80;
  signalKServerToken = "";

  printDeltaSerial = false;
  printDebugSerial = false;
  lastPrintDebugSerialHadNewline = true;

  wsClientReconnectInterval = 10000;
  
  timerReconnect  = millis();

  idxDeltaValues = 0; // init deltas
  for (uint8_t i = 0; i < MAX_DELTA_VALUES; i++) { 
    deltaPaths[i] = ""; 
    deltaValues[i] = "";
  }
}

void EspSigK::setServerHost(String newServer) {
  signalKServerHost = newServer;
}
void EspSigK::setServerPort(uint16_t newPort) {
  signalKServerPort = newPort;
}
void EspSigK::setServerToken(String token) {
    signalKServerToken = token;
}
void EspSigK::setPrintDeltaSerial(bool v) {
  printDeltaSerial = v;
}
void EspSigK::setPrintDebugSerial(bool v) {
  printDebugSerial = v;
}
bool EspSigK::isPrintDebugSerial() {
  return printDebugSerial;
}

void EspSigK::printDebugSerialMessage(const char* message, bool newline) {
  if (!printDebugSerial) {
    return;
  }
  
  if (lastPrintDebugSerialHadNewline) Serial.print(SERIAL_DEBUG_MESSAGE_PREFIX);
  newline ? Serial.println(message) : Serial.print(message);
  lastPrintDebugSerialHadNewline = newline;
}
void EspSigK::printDebugSerialMessage(String message, bool newline) {
  if (!printDebugSerial) {
    return;
  }
  
  if (lastPrintDebugSerialHadNewline) Serial.print(SERIAL_DEBUG_MESSAGE_PREFIX);
  newline ? Serial.println(message) : Serial.print(message);
  lastPrintDebugSerialHadNewline = newline;
}
void EspSigK::printDebugSerialMessage(int message, bool newline) {
  if (!printDebugSerial) {
    return;
  }
  
  if (lastPrintDebugSerialHadNewline) Serial.print(SERIAL_DEBUG_MESSAGE_PREFIX);
  newline ? Serial.println(message) : Serial.print(message);
  lastPrintDebugSerialHadNewline = newline;
}


/* ******************************************************************** */
/* ******************************************************************** */
/* ******************************************************************** */
/* Setup                                                                *.
/* ******************************************************************** */
/* ******************************************************************** */
/* ******************************************************************** */


void EspSigK::connectWifi() {
  printDebugSerialMessage(F("Connecting to Wifi.."), false);
  WiFi.begin(mySSID.c_str(), mySSIDPass.c_str());
  while (WiFi.status() != WL_CONNECTED) {
    delay(200);
    printDebugSerialMessage(F("."), false);
  }

  printDebugSerialMessage(F("Connected, IP:"), false);
  printDebugSerialMessage(WiFi.localIP().toString(), true);
}

void EspSigK::setupDiscovery() {
  if (!MDNS.begin(myHostname.c_str())) {             // Start the mDNS responder for esp8266.local
    printDebugSerialMessage(F("Error setting up MDNS responder!"), true);
  } else {
    MDNS.addService("http", "tcp", 80);
    printDebugSerialMessage(F("SIGK: mDNS responder started at "), false);
    printDebugSerialMessage(myHostname, true);
  }
    
  printDebugSerialMessage(F("SIGK: Starting SSDP..."), true);
  SSDP.setSchemaURL("description.xml");
  SSDP.setHTTPPort(80);
  SSDP.setName(myHostname);
  SSDP.setSerialNumber("12345");
  SSDP.setURL("index.html");
  SSDP.setModelName("WifiSensorNode");
  SSDP.setModelNumber("12345");
  SSDP.setModelURL("http://www.signalk.org");
  SSDP.setManufacturer("SigK");
  SSDP.setManufacturerURL("http://www.signalk.org");
  SSDP.setDeviceType("upnp:rootdevice");
  SSDP.begin();
}


/* ******************************************************************** */
/* ******************************************************************** */
/* ******************************************************************** */
/* Run                                                                  */
/* ******************************************************************** */
/* ******************************************************************** */
/* ******************************************************************** */
void EspSigK::begin() {
  printDebugSerialMessage(F("SIGK: Starting as host: "), false);
  printDebugSerialMessage(myHostname, true);

  /* Explicitly set the ESP8266 to be a WiFi-client, otherwise, it by default,
     would try to act as both a client and an access-point and could cause
     network-issues with your other WiFi-devices on your WiFi-network. */
  WiFi.mode(WIFI_STA);
  connectWifi();

  setupSignalKServerToken();

  setupDiscovery();
  setupHTTP();
  setupWebSocket();
}

void EspSigK::handle() {
  yield(); //let the ESP do whatever it needs to...

  //Timers
  uint32_t currentMilis = millis();
  //Overflow handle
  if (timerReconnect > currentMilis) { timerReconnect = currentMilis; }

  // reconnect timers, make sure wifi connected and websocket connected
  if ( (timerReconnect + wsClientReconnectInterval) < currentMilis) {
    if (WiFi.status() != WL_CONNECTED) {
      connectWifi();
    }
    if (!wsClientConnected) {
      connectWebSocketClient(); 
    }
    timerReconnect = currentMilis;
  }


  //HTTP
  server.handleClient();
  //WS
  if (wsClientConnected && webSocketClient.available()) {
    webSocketClient.poll();
  }
}

// our delay function will let stuff like websocket/http etc run instead of blocking
void EspSigK::safeDelay(unsigned long ms)
{
  uint32_t start = millis();

  while (millis() < (start + ms)) {
    handle();
  }
}


/* ******************************************************************** */
/* ******************************************************************** */
/* ******************************************************************** */
/* HTTP                                                                 */
/* ******************************************************************** */
/* ******************************************************************** */
/* ******************************************************************** */
void EspSigK::setupHTTP() {
  printDebugSerialMessage(F("SIGK: Starting HTTP Server"), true);
  server.onNotFound(htmlHandleNotFound);

  server.on("/description.xml", HTTP_GET, [](){ SSDP.schema(server.client()); });
  server.on("/signalk", HTTP_GET, htmlSignalKEndpoints);
  server.on("/signalk/", HTTP_GET, htmlSignalKEndpoints);

  server.on("/",[]() {
      server.send ( 200, "text/html", EspSigKIndexContents );
    });
  server.on("/index.html",[]() {
      server.send ( 200, "text/html", EspSigKIndexContents );
    });
  server.on("/reset_auth",[&]() {
      server.send ( 200, "text/html", EspSigKAuthResetContent );
      signalKServerToken = "";
      preferencesClear();
      setupSignalKServerToken();
    });

  server.begin();
}

void htmlHandleNotFound(){
  server.send(404, "text/plain", "404: Not found"); // Send HTTP status 404 (Not Found) when there's no handler for the URI in the request
}

void htmlSignalKEndpoints() {
  IPAddress ip;
  const int capacity = JSON_OBJECT_SIZE(JSON_DESERIALIZE_DELTA_SIZE);
  StaticJsonDocument<capacity> jsonBuffer;
  char response[2048];
  String wsURL;
  ip = WiFi.localIP();
 
  JsonObject json = jsonBuffer.createNestedObject();
  String ipString = String(ip[0]);
  for (uint8_t octet = 1; octet < 4; ++octet) {
    ipString += '.' + String(ip[octet]);
  }


  wsURL = "ws://" + ipString + ":81/";

  JsonObject endpoints = json.createNestedObject("endpoints");
  JsonObject v1 = endpoints.createNestedObject("v1");
  v1["version"] = "1.alpha1";
  v1["signalk-ws"] = wsURL;
  JsonObject serverInfo = json.createNestedObject("server");
  serverInfo["id"] = "ESP-SigKSen";
  serializeJson(json, response);
  server.send ( 200, "application/json", response);
}

/* ******************************************************************** */
/* ******************************************************************** */
/* ******************************************************************** */
/* Websocket                                                            */
/* ******************************************************************** */
/* ******************************************************************** */
/* ******************************************************************** */
void EspSigK::setupWebSocket() {
  
  webSocketClient.onMessage(webSocketClientMessage);

  connectWebSocketClient();
}

bool EspSigK::getMDNSService(String &host, uint16_t &port) {
  // get IP address using an mDNS query
  printDebugSerialMessage(F("Searching for server via mDNS"), true);
  int n = MDNS.queryService("signalk-ws", "tcp");
  if (n==0) {
    // no service found
    return false;
  } else {
    host = MDNS.IP(0).toString();
    port = MDNS.port(0);
    printDebugSerialMessage(F("Found SignalK Server via mDNS at: "), false);
    printDebugSerialMessage(host, false);
    printDebugSerialMessage(F(":"), false);
    printDebugSerialMessage(port, false);
    return true;
  }
}




void EspSigK::connectWebSocketClient() {
  String host = "";
  uint16_t port = 80;
  String url = "/signalk/v1/stream?subscribe=none";

  if (signalKServerHost.length() == 0) {
    getMDNSService(host, port);
  } else {
    host = signalKServerHost;
    port = signalKServerPort;
  }

  if ( (host.length() > 0) && 
       (port > 0) ) {
    printDebugSerialMessage(F("Websocket client attempting to connect!"), true);
  } else {
    printDebugSerialMessage(F("No server for websocket client"), true);
    return;
  }
  if (signalKServerToken != "") {
    url = url + "&token=" + signalKServerToken;
  }

  wsClientConnected = webSocketClient.connect(host, port, url);
}

void webSocketClientMessage(websockets::WebsocketsMessage message) {
  String payload = message.data();
  Serial.printf("[WSc] get text: %s\n", payload);
  // receiveDelta(payload);
}



/* ******************************************************************** */
/* ******************************************************************** */
/* ******************************************************************** */
/* SignalK                                                              */
/* ******************************************************************** */
/* ******************************************************************** */
/* ******************************************************************** */

void EspSigK::setupSignalKServerToken() {
  if (signalKServerToken == "") {
    char serverToken[256] = "";
    getServerToken(serverToken);
    setServerToken(String(serverToken));
  }
}

void EspSigK::getServerToken(char * token) {
  strcpy(signalKclientId, preferencesGetClientId().c_str());
  printDebugSerialMessage("Client ID: ", false);
  printDebugSerialMessage(signalKclientId, true);

  getRequestHref(signalKclientId, signalKrequestHref);
  getRequestToken(signalKrequestHref, token);

  printDebugSerialMessage("getRequestToken return token: ", false);
  printDebugSerialMessage(token, true);
}

void EspSigK::getRequestHref(const char * clientId, char * requestHref) {
  strcpy(requestHref, preferencesGetRequestHref().c_str());
  if (strcmp(requestHref, "") != 0) {
    printDebugSerialMessage(F("requestHref was found from settings"), true);
    return;
  }

  String requestJson = "{\"clientId\":\"" + String(clientId) + "\",\"description\":\"" + myHostname + "\"}";
  String path = String(F("/signalk/v1/access/requests"));

  signalKAccessResponse accessResponse;

  printDebugSerialMessage(F("Getting access request href..."), false);
  const char * newRequestHref;
  while (strcmp(requestHref, "") == 0) {
    accessResponse = sendAccessRequest(path, true, requestJson);
    printDebugSerialMessage(accessResponse.href, true);
    newRequestHref = accessResponse.href.c_str();
    strcpy(requestHref, newRequestHref);
    printDebugSerialMessage(".", false);
    delay(1000);
  }
  printDebugSerialMessage(requestHref, true);

  preferencesPutRequestHref(requestHref);
}

void EspSigK::getRequestToken(const char * requestHref, char * token) {
  String tokenStr = preferencesGetServerToken();
  if (tokenStr != "") {
    strcpy(token, tokenStr.c_str());
    printDebugSerialMessage(F("serverToken was found from settings"), true);
    return;
  }

  signalKAccessResponse accessResponse;
  String jsonPayload = String("");

  printDebugSerialMessage(F("Getting request token..."), false);

  const char * newServerToken;
  while (strcmp(token, "") == 0) {
    accessResponse = sendAccessRequest(requestHref, false, jsonPayload);
    printDebugSerialMessage("[" + accessResponse.state + "] ", true);
    newServerToken = accessResponse.accessRequestToken.c_str();
    strcpy(token, newServerToken);
    delay(1000);
  }

  printDebugSerialMessage(F("Got token: "), false);
  printDebugSerialMessage(token, true);

  preferencesPutServerToken(token);
}

signalKAccessResponse EspSigK::sendAccessRequest(const String &urlPath, bool isPost, const String &jsonPayload) {
  signalKAccessResponse response;
  response.error = 0;

  printDebugSerialMessage(F("sendAccessRequest called, now connecting to "), false);
  printDebugSerialMessage(signalKServerHost, false);
  printDebugSerialMessage(F(":"), false);
  printDebugSerialMessage(signalKServerPort, false);
  printDebugSerialMessage(urlPath, true);

  printDebugSerialMessage(F("Wifi status: "), false);
  printDebugSerialMessage(WiFi.status(), true);

  if (! wiFiClient->connect(signalKServerHost, signalKServerPort)) {
    printDebugSerialMessage(F("sendAccessRequest could not connect to server"), true);
    response.error = 1;
    return response;
  }

  while (!wiFiClient->connected()) {
    printDebugSerialMessage(F("Waiting while connected to "), false);
    printDebugSerialMessage(urlPath, true);
    delay(50);
  }
 
  if (isPost) {
    printDebugSerialMessage("POST: ", false);
    wiFiClient->println("POST " + urlPath + " HTTP/1.1");
    printDebugSerialMessage(urlPath, true);
  }
  else {
    printDebugSerialMessage("GET: ", false);
    wiFiClient->println("GET " + urlPath + " HTTP/1.1");
    printDebugSerialMessage(urlPath, true);
  }
  wiFiClient->println("Host: " + myHostname);
  if (jsonPayload != "") {
    wiFiClient->println(F("Content-type: application/json"));
    wiFiClient->println("Content-length: " + String(jsonPayload.length()));
    wiFiClient->println(); // end HTTP header
    wiFiClient->println(jsonPayload);
    printDebugSerialMessage(F("Payload: "), false);
    printDebugSerialMessage(jsonPayload, true);
  }
  else {
    printDebugSerialMessage("No payload", true);
  }

  if (wiFiClient->println() == 0) {
    wiFiClient->stop();
    printDebugSerialMessage(F("Error: Could not write to server"), true);
    response.error = 2;
    return response;
  }

  // Read HTTP status
  char httpStatus[32] = {0};
  wiFiClient->readBytesUntil('\r', httpStatus, sizeof(httpStatus));

  // Skip HTTP headers
  char endOfHeaders[] = "\r\n\r\n";
  if (!wiFiClient->find(endOfHeaders)) {
    printDebugSerialMessage(F("Error: Invalid response"), true);
    wiFiClient->stop();
    response.error = 3;
    return response;
  }

  const int capacity = JSON_OBJECT_SIZE(JSON_DESERIALIZE_HTTP_RESPONSE_SIZE);
  DynamicJsonDocument payload(capacity);
  // const int capacityAccessRequest = JSON_OBJECT_SIZE(JSON_DESERIALIZE_HTTP_RESPONSE_SIZE);
  // StaticJsonDocument<capacityAccessRequest> payloadAccessRequest;

  DeserializationError error = deserializeJson(payload, *wiFiClient);
  if (error) {
    printDebugSerialMessage(F("deserializeJson() failed: "), false);
    printDebugSerialMessage(error.f_str(), true);
    wiFiClient->stop();
    response.error = 4;
    return response;
  }

  response.state = String((const char*)payload["state"]);
  response.href = String((const char*)payload["href"]);
  response.accessRequestPermission = String((const char*)payload["accessRequest"]["permission"]);
  response.accessRequestToken = String((const char*)payload["accessRequest"]["token"]);

  printDebugSerialMessage("state: ", false);
  printDebugSerialMessage(response.state, true);
  printDebugSerialMessage("href: ", false);
  printDebugSerialMessage(response.href, true);
  printDebugSerialMessage("accessRequstPermission: ", false);
  printDebugSerialMessage(response.accessRequestPermission, true);
  printDebugSerialMessage("accessRequestToken: ", false);
  printDebugSerialMessage(response.accessRequestToken, true);

  return response;
}

void EspSigK::addDeltaValue(String path, int value) {
  String v = String(value);
  deltaPaths[idxDeltaValues] = path;
  deltaValues[idxDeltaValues] = v;
  idxDeltaValues++;
}
void EspSigK::addDeltaValue(String path, double value) {
  String v = String(value);
  deltaPaths[idxDeltaValues] = path;
  deltaValues[idxDeltaValues] = v;
  idxDeltaValues++;
}
void EspSigK::addDeltaValue(String path, bool value) {
  String v;
  if (value) { v = "true"; } else { v = "false"; }
  deltaPaths[idxDeltaValues] = path;
  deltaValues[idxDeltaValues] = v;
  idxDeltaValues++;
}

void EspSigK::sendDelta(String path, int value) {
  addDeltaValue(path, value);
  sendDelta();
}
void EspSigK::sendDelta(String path, double value) {
  addDeltaValue(path, value);
  sendDelta();
}
void EspSigK::sendDelta(String path, bool value) {
  addDeltaValue(path, value);
  sendDelta();
}

void EspSigK::sendDelta() {
  const int capacity = JSON_OBJECT_SIZE(JSON_DESERIALIZE_DELTA_SIZE);
  StaticJsonDocument<capacity> jsonBuffer;
  String deltaText;

  //  build delta message
  JsonObject delta = jsonBuffer.createNestedObject();

  //updated array
  JsonArray updatesArr = delta.createNestedArray("updates");
  
  JsonObject thisUpdate = updatesArr.createNestedObject();

  JsonObject source = thisUpdate.createNestedObject("source");
  source["label"] = "ESP";
  source["src"] = myHostname;
     
  JsonArray values = thisUpdate.createNestedArray("values");
  for (uint8_t i = 0; i < idxDeltaValues; i++) {
    JsonObject thisValue = values.createNestedObject();
    thisValue["path"] = deltaPaths[i].c_str();
    thisValue["value"] = serialized(deltaValues[i].c_str());
  }

        
  
  serializeJson(delta, deltaText);
  if (printDeltaSerial) Serial.println(deltaText);
  if (wsClientConnected) { // client
    webSocketClient.send(deltaText);
  }
 
  //reset delta info
  idxDeltaValues = 0; // init deltas
  for (uint8_t i = 0; i < MAX_DELTA_VALUES; i++) { 
    deltaPaths[i] = ""; 
    deltaValues[i] = "";
  }
}

void EspSigK::preferencesClear() {
  Preferences preferences;

  printDebugSerialMessage(F("preferencesClear"), true);

  preferences.begin(PREFERENCES_NAMESPACE, false);
  // ESP8266 failed to delete preferences using preferences.clear() so deleting preferences one by one
  preferences.remove("clientId");
  preferences.remove("requestHref");
  preferences.remove("serverToken");
  preferences.end();
}

String EspSigK::preferencesGet(const String &property) {
  Preferences preferences;

  preferences.begin(PREFERENCES_NAMESPACE, false);

  printDebugSerialMessage(F("preferencesGet, property: "), false);
  printDebugSerialMessage(property, false);

  String value = preferences.getString(property.c_str(), "");
  preferences.end();

  printDebugSerialMessage(F(", "), false);
  printDebugSerialMessage(value, true);

  return value;
}

void EspSigK::preferencesPut(const String &property, const String &value) {
  Preferences preferences;

  preferences.begin(PREFERENCES_NAMESPACE, false);
  preferences.putString(property.c_str(), value);
  preferences.end();

  printDebugSerialMessage(F("preferencesPut, property: "), false);
  printDebugSerialMessage(property, false);
  printDebugSerialMessage(F(", value: "), false);
  printDebugSerialMessage(value, true);
}

String EspSigK::preferencesGetClientId() {
  UUID uuid;

  String clientIdPreferences = preferencesGet(F("clientId"));

  if (clientIdPreferences == "") {
    uuid.setRandomMode();
    uuid.generate();
    String newClientId = String(uuid.toCharArray());
    printDebugSerialMessage("New clientId: ", false);
    printDebugSerialMessage(newClientId, true);
    preferencesPut(F("clientId"), newClientId);
    return newClientId;
  }

  return clientIdPreferences;
}

String EspSigK::preferencesGetRequestHref() {
  return preferencesGet(F("requestHref"));
}

void EspSigK::preferencesPutRequestHref(const String &value) {
  preferencesPut(F("requestHref"), value);
}

String EspSigK::preferencesGetServerToken() {
  return preferencesGet(F("serverToken"));
}

void EspSigK::preferencesPutServerToken(const String &value) {
  preferencesPut(F("serverToken"), value);
}
