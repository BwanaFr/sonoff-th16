#include <Arduino.h>

#include <SonoffI7021.h>

//Includes for the webserver and captive portal
#include <ESPAsyncWebServer.h>
#include <ESPEasyCfg.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>

#define SENSOR_PIN 14

//Objects for captive portal/MQTT
AsyncWebServer server(80);
AsyncEventSource events("/events");
const unsigned long eventPostingInterval = 1000;  // Delay between updates, in milliseconds
static unsigned long lastEventPostTime = 0;
ESPEasyCfg captivePortal(&server, "Sonoff TH16/i7021");
//Custom application parameters
ESPEasyCfgParameterGroup mqttParamGrp("MQTT");
ESPEasyCfgParameter<String> mqttServer("mqttServer", "MQTT server", "");
ESPEasyCfgParameter<String> mqttUser("mqttUser", "MQTT username", "homeassistant");
ESPEasyCfgParameter<String> mqttPass("mqttPass", "MQTT password", "");
ESPEasyCfgParameter<int> mqttPort("mqttPort", "MQTT port", 1883);
ESPEasyCfgParameter<String> mqttName("mqttName", "MQTT name", "TH16");

//Class to read sensor
SonoffI7021 sensor(SENSOR_PIN);
static unsigned long lastSensorRead = 0;
const unsigned long sensorReadingInterval = 1000;


//MQTT objects
WiFiClient espClient;                                   // TCP client
PubSubClient client(espClient);                         // MQTT object
const unsigned long mqttPostingInterval = 10L * 1000L;  // Delay between updates, in milliseconds
static unsigned long mqttLastPostTime = 0;              // Last time you sent to the server, in milliseconds
static char mqttSensorService[128];                     // Status MQTT service name
static char mqttRelayService[128];                      // Relay MQTT service name
uint32_t lastMQTTConAttempt = 0;                        //Last MQTT connection attempt
enum class MQTTConState {Connecting, Connected, Disconnected, NotUsed};
MQTTConState mqttState = MQTTConState::Disconnected;

/**
 * Call back on parameter change
 */
void newState(ESPEasyCfgState state) {
  if(state == ESPEasyCfgState::Reconfigured){
    Serial.println("ESPEasyCfgState::Reconfigured");
    //client.disconnect();
    //Don't use MQTT if server is not filled
    if(mqttServer.getValue().isEmpty()){
      mqttState = MQTTConState::NotUsed;
    }else{
      mqttState = MQTTConState::Connecting;
    }
  }else if(state == ESPEasyCfgState::Connected){
    Serial.println("ESPEasyCfgState::Connected");
    Serial.print("DNS: ");
    Serial.println(WiFi.dnsIP());
  }
}

/**
 * Print value to a JSON string
 */
void publishValuesToJSON(String& str){
  StaticJsonDocument<210> root;
  root["temperature"] = sensor.getTemperature();
  root["humidity"] = sensor.getHumidity();
  root["relay"] = (bool)digitalRead(RELAY_BUILTIN);
  serializeJson(root, str);
}

/**
 * Callback of MQTT
 */
void callback(char* topic, byte* payload, unsigned int length) {
  String data;
  for (unsigned int i = 0; i < length; i++) {
    data += (char)payload[i];
  }
  if(strcmp(topic, mqttRelayService) == 0){
    if(data == "ON"){
      digitalWrite(BUILTIN_RELAY, HIGH);
    }else{
      digitalWrite(BUILTIN_RELAY, LOW);
    }
  }
}

void setup() {
  Serial.begin(115200);
  pinMode(LED_BUILTIN, OUTPUT);
  pinMode(RELAY_BUILTIN, OUTPUT);
  captivePortal.setLedPin(LED_BUILTIN);
  captivePortal.setLedActiveLow(true);
   //Register custom parameters
  mqttPass.setInputType("password");
  mqttParamGrp.add(&mqttServer);
  mqttParamGrp.add(&mqttUser);
  mqttParamGrp.add(&mqttPass);
  mqttParamGrp.add(&mqttPort);
  mqttParamGrp.add(&mqttName);
  captivePortal.addParameterGroup(&mqttParamGrp);
  captivePortal.setStateHandler(newState);
  captivePortal.begin();
  server.begin();

  //MQTT services
  //Build MQTT service names
  snprintf(mqttSensorService, 128, "%s/Values", mqttName.getValue().c_str());
  snprintf(mqttRelayService, 128, "%s/Relay", mqttName.getValue().c_str());
  //Setup MQTT client callbacks and port
  client.setServer(mqttServer.getValue().c_str(), mqttPort.getValue());
  client.setCallback(callback);
  if(mqttServer.getValue().isEmpty()){
    mqttState = MQTTConState::NotUsed;
  }

  //Serve HTTP pages
  captivePortal.setRootHandler([](AsyncWebServerRequest *request){
      request->redirect("/monitor.html");
    });
  server.on("/values", HTTP_GET, [=](AsyncWebServerRequest *request){
      String json;
      publishValuesToJSON(json);
      AsyncWebServerResponse *response = request->beginResponse(200, "application/json", json);
      response->addHeader("Access-Control-Allow-Origin", "*");
      request->send(response);
    });
  server.serveStatic("/monitor.html", SPIFFS, "/monitor.html")
        .setCacheControl("public, max-age=31536000").setLastModified("Mon, 10 Dec 2019 15:33:00 GMT");

  server.addHandler(&events);
}

void publishValuesToMQTT(){
  //Publish to MQTT clients
  if(client.connected()){
    String msg;
    publishValuesToJSON(msg);
    client.publish(mqttSensorService, msg.c_str());
  }
}

void publishValuesToEvents(){
  String msg;
  publishValuesToJSON(msg);
  events.send(msg.c_str(),"value",millis());
}

void reconnect() {
  //Don't use MQTT if server is not filled
  if(mqttServer.getValue().isEmpty()){
    return;
  }
  // Loop until we're reconnected
  if (!client.connected() && ((millis()-lastMQTTConAttempt)>5000)) {
    mqttState = MQTTConState::Connecting;
    IPAddress mqttServerIP;
    int ret = WiFi.hostByName(mqttServer.getValue().c_str(), mqttServerIP);
    if(ret != 1){
      Serial.print("Unable to resolve hostname: ");
      Serial.print(mqttServer.getValue().c_str());
      Serial.println(" try again in 5 seconds");
      lastMQTTConAttempt = millis();
      return;
    }
    Serial.print("Attempting MQTT connection to ");
    Serial.print(mqttServer.getValue().c_str());
    Serial.print(':');
    Serial.print(mqttPort.getValue());
    Serial.print('(');
    Serial.print(mqttServerIP);
    Serial.print(")...");
    // Create a Client ID baased on MAC address
    byte mac[6];                     // the MAC address of your Wifi shield
    WiFi.macAddress(mac);
    String clientId = "SonoffTH16-";
    clientId += String(mac[3], HEX);
    clientId += String(mac[4], HEX);
    clientId += String(mac[5], HEX);
    // Attempt to connect
    client.setServer(mqttServerIP, mqttPort.getValue());
    if((ret == 1) && (client.connect(clientId.c_str(), mqttUser.getValue().c_str(), mqttPass.getValue().c_str()))) {
      Serial.println("connected");
      mqttState = MQTTConState::Connected;
      client.subscribe(mqttRelayService);
    } else {
      Serial.print("failed, rc=");
      Serial.print(client.state());
      Serial.println(" try again in 5 seconds");
      client.disconnect();
      mqttState = MQTTConState::Disconnected;
      lastMQTTConAttempt = millis();
    }
  }
}

void loop() {
  uint32_t now = millis();
  captivePortal.loop();

  //Read sensor if needed
  if((now - lastSensorRead) > sensorReadingInterval){
    if(sensor.read()){
      lastSensorRead = now;
    }
  }

//MQTT management
  if (mqttState != MQTTConState::NotUsed){
      if(!client.loop()) {
        //Not connected of problem with updates
        reconnect();
      }else{
        //Ok, we can publish
        if((now-mqttLastPostTime)>mqttPostingInterval){
          publishValuesToMQTT();
          mqttLastPostTime = now;
        }
      }
  }

//Event listeners
  if((now-lastEventPostTime)>eventPostingInterval){
    publishValuesToEvents();
    lastEventPostTime = now;
  }

  yield();
}