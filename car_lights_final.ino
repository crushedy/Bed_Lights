#include <ESP8266WiFi.h>
#include <WebSocketsServer.h>
#include <Hash.h>
#include <ESP8266WebServer.h>
#include <ESP8266mDNS.h>
#include <NeoPixelAnimator.h>
#include <NeoPixelBrightnessBus.h>
#include <NeoPixelBus.h>
#include <Ticker.h>

#define USE_SERIAL Serial
//state machine states
#define state_off       0
#define state_constant  1
#define state_police    2
#define state_ambulance 3
#define state_fire      4
#define state_mode1     5

//colors definition
#define COLOR_RED RgbColor(255, 0, 0)
#define COLOR_GREEN RgbColor(0.255.0)
#define COLOR_BLUE  RgbColor(0,0,255)
#define COLOR_WHITE RgbColor(255, 255, 255)
#define COLOR_DARK_RED RgbColor(139, 0, 0)
#define COLOR_ORANGE RgbColor(255, 165, 0)
#define COLOR_PINK RgbColor(255, 192, 203)
#define COLOR_SICLAM RgbColor(249, 44, 161)
#define COLOR_LILA RgbColor(200, 162, 200)
#define COLOR_BLACK RgbColor(0, 0, 0)
#define COLOR_YELLOW RgbColor(188, 171, 21)

//4 leds
#define LED_COUNT 4
//thick period (how fast the leds are moving)
#define PERIOD 0.1
NeoPixelBrightnessBus<NeoGrbFeature, NeoEsp8266Uart800KbpsMethod> strip(LED_COUNT, 5);
const uint8_t c_MinBrightness = 8; 
const uint8_t c_MaxBrightness = 255;

bool sound_OK = 1;
bool tickOccured;
bool brightness_direction = true;
Ticker flipper;
int counter = 0;
RgbColor LedColor[4];
RgbColor Constant_Color;
int brightness=255;

static const char ssid[] = "IOOOO2";
static const char password[] = "mamaare23MERE";
MDNSResponder mdns;

int state = state_off;

static void writeLED(bool);

ESP8266WebServer server(80);
WebSocketsServer webSocket = WebSocketsServer(81);

static const char PROGMEM INDEX_HTML[] = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
<meta name = "viewport" content = "width = device-width, initial-scale = 1.0, maximum-scale = 1.0, user-scalable=0">
<title>Light Control</title>
<style>
"body { background-color: #808080; font-family: Arial, Helvetica, Sans-Serif; Color: #000000; }"
</style>
<script>
var websock;
function start() {
  websock = new WebSocket('ws://' + window.location.hostname + ':81/');
  websock.onopen = function(evt) { console.log('websock open'); };
  websock.onclose = function(evt) { console.log('websock close'); };
  websock.onerror = function(evt) { console.log(evt); };
  websock.onmessage = function(evt) {
    console.log(evt);
    var e = document.getElementById('ledstatus');
    if (evt.data === 'ledon') {
      e.style.color = 'red';
    }
    else if (evt.data === 'ledoff') {
      e.style.color = 'black';
    }
    else {
      console.log('unknown event');
    }
  };
}
function buttonclick(e) {
if(e.id!="brightness")
      websock.send(e.id);
  else
    websock.send(e.id + " " + document.getElementById(e.id).value);
}

function fnAllowDigitsOnly(key)
{      
    var keycode=(key.keywhich)?key.which:key.keyCode;
    if(keycode<48||keycode>57 || keycode !=8)
    {
        return false;
    }
}

</script>
</head>
<body onload="javascript:start();">
<h1>Led Color</h1>
<div id="ledstatus"><b>LED</b></div>
<button id="ledon"  type="button" onclick="buttonclick(this);">LED On</button> 
<button id="ledoff" type="button" onclick="buttonclick(this);">LED Off</button><br><br>
<button id="off"  type="button" onclick="buttonclick(this);">OFF</button> 
<button id="constant"  type="button" onclick="buttonclick(this);">Constant</button> 
<button id="ambulance" type="button" onclick="buttonclick(this);">Ambulance</button>
<button id="police"  type="button" onclick="buttonclick(this);">Police</button> 
<button id="fire" type="button" onclick="buttonclick(this);">Fire Truck</button>
<button id="mode1" type="button" onclick="buttonclick(this);">Mode 1</button>
<p>Brightness: <input id="brightness" type="text" onkedydown="fnAllowDigitsOnly(this);"/><br></p><br>
<button id="brightness" type="button" onclick="buttonclick(this);"> Brightness </button><br><br>
<button id="soundon" type="button" onclick="buttonclick(this);">  Sound on </button>  
<button id="soundoff" type="button" onclick="buttonclick(this);"> Sound off </button>  

</body>
</html>
)rawliteral";

// GPIO#0 is for Adafruit ESP8266 HUZZAH board. Your board LED might be on 13.
const int LEDPIN = 16;
const int LED_SOUND = 12;
// Current LED status
bool LEDStatus;


// Commands sent through Web Socket
const char LEDON[] = "ledon";
const char LEDOFF[] = "ledoff";
const char OFF[] = "off";
const char CONSTANT[] = "constant";
const char POLICE[] = "police";
const char AMBULANCE[] = "ambulance";
const char FIRE[] = "fire";
const char MODE1[] = "mode1";
const char BRIGHTNESS[] = "brightness";
const char SOUNDON[] = "soundon";
const char SOUNDOFF[] = "soundoff";


String payload1;

void webSocketEvent(uint8_t num, WStype_t type, uint8_t * payload, size_t length)
{
  int red_value, green_value, blue_value;
  USE_SERIAL.printf("webSocketEvent(%d, %d, ...)\r\n", num, type);
  switch (type) {
  case WStype_DISCONNECTED:
    USE_SERIAL.printf("[%u] Disconnected!\r\n", num);
    break;
  case WStype_CONNECTED:
  {
               IPAddress ip = webSocket.remoteIP(num);
               USE_SERIAL.printf("[%u] Connected from %d.%d.%d.%d url: %s\r\n", num, ip[0], ip[1], ip[2], ip[3], payload);
               // Send the current LED status
               if (LEDStatus) {
                 webSocket.sendTXT(num, LEDON, strlen(LEDON));
               }
               else {
                 webSocket.sendTXT(num, LEDOFF, strlen(LEDOFF));
               }
  }
    break;
  case WStype_TEXT:
    USE_SERIAL.printf("[%u] get Text: %s\r\n", num, payload);

    if (strcmp(LEDON, (const char *)payload) == 0) {
      writeLED(true);
    }
    else if (strcmp(LEDOFF, (const char *)payload) == 0) {
      writeLED(false);
    }
    else if (strcmp(OFF, (const char *)payload) == 0) {
      state = state_off;
      tickOccured = true;
      USE_SERIAL.println("STATE: Off");
      flipper.detach();
      digitalWrite(LED_SOUND, 0);
    }
    else if (strncmp(CONSTANT, (const char *)payload, 8) == 0) {
      state = state_constant;
      tickOccured = true;
      USE_SERIAL.println("STATE: Constant");
      if(length!=8) 
       sscanf((char *)payload, "constant %d %d %d", &red_value, &green_value, &blue_value); 
      else 
      {
        red_value = 255;
        green_value = 255;
        blue_value = 255;
      }
      Constant_Color = RgbColor(red_value, green_value, blue_value);
      flipper.detach();
      
    }
    else if (strcmp(AMBULANCE, (const char *)payload) == 0) {
      state = state_ambulance;
      flipper.attach(PERIOD*3, tick);
      counter=0;
      USE_SERIAL.println("STATE: Ambulance");
    }
    else if (strcmp(POLICE, (const char *)payload) == 0) {
      state = state_police;
      flipper.attach(PERIOD*4, tick);
      counter=0;
      USE_SERIAL.println("STATE: Police");
    }
    else if (strcmp(FIRE, (const char *)payload) == 0) {
      state = state_fire;
      flipper.attach(PERIOD*2, tick);
      counter=0;
      USE_SERIAL.println("STATE: Fire");
    }
    else if (strcmp(MODE1, (const char *)payload) == 0) {
      state = state_mode1;
      flipper.attach(PERIOD/10, tick);
      counter=0;
      USE_SERIAL.println("STATE: Mode 1");
    }
    else if (strncmp(BRIGHTNESS, (const char *)payload,10) == 0)  {
      payload1 = (char*)payload;
      USE_SERIAL.println(payload1);
      brightness = (payload1.substring(11)).toInt();
      USE_SERIAL.println(brightness);
      update_strip();
    }
    else if (strcmp(SOUNDON, (const char *)payload) == 0)  {
      sound_OK = true;
      USE_SERIAL.println("sound on");
    }
    else if (strcmp(SOUNDOFF, (const char *)payload) == 0)  {
      sound_OK = false;
      USE_SERIAL.println("sound off");
    }
    else {
      USE_SERIAL.println("Unknown command");
    }

    
    // send data to all connected clients
    webSocket.broadcastTXT(payload, length);
    break;
  case WStype_BIN:
    USE_SERIAL.printf("[%u] get binary length: %u\r\n", num, length);
    hexdump(payload, length);

    // echo data back to browser
    webSocket.sendBIN(num, payload, length);
    break;
  default:
    USE_SERIAL.printf("Invalid WStype [%d]\r\n", type);
    break;
  }
}

void handleRoot()
{
  server.send_P(200, "text/html", INDEX_HTML);
}

void handleNotFound()
{
  String message = "File Not Found\n\n";
  message += "URI: ";
  message += server.uri();
  message += "\nMethod: ";
  message += (server.method() == HTTP_GET) ? "GET" : "POST";
  message += "\nArguments: ";
  message += server.args();
  message += "\n";
  for (uint8_t i = 0; i<server.args(); i++){
    message += " " + server.argName(i) + ": " + server.arg(i) + "\n";
  }
  server.send(404, "text/plain", message);
}

static void writeLED(bool LEDon)
{
  LEDStatus = LEDon;
  // Note inverted logic for Adafruit HUZZAH board
  if (LEDon) {
    digitalWrite(LEDPIN, 0);
  }
  else {
    digitalWrite(LEDPIN, 1);
  }
}
//ticks once at 100ms
void tick()
{
      tickOccured = true;
      counter++;
      if(sound_OK) 
        if(digitalRead(LED_SOUND)==0) digitalWrite(LED_SOUND, 1); else digitalWrite(LED_SOUND, 0);
}
//updates the leds and displays them
void update_strip()
{
  strip.SetBrightness(brightness);
  for(int i=0; i<LED_COUNT; i++)
    strip.SetPixelColor(i, LedColor[i]);
  strip.Show(); // Initialize all pixels to 'off'
  if(sound_OK) digitalWrite(LED_SOUND, 1);
}


void setup()
{
  pinMode(LEDPIN, OUTPUT);
  pinMode(LED_SOUND, OUTPUT);
  digitalWrite(LED_SOUND, 0);
  writeLED(false);
  counter=0;
  state=state_off;
  strip.Begin();
  update_strip();
  tickOccured = false;
  
  USE_SERIAL.begin(9600);

  //Serial.setDebugOutput(true);

  USE_SERIAL.println();
  USE_SERIAL.println();
  USE_SERIAL.println();

  for (uint8_t t = 4; t > 0; t--) {
    USE_SERIAL.printf("[SETUP] BOOT WAIT %d...\r\n", t);
    USE_SERIAL.flush();
    delay(1000);
  }
  // Connect to WiFi network
  Serial.println();
  Serial.println();
  Serial.print("Connecting to ");
  Serial.println(ssid);
  
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("");
  Serial.println("WiFi connected");



  if (mdns.begin("espWebSock", WiFi.localIP())) {
    USE_SERIAL.println("MDNS responder started");
    mdns.addService("http", "tcp", 80);
    mdns.addService("ws", "tcp", 81);
  }
  else {
    USE_SERIAL.println("MDNS.begin failed");
  }
  USE_SERIAL.print("Connect to http://espWebSock.local or http://");
  USE_SERIAL.println(WiFi.localIP());

  server.on("/", handleRoot);
  server.onNotFound(handleNotFound);

  server.begin();

  webSocket.begin();
  webSocket.onEvent(webSocketEvent);
}

void loop()
{
  int i;
  webSocket.loop();
  server.handleClient();
  if(tickOccured == true)
  {
    tickOccured = false;
    if(state==state_ambulance)
    {
      if(counter%2)
      {
      LedColor[0]=COLOR_YELLOW;
      LedColor[1]=COLOR_YELLOW;
      LedColor[2]=COLOR_YELLOW;
      LedColor[3]=COLOR_YELLOW;
      }
      else
      {
      LedColor[0]=COLOR_BLACK;
      LedColor[1]=COLOR_BLACK;
      LedColor[2]=COLOR_BLACK;
      LedColor[3]=COLOR_BLACK;
      }
      update_strip();

    }
    else if (state == state_police)
    {
      if(counter%2)
      {
      LedColor[0]=COLOR_RED;
      LedColor[1]=COLOR_BLUE;
      LedColor[2]=COLOR_BLUE;
      LedColor[3]=COLOR_RED;
      }
      else
      {
      LedColor[0]=COLOR_BLUE;
      LedColor[1]=COLOR_RED;
      LedColor[2]=COLOR_RED;
      LedColor[3]=COLOR_BLUE;
      }
      update_strip();
    }
    else if (state == state_fire)
    {
      if(counter==1)
      {
      LedColor[0]=COLOR_WHITE;
      LedColor[1]=COLOR_BLACK;
      LedColor[2]=COLOR_BLACK;
      LedColor[3]=COLOR_BLACK;
      update_strip();
      USE_SERIAL.println("counter = 1");
      counter++;
      } 
      else
      {
      strip.RotateRight(1);
      strip.Show();
      
      }

    }
    else if (state == state_mode1)
    {
      
      LedColor[0]=COLOR_WHITE;
      LedColor[1]=COLOR_WHITE;
      LedColor[2]=COLOR_WHITE;
      LedColor[3]=COLOR_WHITE;
      for(int i=0; i<LED_COUNT; i++)
      strip.SetPixelColor(i, LedColor[i]);    
      if(brightness_direction==true) 
        strip.SetBrightness(counter*5);
      else
        strip.SetBrightness(250-counter*5);
      if(counter>50) 
      {
        counter=0;
        if(brightness_direction == true) brightness_direction = false; else brightness_direction = true;
      }
      if(sound_OK) digitalWrite(LED_SOUND, 1);
      strip.Show();
    }
    else if (state == state_constant)
    {
      LedColor[0]=Constant_Color;
      LedColor[1]=Constant_Color;
      LedColor[2]=Constant_Color;
      LedColor[3]=Constant_Color;
      update_strip();
      
    }
    else if (state == state_off)
    {
      for(i=0; i<LED_COUNT;i++)
        LedColor[i]=COLOR_BLACK;
       update_strip();
    }
    
    
    
  }  
}

