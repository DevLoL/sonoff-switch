#include "Arduino.h"

#include <ESP8266WiFi.h>
#include <ESP8266WiFiMulti.h>
#include <PubSubClient.h>

extern "C" {
#include "user_interface.h"
}

#include "wifi_credentials.h"

#define SWITCH_NUMBER_STRING "3"

#define RELAY_PIN  12
#define BUTTON_PIN 0
#define LED_PIN    13

#define DEBOUNCE_COUNTER_START 150

bool defaultOn = true;

/* Network */
ESP8266WiFiMulti wifiMulti;
WiFiClient wclient;
/**********/

/* MQTT */
String MQTT_USERNAME = "sonof_1";
String MQTT_TOPIC = "sonoff/1/";
IPAddress MQTTserver(158, 255, 212, 248);
PubSubClient client(MQTTserver, 1883, wclient);
/***********/

bool relayOn = false;


void switchRelayOn()
{
    digitalWrite(RELAY_PIN, HIGH);
    digitalWrite(LED_PIN, LOW);
    relayOn = true;
    client.publish((MQTT_TOPIC + "relay").c_str(), relayOn ? "ON" : "OFF", true);
}

void switchRelayOff()
{
    digitalWrite(RELAY_PIN, LOW);
    digitalWrite(LED_PIN, HIGH);
    relayOn = false;
    client.publish((MQTT_TOPIC + "relay").c_str(), relayOn ? "ON" : "OFF", true);
}

void toggleRelay()
{
    if (relayOn)
    {
        switchRelayOff();
    }
    else
    {
        switchRelayOn();
    }
}

void setup()
{
    delay(10);

    pinMode(RELAY_PIN, OUTPUT);
    pinMode(BUTTON_PIN, INPUT_PULLUP);
    pinMode(LED_PIN, OUTPUT);

    // defaults to off
    if (defaultOn) {
        switchRelayOn();
    } else {
        switchRelayOff();
    }
  
    Serial.begin(115200);
    delay(1000);
    Serial.println("in setup");

    for (int i=0; i<NUM_WIFI_CREDENTIALS; i++) {
        wifiMulti.addAP(WIFI_CREDENTIALS[i][0], WIFI_CREDENTIALS[i][1]);
    }
    Serial.println("added wifi credentials");

    if(wifiMulti.run() == WL_CONNECTED) {
        Serial.println("Wifi connected.");
    } else {
        Serial.println("Wifi not connected!");
    }
}


/* inefficient but i see no better way atm */
inline String byteArrayToString(byte* array, unsigned int length) {
    char chars[length+1];
    for (int i=0; i<length; i++) {
        chars[i] =  (char) array[i];
    }
    chars[length] = '\0';
    return chars;
}


void mqtt_callback(char* topic, byte* payload, unsigned int length) {
  
    String msg = byteArrayToString(payload, length);

    if (msg == "ON") {
        switchRelayOn();
    } else if (msg == "OFF") {
        switchRelayOff();
    } else if (msg == "TOGGL") {
        toggleRelay();
    }

}


void loop() {

    /**** button handling and debouncing ****/
    static int lastButtonState = LOW;
    static unsigned long buttonPressTime = 0;
    static bool inDebounce = false;

    if (digitalRead(BUTTON_PIN) != lastButtonState) { //if pressed or unpressed

        if (inDebounce) {
            unsigned long cmillis = millis();
            if (buttonPressTime + 50 < cmillis) {
                inDebounce = false;
            }
            if (cmillis < buttonPressTime) { // on integer overflow
                inDebounce = false;
            }
        } else {
            inDebounce = true;
            buttonPressTime = millis();
            int newButtonState = lastButtonState == HIGH ? LOW : HIGH;
            if (newButtonState == LOW) {
                Serial.println("button pressed");
                toggleRelay();
                client.publish((MQTT_TOPIC + "button").c_str(), "PRESS", true);
            }
            lastButtonState = newButtonState;
        }
    }

    /**** WIFI and MQTT reconnecting *****/
    static bool wifiConnected = false;
    /* reconnect wifi */
    if(wifiMulti.run() == WL_CONNECTED) {
        if (!wifiConnected) {
            Serial.println("Wifi connected.");
            wifiConnected = true;
        }
    } else {
        if (wifiConnected) {
            Serial.println("Wifi not connected!");
            wifiConnected = false;
        }
        return;
    }

    /* MQTT */
    if (client.connected()) {
        client.loop();
    } else {
        if (client.connect(MQTT_USERNAME.c_str(), (MQTT_TOPIC + "online").c_str(), 0, true, "false")) {
            client.publish((MQTT_TOPIC + "online").c_str(), relayOn ? "on" : "off", true);
            // clear the CMD topic on boot in case a cmd was retained
            client.publish((MQTT_TOPIC + "cmd").c_str(), "NOCMD", true);
            client.publish((MQTT_TOPIC + "relay").c_str(), relayOn ? "ON" : "OFF", true);
            Serial.println("MQTT connected");
            client.setCallback(mqtt_callback);
            client.subscribe((MQTT_TOPIC + "cmd").c_str());
        }
    }

}
