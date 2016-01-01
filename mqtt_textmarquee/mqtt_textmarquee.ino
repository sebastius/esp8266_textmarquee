/*
It's finally here!
*/

#include <EEPROM.h>

#include <ESP8266WiFi.h>
#include <WiFiUdp.h>
#include <WiFiClient.h>

#include <Time.h>        //http://www.arduino.cc/playground/Code/Time
#include <Timezone.h>    //https://github.com/JChristensen/Timezone < i modified timezone.cpp to remove the EEPROM functions

#include <PubSubClient.h>

#include <Sixteen.h>

Sixteen display = Sixteen();

static uint8_t CAM = 0; //Cam choice, 0 is total of all cams.
int gluurders[10];
uint16_t old_gluur;
bool state;
bool klok_ok = false;


// WiFi settings
char ssid[] = "revspace-pub-2.4ghz";  //  your network SSID (name)
char pass[] = "";       // your network password
const char* mqtt_server = "test.mosquitto.org";

// Timezone Rules for Europe
// European Daylight time begins on the last sunday of March
// European Standard time begins on the last sunday of October
// Officially both around 2 AM, but let's keep things simple.

TimeChangeRule CEST = {"CEST", Last, Sun, Mar, 1, +120};    //Daylight time = UTC +2 hours
TimeChangeRule CET = {"CET", Last, Sun, Oct, 1, +60};     //Standard time = UTC +1 hours
Timezone myTZ(CEST, CET);
TimeChangeRule *tcr;        //pointer to the time change rule, use to get TZ abbrev
time_t utc, local;

// NTP Server settings
unsigned int localPort = 2390;      // local port to listen for UDP packets
IPAddress timeServerIP; // time.nist.gov NTP server address
const char* ntpServerName = "pool.ntp.org";
const int NTP_PACKET_SIZE = 48; // NTP time stamp is in the first 48 bytes of the message
byte packetBuffer[ NTP_PACKET_SIZE]; //buffer to hold incoming and outgoing packets

// A UDP instance to let us send and receive packets over UDP
WiFiUDP udp;

WiFiClient espClient;
PubSubClient client(mqtt_server, 1883, onMqttMessage, espClient);


long lastMsg = 0;
char msg[50];
int value = 0;
long lastReconnectAttempt = 0;
char suckermode[7];
char suckerpower[5];

void setup()
{
  display.addScreen(5, 4);
  display.addScreen(5, 2);
  display.addScreen(5, 14);
  display.addScreen(5, 12);
  display.addScreen(5, 13);

  Serial.begin(115200);
  Serial.println();


  // We start by connecting to a WiFi network
  Serial.print("Connecting to ");
  display.scroll("Connecting", 10);
  Serial.println(ssid);
  WiFi.begin(ssid, pass);

  while (WiFi.status() != WL_CONNECTED) {
    delay(50);
    Serial.print(".");

  }
  Serial.println("");

  Serial.println("WiFi connected");
  display.scroll(ssid, 100);

  Serial.println("IP address: ");
  Serial.println(WiFi.localIP());




  udp.begin(localPort);
  ntpsync();

  state = HIGH; // so we get display before receiving MQTT Space State message

}

void loop()
{
  if (!client.connected()) {
    Serial.println(".");
    long verstreken = millis();
    if (verstreken - lastReconnectAttempt > 5000) {
      lastReconnectAttempt = verstreken;
      // Attempt to reconnect
      if (reconnect()) {
        lastReconnectAttempt = 0;
      }
    }
  } else {
    // Client connected
    client.loop();
  }


  if (state == HIGH) {
  
    printTime(now());
    
  } else {
    // Low-power-mode
    display.scroll("          ", 100);
  }


  // NTP sync every 3 hours.
  if (hour(now()) % 3 == 0 && minute(now()) == 0 && second(now()) == 0) {
    klok_ok = false;
  }

  if (!klok_ok) {
    ntpsync();
  }

  // Need to add a if NTPsync fail fallback option.
}

boolean reconnect() {
  if (client.connect("ESP-krant")) {
    // Once connected, publish an announcement...
    client.publish("revspace/espkrant", "hello world");
    // ... and resubscribe
    client.subscribe("revspace/cams");
    client.loop();
    client.subscribe("revspace/state");
    client.loop();
    client.subscribe("revspace/button/nomz");
    client.loop();
    client.subscribe("revspace/spacesucker/#");
    client.loop();
  }
  return client.connected();
}



boolean ntpsync() {

  //get a random server from the pool
  WiFi.hostByName(ntpServerName, timeServerIP);

  sendNTPpacket(timeServerIP); // send an NTP packet to a time server

  delay(500);

  int cb = udp.parsePacket();
  if (!cb) {
    Serial.println("no packet yet");
    klok_ok = false;
  }
  else {
    Serial.print("packet received, length=");
    Serial.println(cb);
    // We've received a packet, read the data from it
    udp.read(packetBuffer, NTP_PACKET_SIZE); // read the packet into the buffer

    //the timestamp starts at byte 40 of the received packet and is four bytes,
    // or two words, long. First, esxtract the two words:

    unsigned long highWord = word(packetBuffer[40], packetBuffer[41]);
    unsigned long lowWord = word(packetBuffer[42], packetBuffer[43]);
    // combine the four bytes (two words) into a long integer
    // this is NTP time (seconds since Jan 1 1900):
    unsigned long secsSince1900 = highWord << 16 | lowWord;
    Serial.print("Seconds since Jan 1 1900 = " );
    Serial.println(secsSince1900);

    // now convert NTP time into everyday time:
    Serial.print("Unix time = ");
    // Unix time starts on Jan 1 1970. In seconds, that's 2208988800:
    const unsigned long seventyYears = 2208988800UL;
    // subtract seventy years:
    unsigned long epoch = secsSince1900 - seventyYears;
    // print Unix time:
    Serial.println(epoch);

    local = myTZ.toLocal(epoch, &tcr);

    setTime(local);
    klok_ok = true;
  }
}



// send an NTP request to the time server at the given address
unsigned long sendNTPpacket(IPAddress& address)
{
  Serial.println("sending NTP packet...");
  // set all bytes in the buffer to 0
  memset(packetBuffer, 0, NTP_PACKET_SIZE);
  // Initialize values needed to form NTP request
  // (see URL above for details on the packets)
  packetBuffer[0] = 0b11100011;   // LI, Version, Mode
  packetBuffer[1] = 0;     // Stratum, or type of clock
  packetBuffer[2] = 6;     // Polling Interval
  packetBuffer[3] = 0xEC;  // Peer Clock Precision
  // 8 bytes of zero for Root Delay & Root Dispersion
  packetBuffer[12]  = 49;
  packetBuffer[13]  = 0x4E;
  packetBuffer[14]  = 49;
  packetBuffer[15]  = 52;

  // all NTP fields have been given values, now
  // you can send a packet requesting a timestamp:
  udp.beginPacket(address, 123); //NTP requests are to port 123
  udp.write(packetBuffer, NTP_PACKET_SIZE);
  udp.endPacket();
}





void printTime(time_t t)
{

  //Serial.print("Het is nu: " );
  //Serial.print(hour(t));
  //Serial.print(":");
  //Serial.print(minute(t));
  //Serial.print(":");
  //Serial.println(second(t));

  String stringOne = " ";
  if (hour(t) < 10) {
    stringOne += "0";
  }

  stringOne += hour(t);
  stringOne += ":";
  if (minute(t) < 10) {
    stringOne += "0";
  }
  stringOne += minute(t);
  stringOne += ":";
  if (second(t) < 10) {
    stringOne += "0";
  }
  stringOne += second(t);


  char charBuf[10];
  stringOne.toCharArray(charBuf, 10);

  display.scroll(charBuf, 10);
}

void printDate(time_t t)
{

  //Serial.print("Het is nu: " );
  //Serial.print(day(t));
  //Serial.print("-");
  //Serial.print(month(t));
  //Serial.print("-");
  //Serial.println(year(t));

  String stringOne = "";
  if (day(t) < 10) {
    stringOne += "0";
  }

  stringOne += day(t);
  stringOne += "-";
  if (month(t) < 10) {
    stringOne += "0";
  }
  stringOne += month(t);
  stringOne += "-";

  stringOne += year(t);


  char charBuf[11];
  stringOne.toCharArray(charBuf, 11);

  display.scroll(charBuf, 10);
}

void onMqttMessage(char* topic, byte* payload, unsigned int length) {
  uint16_t spaceCnt;
  uint8_t numCnt = 0;
  char num[3] = "";

  char bericht[50] = "";
  for (uint8_t pos = 0; pos < length; pos++) {
    bericht[pos] = payload[pos];
  }

  // handle message arrived

  Serial.print("received topic: ");
  Serial.println(topic);
  Serial.print("length: ");
  Serial.println(length);
  Serial.print(" **** payload: ");

  Serial.println(bericht);
  Serial.println();

  // Lets select a payload handler

  if (strcmp(topic, "revspace/state") == 0) {
    if (strcmp(bericht, "open") == 0) {
      Serial.println("Revspace is open");
      state = HIGH;
    }
    if (strcmp(bericht, "closed") == 0) {
      Serial.println("Revspace is dicht");
      state = LOW;
      display.scroll("[88888888]", 10);
      display.scroll("-[888888]-", 10);
      display.scroll("--[8888]--", 10);
      display.scroll("---[88]---", 10);
      display.scroll("----[]----", 10);
      display.scroll("  --[]--  ", 10);
      display.scroll("    []    ", 10);

    }
  }

  if (strcmp(topic, "revspace/button/nomz") == 0) {
    for (uint8_t tel = 0; tel < 20; tel++) {

      display.scroll("   nomz   ", 50);
      display.scroll("          ", 10);
      display.scroll("nomz  nomz", 50);
      display.scroll("          ", 10);
    }

  }

  if (strcmp(topic, "revspace/button/deurbel") == 0) {
    display.scroll(" deurbel. ", 2000);
  }

  if (strcmp(topic, "revspace/spacesucker/level") == 0) {
    for (uint8_t tel = 0; tel < 5; tel++) {
      suckerpower[tel] = bericht[tel];
    }
  }

  if (strcmp(topic, "revspace/spacesucker/level") == 0) {
    for (uint8_t tel = 0; tel < 7; tel++) {
      suckermode[tel] = bericht[tel];
    }
  }

  if (strcmp(topic, "revspace/cams") == 0) {
    // This part was written by Benadski@Revspace, many thanks!
    // Modified it so the function fills an array with all available cam-stats

    for (uint8_t teller = 0; teller < 10; teller++) {
      spaceCnt = 0;
      numCnt = 0;
      memset(num, 0, sizeof(num));

      for (uint8_t skip = 0; skip < teller; skip++) {
        while (((uint8_t)payload[spaceCnt] != 32) && (spaceCnt < length)) {
          spaceCnt++;
        }
        if (((uint8_t)payload[spaceCnt] == 32) && (spaceCnt < length)) spaceCnt++;
      }

      while (((uint8_t)payload[spaceCnt] != 32) && (spaceCnt <= length) && (numCnt < 3)) {
        num[numCnt] = payload[spaceCnt];
        numCnt++;
        spaceCnt++;
      }
      if (numCnt > 0) {
        gluurders[teller] = atoi(&num[0]);
      }
    }

    if (gluurders[0] > 0) {

      Serial.println("Aantal gluurders");
      for (uint8_t teller = 0; teller < 10; teller++) {
        if (gluurders[teller] > 0) {
          if (teller == 0) {
            Serial.print("Totaal");
          } else {
            Serial.print("Cam ");
            Serial.print(teller);
          }
          Serial.print(": ");
          Serial.println(gluurders[teller]);
        }
      }
    } else {
      Serial.println("geen gluurders :(");
    }
  }
  Serial.println();
}


