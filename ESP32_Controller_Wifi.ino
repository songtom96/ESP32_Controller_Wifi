#include <DHT20.h>
#include <WiFi.h>
// #include <time.h>
// #include "test_library.h"

// Sensor & Actuators
#define RELAY_PIN_12 25
#define RELAY_PIN_24 27
// I2C pins
#define SDA1 0
#define SCL1 4

DHT20 DHT(&Wire);  //  2nd I2C interface

//Timer Scale
#define sFactor 1000000
#define mFactor sFactor * 60
#define hFactor 60 * mFactor

bool isVenting = false;
bool isIdle = false;
bool eStop = false;

//Prototypes
static void
vent_timer_ISR(void* arg);
static void idle_timer_ISR(void* arg);
bool updateDHTData();
void controlRelay(bool fSwitch = 0, bool hSwitch = 0, bool STATE = 0);

enum State { idling,
             venting };
State state = venting;

unsigned long senseTime;

// Wifi connection 1
const char* ssid = "raylynoffice";
const char* password = "rwf2020118";
// Wifi connection 2
const char* ssid2 = "Raylyn20";
const char* password2 = "war2trail20";
//Acess Point
const char* id = "GreenHouse";
const char* pass = "buyyourownwifinoob";

const long timeoutTime = 2000;
IPAddress local_IP(192, 168, 0, 198);
IPAddress NMask(255, 255, 255, 0);
bool wifiConnected = false;

//define authenication
const char* base64Encoding = "c29uZ3RvbTk2Ojk2MDEwNQ==";  // base64encoding user:pass // songtom96:960105

// Current time
unsigned long currentTime = millis();
// Previous time
unsigned long previousTime = 0;

// Timer
const esp_timer_create_args_t vent_timer_args = {
  .callback = &vent_timer_ISR,
  /* name is optional, but may help identify the timer when debugging */
  .name = "vent"
};
const esp_timer_create_args_t idle_timer_args = {
  .callback = &idle_timer_ISR,
  /* name is optional, but may help identify the timer when debugging */
  .name = "idle"
};
esp_timer_handle_t vent_timer;
esp_timer_handle_t idle_timer;

// Auxiliar variables to store the current output state
int setpointTemperature = 22;
int setpointHumidity = 80;
int ventTime = 1;      // Default Venting duration in minutes
int intervalTime = 1;  // Default Venting interval in hours
int intervalCounter = intervalTime * 60;
String output26State = "off";
String output27State = "off";

String header;

float temp = 0.0;
float humidity = 0.0;

WiFiServer server(80);

void setup() {
  Serial.begin(115200);
  pinMode(RELAY_PIN_12, OUTPUT);
  pinMode(RELAY_PIN_24, OUTPUT);
  Wire.begin(SDA1, SCL1);  // Start I2C on defined pins
  while (!Serial) {
  }

  // Timer Setup

  // WiFi connection
#define WIFI_ENABLE
#ifdef WIFI_ENABLE
  WiFi.config(local_IP);
  Serial.print("Connecting to ");
  Serial.println(ssid);
  WiFi.begin(ssid, password);
  unsigned long wifiTimeout = millis();
  while (WiFi.status() != WL_CONNECTED && ((millis() - wifiTimeout) < 5000)) {
    delay(500);
    Serial.print(".");
  }
  // if (WiFi.status() != WL_CONNECTED) {
  //   Serial.println("");
  //   Serial.print("Connecting to ");
  //   Serial.println(ssid2);
  //   WiFi.begin(ssid2, password2);
  //   wifiTimeout = millis();
  //   while (WiFi.status() != WL_CONNECTED && ((millis() - wifiTimeout) < 5000)) {
  //     delay(500);
  //     Serial.print(".");
  //   }
  // }
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("");
    Serial.println("WiFi connected.");
    Serial.println("IP address: ");
    Serial.println(WiFi.localIP());
    server.begin();
    wifiConnected = true;
  } else {
    WiFi.softAPConfig(local_IP, local_IP, NMask);
    WiFi.mode(WIFI_MODE_AP);
    WiFi.softAP(id, pass);
    IPAddress IP = WiFi.softAPIP();
    Serial.println("");
    Serial.print("AP IP address: ");
    Serial.println(IP);
    server.begin();
    wifiConnected = true;
  }
#endif

  delay(1000);
  senseTime = millis();
  ESP_ERROR_CHECK(esp_timer_create(&vent_timer_args, &vent_timer));
  ESP_ERROR_CHECK(esp_timer_create(&idle_timer_args, &idle_timer));
  // ESP_ERROR_CHECK(esp_timer_start_once(vent_timer, 5 * mFactor));
  // ESP_ERROR_CHECK(esp_timer_start_once(idle_timer, 5 * mFactor));
}

void loop() {
  // Sensor update
  if ((millis() - senseTime) > 2000) {
    updateDHTData();
    // Serial.print("Temp: ");
    // Serial.println(temp);
    // Serial.print("Humidity: ");
    // Serial.println(humidity);
    senseTime = millis();
  }
// WiFi protocol
#ifdef WIFI_ENABLE
  if (wifiConnected) {
    wifiProtocol();
  }
#endif
  if (!eStop) {
    // Control protocol
    if (humidity < setpointHumidity - 5) {
      controlRelay(0, 1, 1);
    } else if (humidity >= setpointHumidity) {
      controlRelay(0, 1, 0);
    }

    if (!isVenting) {
      if (temp > setpointTemperature + 3) {
        controlRelay(1, 0, 1);
      } else if (temp <= setpointTemperature) {
        controlRelay(1, 0, 0);
      }
    }
    // Venting state machine/protocol
    switch (state) {
      case venting:
        if (!isVenting) {
          isVenting = true;
          controlRelay(1, 0, 1);
          ESP_ERROR_CHECK(esp_timer_start_once(vent_timer, ventTime * mFactor));
          Serial.println("Venting...");
        }
        break;
      case idling:
        if (!isIdle) {
          isIdle = true;
          intervalCounter = intervalTime; //in minutes currently //intervalTime * 60
          controlRelay(1, 0, 0);
          ESP_ERROR_CHECK(esp_timer_start_once(idle_timer, 1 * mFactor));
          Serial.println("Idling...");
        }
        break;
    }
  } else {
    relayShutdown();
  }
}

/*
* Update global humidty and temperature values from DHT11
* If values read are invalid then return false and does not update values
* RETURN: true on success and false on fail
*/
bool updateDHTData() {
  int status = DHT.read();
  // switch (status) {
  //   case DHT20_OK:
  //     Serial.print("OK,\t");
  //     break;
  //   case DHT20_ERROR_CHECKSUM:
  //     Serial.print("Checksum error,\t");
  //     break;
  //   case DHT20_ERROR_CONNECT:
  //     Serial.print("Connect error,\t");
  //     break;
  //   case DHT20_MISSING_BYTES:
  //     Serial.print("Missing bytes,\t");
  //     break;
  //   case DHT20_ERROR_BYTES_ALL_ZERO:
  //     Serial.print("All bytes read zero");
  //     break;
  //   case DHT20_ERROR_READ_TIMEOUT:
  //     Serial.print("Read time out");
  //     break;
  //   case DHT20_ERROR_LASTREAD:
  //     Serial.print("Error read too fast");
  //     break;
  //   default:
  //     Serial.print("Unknown error,\t");
  //     break;
  // }
  Serial.println("");
  if (status == DHT20_OK) {
    float t = DHT.getTemperature();
    float h = DHT.getHumidity();
    if (!(isnan(t) | isnan(h)) && (t < 99 && h > 5)) {
      temp = t;
      humidity = h;
      return true;
    }
  }
  return false;
}

void wifiProtocol() {
  WiFiClient client = server.available();  // Listen for incoming clients

  if (client) {  // If a new client connects,
    currentTime = millis();
    previousTime = currentTime;
    Serial.println("New Client.");                                             // print a message out in the serial port
    String currentLine = "";                                                   // make a String to hold incoming data from the client
    while (client.connected() && currentTime - previousTime <= timeoutTime) {  // loop while the client's connected
      currentTime = millis();
      if (client.available()) {  // if there's bytes to read from the client,
        char c = client.read();  // read a byte, then
        Serial.write(c);         // print it out the serial monitor
        header += c;
        if (c == '\n') {  // if the byte is a newline character
          // if the current line is blank, you got two newline characters in a row.
          // that's the end of the client HTTP request, so send a response:
          if (currentLine.length() == 0) {
            // HTTP headers always start with a response code (e.g. HTTP/1.1 200 OK)
            // and a content-type so the client knows what's coming, then a blank line:
            // Finding the right Credential string. If correct, load webpage
            if (header.indexOf(base64Encoding) >= 0) {
              client.println("HTTP/1.1 200 OK");
              client.println("Content-type:text/html");
              client.println("Connection: close");
              client.println();

              // turns the GPIOs on and off
              if (header.indexOf("GET /emergencystop") >= 0) {
                relayShutdown();
                eStop = true;
              } else if (header.indexOf("GET /resumestop") >= 0) {
                eStop = false;
              } else if (header.indexOf("GET /temp/down") >= 0) {
                if (setpointTemperature > 0) {
                  Serial.println("Temp down");
                  setpointTemperature--;
                } else {
                  Serial.println("Min temp reached...");
                }
              } else if (header.indexOf("GET /temp/up") >= 0) {
                if (setpointTemperature < 60) {
                  Serial.println("Temp up");
                  setpointTemperature++;
                } else {
                  Serial.println("Max temp reached...");
                }
              } else if (header.indexOf("GET /humd/down") >= 0) {
                if (setpointHumidity > 0) {
                  Serial.println("Humid down");
                  setpointHumidity--;
                } else {
                  Serial.println("Min humidity reached...");
                }
              } else if (header.indexOf("GET /humd/up") >= 0) {
                if (setpointHumidity < 100) {
                  Serial.println("Humid up");
                  setpointHumidity++;
                } else {
                  Serial.println("Max humidity reached...");
                }
              } else if (header.indexOf("GET /intervalTime/down") >= 0) {
                if (intervalTime > 1) {
                  Serial.println("Interval Time down");
                  intervalTime--;
                } else {
                  Serial.println("Min Interval Time reached...");
                }
              } else if (header.indexOf("GET /intervalTime/up") >= 0) {
                if (intervalTime < 60) {
                  Serial.println("Interval Time up");
                  intervalTime++;
                } else {
                  Serial.println("Max Interval Time reached...");
                }
              } else if (header.indexOf("GET /ventTime/down") >= 0) {
                if (ventTime > 1) {
                  Serial.println("Vent Time down");
                  ventTime--;
                } else {
                  Serial.println("Min Vent Time reached...");
                }
              } else if (header.indexOf("GET /ventTime/up") >= 0) {
                if (ventTime < 60) {
                  Serial.println("Vent Time up");
                  ventTime++;
                } else {
                  Serial.println("Max Vent Time reached...");
                }
              }

              // Display the HTML web page
              client.println("<!DOCTYPE html><html>");
              client.println("<head><meta http-equiv=\"refresh\" content=\"4; url =/\"><meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">");
              client.println("<link rel=\"icon\" href=\"data:,\">");
              // CSS to style the on/off buttons
              // Feel free to change the background-color and font-size attributes to fit your preferences
              client.println("<style>html { font-family: Georgia, serif; display: inline-block; margin: 0px auto; text-align: center;}");
              client.println(".button { background-color: #04AA6D; border: none; color: white; padding: 16px 25px; border-radius: 8px; box-shadow: 0 9px #999;");
              client.println("text-decoration: none; font-size: 15px; margin: 8px 4px; cursor: pointer;}");
              client.println(".button:hover {background-color: #3e8e41} .button:active { background - color : #3e8e41; box - shadow : 0 5px #666; transform: translateY(4px); }");
              client.println(".button2 {background-color: #f44336; font-size: 24px;}");
              client.println(".button2:hover {background-color: #C23C32} .button2:active { background - color : #C23C32; box - shadow : 0 5px #666; transform: translateY(4px); }");
              client.println("</style></head>");
              // Web Page Heading
              client.println("<body><h1>GreenHouse 1.0</h1>");

              client.println("<p>Current Temperature: ");
              client.println(temp);
              client.println("&deg;C</p>");
              // If the output26State is off, it displays the ON button
              client.println("<p>Setpoint Temperature: ");
              client.println(setpointTemperature);
              client.println("&deg;C");
              client.println("  <a href=\"/temp/down\"><button class=\"button\">-</button></a><a href=\"/temp/up\"><button class=\"button\">+</button></a></p>");
              // client.println("<form action=\"/userTempSubmit\">input1: <input autocomplete=\"off\" type=\"text\" name=\"input1\"><input type=\"submit\" value=\"Submit\"></form>");
              client.println("<p>Current Humidity: ");
              client.println(humidity);
              client.println("%</p>");
              // If the output27State is off, it displays the ON button
              client.println("<p>Setpoint Humidity: ");
              client.println(setpointHumidity);
              client.println("%");
              client.println("  <a href=\"/humd/down\"><button class=\"button\">-</button></a><a href=\"/humd/up\"><button class=\"button\">+</button></a></p>");
              client.println("<p><br></p><p>Vent Intervals: ");
              client.println(intervalTime);
              client.println(" Minutes <a href=\"/intervalTime/down\"><button class=\"button\">-</button></a><a href=\"/intervalTime/up\"><button class=\"button\">+</button></a>");
              client.println("<br>Vent Duration: ");
              client.println(ventTime);
              client.println(" Minutes <a href=\"/ventTime/down\"><button class=\"button\">-</button></a><a href=\"/ventTime/up\"><button class=\"button\">+</button></a>");
              if (!eStop) {
                client.println(" <br><br><br><a href=\"/emergencystop\"><button class=\"button button2\"><b><i>EMERGENCY STOP</i></b></button></a>");
              } else {
                client.println(" <br><br><br><a href=\"/resumestop\"><button class=\"button button2\"><b><i>RESUME</i></b></button></a>");
              }
              client.println("</p>");
              if (isVenting) {
                client.println("<p>FAN Status: VENTING...<br></p>");
              } else {
                client.println("<p>FAN Status: IDLING....<br></p>");
                client.println("<p>Interval Time Remaining: Status: ");
                client.println(intervalCounter);
                client.println(" Mins </p>");
              }

              client.println("</body></html>");

              // The HTTP response ends with another blank line
              client.println();
              // Break out of the while loop
              break;
            } else {
              client.println("HTTP/1.1 401 Unauthorised");
              client.println("WWW-Authenticate: Basic realm=\"Secure\"");
              client.println("Content-Type: text/html");
              client.println("");
              client.println("<html>Authentication failed</html>");
            }


          } else {  // if you got a newline, then clear currentLine
            currentLine = "";
          }
        } else if (c != '\r') {  // if you got anything else but a carriage return character,
          currentLine += c;      // add it to the end of the currentLine
        }
      }
    }
    // Clear the header variable
    header = "";
    // Close the connection
    client.stop();
    Serial.println("Client disconnected.");
    Serial.println("");
  }
}

/*
* Control function for 12V and 24V relay. fSwitch: 12V, hSwitch: 24V
* Set which switch to using parameter and define switch STATE: 0 = OPEN, 1 = CLOSED for Normaly Open HIGH level trigger
*/
void controlRelay(bool fSwitch, bool hSwitch, bool STATE) {
  if (hSwitch) {
    digitalWrite(RELAY_PIN_24, STATE);
  }
  if (fSwitch) {
    digitalWrite(RELAY_PIN_12, STATE);
  }
}

void relayShutdown() {
  controlRelay(1, 1, 0);
}

// void IRAM_ATTR timer0_ISR() {
// }

static void vent_timer_ISR(void* arg) {
  isVenting = false;
  state = idling;
}
static void idle_timer_ISR(void* arg) {
  intervalCounter--;
  if (intervalCounter <= 0) {
    isIdle = false;
    state = venting;
  } else {
    ESP_ERROR_CHECK(esp_timer_start_once(idle_timer, 1 * mFactor));
  }
}

// void printLocalTime(){
//   struct tm timeinfo;
//   if(!getLocalTime(&timeinfo)){
//     Serial.println("Failed to obtain time");
//     return;
//   }
//   Serial.println(&timeinfo, "%A, %B %d %Y %H:%M:%S");
// }