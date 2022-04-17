// ESP OLED Clock & IR Sender, controllable by HTTP Request
// Jan 9, 2021
// Quincy Cheng

#include <Time.h>
#include <TimeLib.h>
#include <NTPClient.h>
#include <ESP8266WiFi.h>
#include <WiFiUdp.h>
#include <heltec.h>

// Web Server
#include <ESP8266WebServer.h>
#include <ESP8266mDNS.h>

// IRServer
#include <IRremoteESP8266.h>
#include <IRsend.h>

// Gnd Data n/a 3v3
const uint16_t kIrLed = 13;  // ESP GPIO pin to use. Recommended: 4 (D2).  Tested OK: 13 (D7) at Heltec Wifi Kit 8; D5
IRsend irsend(kIrLed);  // Set the GPIO to be used to sending the message.

// WIFI Settings
const char *ssid = "<Your WIFI SSID Here>";
const char *pass = "<Your WIFI Password Here>";

// PDT
long kTimeZone = 8;  // Default Timezone GMT+8
String kCityName = "Hong Kong";
int gDay, gMonth, gYear, gSeconds, gMinutes, gHours, gHKHours;
int gDayLast, gMonthLast, gYearLast, gSecondsLast, gMinutesLast, gHoursLast;
bool gAMlast;
long int gTimeNow, gHKTimeNow;
const char *myMonthStr[] = { "", "Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec" };
const char *myDowStr[] = {"", "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat" };
bool isOledOn = true;

// NTP
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP);

// Query interval
#define kNtpInterval 1000
// Set the timer to trigger immediately.
unsigned long gNtpTimer = 0;

// Web Server
ESP8266WebServer server(80);

void setup() {
  Serial.begin(115200);
  gDayLast = gMonthLast = gYearLast = gSecondsLast = gMinutesLast = gHoursLast = 0;
  gAMlast = true;
  // Initialize OLED
  Heltec.begin(true /*DisplayEnable Enable*/, true /*Serial Enable*/);
  // We start by connecting to a WiFi network
  setupWiFi();
  drawStatus();
  // The timeClient does all the NTP work.
  timeClient.begin();

  // IRServer
  irsend.begin();

  // Web Server
  server.on("/", handleRoot);
  server.on("/ir", handleIr);

  server.on("/setClock", []() {
    for (int i = 0; i < server.args(); i++) {
      if (server.argName(i) == "city") {
        kCityName = server.arg(i);
      };
      if (server.argName(i) == "timezone") {
        kTimeZone = server.arg(i).toInt();
      };
    }
    server.send(200, "text/plain", "Clock set to " + kCityName + " and timezone to " + kTimeZone);
    Heltec.display->clear();
  });
  server.onNotFound(handleNotFound);
  server.begin();
}

void loop() {
  // Web Server
  server.handleClient();
  MDNS.update();

  // Check time
  if (millis() >= gNtpTimer) {
    gNtpTimer = millis() + kNtpInterval;
    timeClient.update();

    // Sleep time for preserve OLED screen
    gHKTimeNow = timeClient.getEpochTime() + (8 * 3600);
    gHKHours = hour(gHKTimeNow);
    
    if ((hour(gHKTimeNow) > 1) && (hour(gHKTimeNow) < 6)) {
      // turn off OLED from 2am to 5:59am
      if (isOledOn) {
        Heltec.display->displayOff(); // To switch display off
        isOledOn = false;
            Serial.println("Display off from 2am to 5:59am");
      }
    } else {
      // Turn on OLED from 6am to 1:59am
      if (!isOledOn) {
        Heltec.display->displayOn(); // To switch display off
        isOledOn = true;
        Serial.println("Display on from 6am to 1:59am");
      }
      // Get epoch time and adjust it according to the local time zone.
      gTimeNow = timeClient.getEpochTime() + (kTimeZone * 3600);
      gDay = day(gTimeNow);
      gMonth = month(gTimeNow);
      gYear = year(gTimeNow);
      gHours = hourFormat12(gTimeNow);
      gMinutes = minute(gTimeNow);
      gSeconds = second(gTimeNow);
      drawDate();
      drawTime();
    }
    /*
      Serial.print(gTimeNow);
      Serial.print(" ");
      Serial.print(gDay);
      Serial.print("/");
      Serial.print(gMonth);
      Serial.print("/");
      Serial.print(gYear);
      Serial.print(" ");
      Serial.print(gHours);
      Serial.print(":");
      Serial.print(gMinutes);
      Serial.print(":");
      Serial.print(gSeconds);
      Serial.println();
    */
  }
}

// OLED Methods

void drawStatus() {
  Heltec.display->clear();
  Heltec.display->setFont(ArialMT_Plain_10);
  Heltec.display->drawString(0, 0, "[ WIFI ]");
  Heltec.display->drawString(0, 10, "Connected to " + String(ssid));
  Heltec.display->drawString(0, 20, "IP: " + WiFi.localIP().toString());
  Heltec.display->display();
  delay(5000);
  Heltec.display->clear();
}

void drawDate() {
  // Assemble date string.
  String dateStr =   String(myMonthStr[gMonth]) + " " + String(gDay) + ", " + String(myDowStr[weekday(gTimeNow)]);
  // Small font for date display
  Heltec.display->setFont(ArialMT_Plain_10);
  if (gDay != gDayLast) {
    // New day. Clear the entire display.
    gDayLast = gDay;
    Heltec.display->clear();
  }

  Heltec.display->drawString(0, 0,  kCityName);
  Heltec.display->setTextAlignment(TEXT_ALIGN_RIGHT);
  Heltec.display->drawString(128, 0, dateStr);
  Heltec.display->setTextAlignment(TEXT_ALIGN_LEFT);
  Heltec.display->display();
}

void drawTime() {
  // Large font for time display
  Heltec.display->setFont(ArialMT_Plain_24);
  // Erase last value if needed and set new last value.
  gHoursLast = eraseOldValueIfNeeded(5, gHoursLast, gHours);
  gMinutesLast = eraseOldValueIfNeeded(39, gMinutesLast, gMinutes);
  gSecondsLast = eraseOldValueIfNeeded(74, gSecondsLast, gSeconds);
  // Draw new time
  Heltec.display->setColor(WHITE);
  Heltec.display->drawString(5, 10, makeStringWithLeadingZeroIfNeeded(gHours));
  Heltec.display->drawString(31, 10, ":");
  Heltec.display->drawString(39, 10, makeStringWithLeadingZeroIfNeeded(gMinutes));
  Heltec.display->drawString(67, 10, ":");
  Heltec.display->drawString(74, 10, makeStringWithLeadingZeroIfNeeded(gSeconds));
  addAMorPM();
  Heltec.display->display();
}

void addAMorPM() {
  // Small font for AM/PM
  Heltec.display->setFont(ArialMT_Plain_10);
  if (isPM(gTimeNow)) {
    // PM
    if (gAMlast) {
      // Change from AM to PM, erase AM
      gAMlast = false;
      Heltec.display->setColor(BLACK);
      Heltec.display->drawString(105, 22, "AM");
      Heltec.display->display();
      Heltec.display->setColor(WHITE);
    }
    Heltec.display->drawString(105, 22, "PM");
  }
  else {
    // AM
    if (!gAMlast) {
      // Change from PM to AM, erase PM
      gAMlast = true;
      Heltec.display->setColor(BLACK);
      Heltec.display->drawString(105, 22, "PM");
      Heltec.display->display();
      Heltec.display->setColor(WHITE);
    }
    Heltec.display->drawString(105, 22, "AM");
  }
}

// Add a leading zero if the time is one digit.
String makeStringWithLeadingZeroIfNeeded(int theTime) {
  String timeStr;
  if (theTime < 10)
    timeStr = "0" + String(theTime);
  else
    timeStr = String(theTime);
  return timeStr;
}

// Draw the old value in black to erase it.
// This avoids the flicker of the entire screen caused by clear.
int eraseOldValueIfNeeded(int xLoc, int oldValue, int newValue) {
  if (newValue != oldValue) {
    // Value changed, erase old value by redrawing it in black.
    String oldValueStr = makeStringWithLeadingZeroIfNeeded(oldValue);
    Heltec.display->setColor(BLACK);
    Heltec.display->drawString(xLoc, 10, oldValueStr);
    Heltec.display->display();
  }
  return newValue;
}

void setupWiFi(void)
{
  // Set WiFi to station mode and disconnect from an AP if it was previously connected
  WiFi.disconnect(true);
  delay(100);
  WiFi.mode(WIFI_STA);
  WiFi.setAutoConnect(true);
  WiFi.begin(ssid, pass);
  delay(100);
  Heltec.display->clear();

  byte count = 0;
  String connectingStr = "Connecting";
  while (WiFi.status() != WL_CONNECTED && count < 100)
  {
    count ++;
    delay(500);
    Heltec.display->drawString(0, 0, connectingStr);
    Heltec.display->display();
    // Display a dot for each attempt.
    connectingStr += ".";
  }  //Heltec.display->clear();
  if (WiFi.status() != WL_CONNECTED)
  {
    Heltec.display->drawString(0, 9, "Failed");
    Heltec.display->display();
    delay(1000);
    Heltec.display->clear();
  }
}

void handleNotFound() {
  String message = "File Not Found\n\n";
  message += "URI: ";
  message += server.uri();
  message += "\nMethod: ";
  message += (server.method() == HTTP_GET) ? "GET" : "POST";
  message += "\nArguments: ";
  message += server.args();
  message += "\n";
  for (uint8_t i = 0; i < server.args(); i++) {
    message += " " + server.argName(i) + ": " + server.arg(i) + "\n";
  }
  server.send(404, "text/plain", message);
}

void handleRoot() {
  server.send(200, "text/html",
              "<!doctype html>" \
              "<html lang=\"en\">" \
              "<head><title>ESP NTP OLED Clock with IR Sender</title>" \
              "<meta http-equiv=\"Content-Type\" " \
              "content=\"text/html;charset=utf-8\">" \
              "<meta name=\"viewport\" content=\"width=device-width," \
              "initial-scale=1.0,minimum-scale=1.0," \
              "maximum-scale=5.0\">" \
              "<link href=\"https://cdn.jsdelivr.net/npm/bootstrap@5.1.3/dist/css/bootstrap.min.css\" rel=\"stylesheet\" integrity=\"sha384-1BmE4kWBq78iYhFldvKuhfTAU6auU8tT94WrHftjDbrCEXSU1oBoqyl2QvZ6jIW3\" crossorigin=\"anonymous\">" \
              "</head>" \
              "<body>" \
              "<div class=\"container-fluid\">" \
              "<h1>ESP NTP OLED Clock with IR Sender</h1>" \
              "<hr/><h3>3 ports HDMI Switch</h3><br/>" \
              "<p><a href=\"ir?code=546791583\">port 1</a></p>" \
              "<p><a href=\"ir?code=546807903\">port 2</a></p>" \
              "<p><a href=\"ir?code=546822183\">port 3</a></p>" \
              "<hr/><h3>Machine</h3><br/>" \
              "<p><a href=\"ir?kvm=dell\">Dell Corp Laptop</a></p>" \
              "<p><a href=\"ir?kvm=mbp\">MacBook Pro</a></p>" \
              "<p><a href=\"ir?kvm=nuc\">NUC</a></p>" \
              "<p><a href=\"ir?switch=a3\">switch A3</a></p>" \
              "<hr/><h3>Switch all ports</h3><br/>" \
              "<p><a href=\"ir?port=1\">port 1</a></p>" \
              "<p><a href=\"ir?port=2\">port 2</a></p>" \
              "<p><a href=\"ir?port=3\">port 3</a></p>" \
              "<hr/>" \
              "<p><a href=\"\\\">Home</a></p>" \
              "</div>" \
              "</body>" \
              "</html>");
}

void handleIr() {
  for (uint8_t i = 0; i < server.args(); i++) {
    if (server.argName(i) == "code") {
      //uint32_t code = strtoul(server.arg(i).c_str(), NULL, 10);
      //irsend.sendNEC(code, 32);

      //Serial.print("IR Send ");
      //Serial.println(code);
      displayOLEDMsg("IR: " + server.arg(i));

      sendIRCode(server.arg(i));  // argument is in DEC format, not HEX
    }
    if (server.argName(i) == "kvm") {
      if (server.arg(i) == "nuc") {
        displayOLEDMsg("Switch to NUC");
        sendKvmCode(3);
      }
      if (server.arg(i) == "mbp") {
        displayOLEDMsg("Switch to MacBook Pro");
        sendKvmCode(1);
      }
      if (server.arg(i) == "dell") {
        displayOLEDMsg("Switch to Dell Laptop");
        sendKvmCode(2);
      }
    }
    if (server.argName(i) == "port") {
      displayOLEDMsg("Switch all ports to " + server.arg(i));
      if (server.arg(i) == "1") {
        sendKvmCode(1);
      }
      if (server.arg(i) == "2") {
        sendKvmCode(2);
      }
      if (server.arg(i) == "3") {
        sendKvmCode(3);
      }
      if (server.arg(i) == "4") {
        sendKvmCode(4);
      }
      if (server.arg(i) == "5") {
        sendKvmCode(5);
      }
    }
  }
  delay(2000);
  Heltec.display->clear();
  handleRoot();
}

void sendIRCode(String theIRCode) {

  uint32_t code = strtoul(theIRCode.c_str(), NULL, 10);
  irsend.sendNEC(code, 32);

  Serial.print("IR Send ");
  Serial.println(code);
}

void sendKvmCode(int portNo) {
  if (portNo == 1) {
    sendIRCode("546791583");  // 3-port hdmi switch
    sendIRCode("16752735");   // 4 port kvm
    sendIRCode("33464415");   // 5 port hdmi switch
  }
  if (portNo == 2) {
    sendIRCode("546807903");  // 3-port hdmi switch
    sendIRCode("33480735");   // 5 port hdmi switch
    for (uint8_t i = 0; i < 3; i++) {
      delay(300);
      sendIRCode("16728255");   // 4 port kvm
    }
  }
  if (portNo == 3) {
    sendIRCode("546822183");  // 3-port hdmi switch
    sendIRCode("16760895");   // 4 port kvm
    for (uint8_t i = 0; i < 3; i++) {
      delay(300);
      sendIRCode("33427695");   // 5 port hdmi switch
    }
  }
  if (portNo == 4) {
    // 3-port hdmi switch
    sendIRCode("16769055");   // 4 port kvm
    sendIRCode("33460335");   // 5 port hdmi switch
  }
  if (portNo == 5) {
    // 3-port hdmi switch
    // 4 port kvm
    sendIRCode("33478695");   // 5 port hdmi switch
  }
}

void displayOLEDMsg(String theMsg) {
  Heltec.display->clear();
  Heltec.display->setFont(ArialMT_Plain_16);
  Heltec.display->setColor(WHITE);
  Heltec.display->drawString(0, 10, theMsg);
  Heltec.display->display();
}
