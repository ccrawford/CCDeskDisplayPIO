/*
   Nexion Control Panel.
   Chris Crawford, September 2021

   This project uses and ESP32 dev board connected to an Nextion NX4832K035_011 (320x480 3.5" enhanced display)
   Connect the Nextion to the ESP32 second serial port (RX2 and TX2). That's it.

   Yahoo API info: https://www.reddit.com/r/sheets/comments/ji52uk/yahoo_finance_api_url/
   https://www.reddit.com/r/sheets/wiki/apis/finance#wiki_multiple_lookup
   https://stackoverflow.com/questions/44030983/yahoo-finance-url-not-working

   Test stock API calls:
   URLS: https://query1.finance.yahoo.com/v8/finance/chart/ACN?interval=2m
   https://query1.finance.yahoo.com/v7/finance/quote?lang=en-US&region=US&corsDomain=finance.yahoo.com&symbols=ACN,^GSPC,^IXIC
   https://query1.finance.yahoo.com/v10/finance/quoteSummary/ACN?modules=price

   Libraries:
   https://github.com/Seithan/EasyNextionLibrary
   https://www.flaticon.com/packs/music-and-video-app-4
   https://github.com/knolleary/pubsubclient

   Supporting technology:
   https://github.com/TroyFernandes/hass-mqtt-mediaplayer

   Libraries:
   https://github.com/107-systems/107-Arduino-Debug Debug Macros
   https://github.com/Seithan/EasyNextionLibrary Useful here.
   https://github.com/eduardomarcos/arduino-esp32-restclient  BUT YOU HAVE TO MODIFY TO USE "CLIENT" NOT "CLIENT SECURE" in library. Should add that.
    So, in RestClient.h in the library, comment out WiFiClientSecure include and make the client_s variable type WiFiClient.
   https://arduinojson.org/ Massive, helpful library.
   https://github.com/knolleary/pubsubclient ("Arduino Client for MQTT" by Nick O'Leary)
*/


#define ESP32_RESTCLIENT_DEBUG

#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <EasyNextionLibrary.h>
#include <RestClient.h>
#include <time.h>
#include <ArduinoJson.h>
#include <PubSubClient.h>
#include <107-Arduino-Debug.hpp>
#include "YahooFin.h"
#include "CCSecrets.h" //Tokens, passwords, etc.

DEBUG_INSTANCE(160, Serial);

void getNtpTime();


// restClient is used to make API requests via the HomeAssistant server to control the Sonos.
RestClient restClient = RestClient(haServer, 8123);

EasyNex myNex(Serial2);

WiFiClient (espClient);
PubSubClient client(espClient);

#define ARDUINOJSON_USE_LONG_LONG 1
#define ARDUINOJSON_USE_DOUBLE 1

const char* ssid = STASSID;
const char* password = STAPSK;

char playerState[20];
int trackDuration;
int trackPosition;


// MQTT callback for media message
// Dont' forget to subscribe in the reconnect function.

void callback(char* topic, byte* payload, unsigned int length) {

  ESP_LOGI("CCD","%s","MQTT Message. Topic: [%s]", topic);
  // Serial.println("Callback.");

  if(!strcmp(topic, "stat/OfficeHeatPlug/POWER")) {
    char powerState[10];
    strncpy(powerState, (char *)payload, 2); //Just grab the ON or OF and check second letter.
    Serial.printf("MQTT Says: Heat Power Plug: %c\n", powerState[1]);
    if(powerState[1] == 'F')
    { 
      myNex.writeStr("page0.b2.pic=19");
      myNex.writeStr("heatState.val=0");
    }
    else 
    {
      myNex.writeStr("page0.b2.pic=33");
      myNex.writeStr("heatState.val=1");
    }
  }
    

  if (!strcmp(topic, "homeassistant/media_player/volume")) {
    char bufVol[6];
    strncpy(bufVol, (char *)payload, length);
    bufVol[length] = 0;
    int vol = atof(bufVol) * 100;
    ESP_LOGI("CCD","%s","Volume: %d", vol);
    myNex.writeNum("page3.j1.val", vol);
  }

  if (!strcmp(topic, "homeassistant/media_player/track")) {
    if (length > 100) length = 100;
    char track[101];
    strncpy(track, (char *)payload, length);
    track[length] = 0;
    ESP_LOGI("CCD","%s","track: %s", track);
    myNex.writeStr("page3.tTrack.txt", track);
  }
  if (!strcmp(topic, "homeassistant/media_player/state")) {
    char bufState[20];
    strncpy(bufState, (char *)payload, length);
    bufState[length] = 0;
    strcpy(playerState, bufState);
    ESP_LOGI("CCD","%s","State: %s", playerState);
    // Change button to show current state icon 9 is pause icon. icon 10 is play.
    // Pause the elapsed time ticker as well.
    if (!strcmp("playing", bufState)) {
      myNex.writeNum("page3.tm0.en", 1);
      myNex.writeStr("page3.bPlayPause.pic=9");
      // myNex.writeStr("vis p7,1");
    } else {
      myNex.writeNum("page3.tm0.en", 0);
      myNex.writeStr("page3.bPlayPause.pic=10");
      // myNex.writeStr("vis p7,0");
    }
  }
  if (!strcmp(topic, "homeassistant/media_player/artist")) {
    if (length > 100) length = 100;
    char artist[101];
    strncpy(artist, (char *)payload, length);
    artist[length] = 0;
    ESP_LOGI("CCD","%s","Artist: %s", artist);
    myNex.writeStr("page3.tArtist.txt", artist);
  }
  if (!strcmp(topic, "homeassistant/media_player/duration")) {
    char duration[6];
    strncpy(duration, (char *)payload, length);
    duration[length] = 0;
    trackDuration = atoi(duration);

    // We'll use that timer to update the progress. Since progress is always 0-100, we need to set the timer
    // to tick every 1% of the track. Timer is in ms. Duration in seconds. Progress bar in %. So, multiply by 1000/100=10.
    myNex.writeNum("page3.tm0.tim", trackDuration * 10);
    ESP_LOGI("CCD","%s","Duration: %d", trackDuration);

  }
  if (!strcmp(topic, "homeassistant/media_player/position")) {
    char bufPosition[6];
    strncpy(bufPosition, (char *)payload, length);
    bufPosition[length] = 0;
    trackPosition = atoi(bufPosition);
    ESP_LOGI("CCD","%s","Position: %d", trackPosition);
  }

  if (!strcmp(topic, "homeassistant/media_player/position_last_update")) {
    int yr, mo, da, hr, mn, se;
    struct tm timeinfo;

    char bufTime[36];
    strncpy(bufTime, (char *)payload, length);
    bufTime[length] = 0;

    sscanf(bufTime, "%d-%d-%d %d:%d:%d\+*", &yr, &mo, &da, &hr, &mn, &se);
    timeinfo.tm_sec = se;
    timeinfo.tm_min = mn;
    timeinfo.tm_hour = hr;
    timeinfo.tm_mday = da;
    timeinfo.tm_mon = mo - 1;
    timeinfo.tm_year = yr - 1900;

    time_t now;
    time(&now);
    int diffTime = difftime(mktime(gmtime(&now)), mktime(&timeinfo));
    ESP_LOGI("CCD","%s","DiffTime: %d", diffTime);

    // If this is a new time update, then reset the time.
    // TODO: deal with a mid-track update...messy.
    // Should also check position here...
    if (diffTime <= 1) {
      int curOffset = (int)((trackPosition * 100) / trackDuration); // Calculate what pct of track has been played.
      myNex.writeNum("page3.j0.val", curOffset);
    }

  }

}


void reconnect() {

  // Make sure network is up
  ESP_LOGI("CCD","%s","Wifi Status (3 is good!): %d", WiFi.status());
  /* Value  Constant  Meaning
    0 WL_IDLE_STATUS  temporary status assigned when WiFi.begin() is called
    1 WL_NO_SSID_AVAIL   when no SSID are available
    2 WL_SCAN_COMPLETED scan networks is completed
    3 WL_CONNECTED  when connected to a WiFi network
    4 WL_CONNECT_FAILED when the connection fails for all the attempts
    5 WL_CONNECTION_LOST  when the connection is lost
    6 WL_DISCONNECTED when disconnected from a network
  */
  // Loop until we're reconnected to the MQTT broker.

  while (!client.connected()) {
    ESP_LOGI("CCD","%s","Attempting MQTT connection...");

    if (client.connect("DesktopBuddy", "hass.mqtt", "trixie*1", 0, 0, 0, 0, 0)) {
      ESP_LOGI("CCD","%s","connected");
      client.subscribe("homeassistant/media_player/#");
      client.subscribe("stat/OfficeHeatPlug/POWER");
    } else {
      ESP_LOGI("CCD","%s","failed to connect to MQTT, Try again in 5 seconds");
      // Wait 5 seconds before retrying
      delay(5000);
    }
  }
}


// Update the RTC in the Nextion. Actual time dispaly handled over there.
void setNexionTime()
{
  getNtpTime();
  time_t rawtime;
  struct tm * timeinfo;
  time(&rawtime);
  timeinfo = localtime(&rawtime);

  Serial.printf("setting Nexion Time: %d:%2d:%2d\n", timeinfo->tm_hour, timeinfo->tm_min, timeinfo->tm_sec);

  char tmStrBuf[24];
  sprintf(tmStrBuf, "rtc%d=%d", 5, timeinfo->tm_sec);
  myNex.writeStr(tmStrBuf);
  sprintf(tmStrBuf, "rtc%d=%d", 4, timeinfo->tm_min);
  myNex.writeStr(tmStrBuf);
  sprintf(tmStrBuf, "rtc%d=%d", 3, timeinfo->tm_hour);
  myNex.writeStr(tmStrBuf);
  sprintf(tmStrBuf, "rtc%d=%d", 1, timeinfo->tm_mon + 1);
  myNex.writeStr(tmStrBuf);
  sprintf(tmStrBuf, "rtc%d=%d", 2, timeinfo->tm_mday);
  myNex.writeStr(tmStrBuf);
  sprintf(tmStrBuf, "rtc%d=%d", 0, timeinfo->tm_year + 1900);
  myNex.writeStr(tmStrBuf);

}


void updateGraph(char * symbol) {
  if (myNex.currentPageId != 2) {
    ESP_LOGI("CCD","%s","Not on page2, skipping graph stuff.");
    return;
  }

  // Update the detailed quote on page.
  char quote_msg[30];
  YahooFin yf = YahooFin(symbol);
  yf.getQuote();

  sprintf(quote_msg, "%.2f(%.2f/%.2f%%)", yf.regularMarketPrice, yf.regularMarketChange, yf.regularMarketChangePercent * 100);
  if (yf.regularMarketChange < 0) myNex.writeNum("t1.pco", 63488);
  else myNex.writeNum("t1.pco", 34784);

  myNex.writeStr("t1.txt", quote_msg);

  // Update the chart
  yf.getChart();

  if (yf.minuteDataPoints == 0) return;

  // Clear the graph...we can think about adding on to it later.
  myNex.writeStr("cle 2,0");
  myNex.writeStr("cle 2,1");

  // Change the line color based on up/down
  if (yf.regularMarketChange < 0) myNex.writeNum("s0.pco0", 63488);
  else myNex.writeNum("s0.pco0", 34784);

  // Figure out scale
  long scaleLow = floor(min(yf.regularMarketPreviousClose, yf.regularMarketDayLow)) * 100;
  long scaleHigh = ceil(max(yf.regularMarketPreviousClose, yf.regularMarketDayHigh)) * 100;

  char pc[18];  // pc is the previous close amount for the line.
  sprintf(pc, "add 2,1,%d", map((long)(yf.regularMarketPreviousClose * 100), scaleLow, scaleHigh, 0, 255));

  int high = 0;
  int highI = 0;
  int low = 999;
  int lowI = 0;
  int j = 0;

  for (int i = 0; i < yf.minuteDataPoints; i++)
  {

    if (yf.minuteQuotes[i] > 0) {

      long mappedVal = map((long)(yf.minuteQuotes[i] * 100), scaleLow, scaleHigh, 0, 255);

      if (mappedVal >= high) {
        high = mappedVal;
        highI = j;
      }

      if (mappedVal <= low) {
        low = mappedVal;
        lowI = j;
      }

      myNex.writeStr("add 2,0," + String(mappedVal));
      myNex.writeStr(pc);
      j++;

      //Stretch the graph a bit
      if (i % 3) {
        myNex.writeStr("add 2,0," + String(mappedVal));
        myNex.writeStr(pc);
        j++;
      }
    }
  }

  // Update the min/max/last overlay

  delay(80); //Delay allows the transparent text to work.

  char controlDesc[55];  //"xstr 245, 355,88,26,0,56154,0,0,1,3,123.45" about 45.
  sprintf(controlDesc, "xstr %d,%d,88,26,0,59164,0,0,1,3,\"%.2f\"", min(245, highI), 255 - high - 4, yf.regularMarketDayHigh);
  myNex.writeStr(controlDesc);

  sprintf(controlDesc, "xstr %d,%d,88,26,0,59164,0,0,1,3,\"%.2f\"", min(245, lowI), 255 - low + 26, yf.regularMarketDayLow);
  myNex.writeStr(controlDesc);

  sprintf(controlDesc, "xstr %d,%d,88,26,0,59164,0,0,1,3,\"%.2f\"", min(245, j), 255 - map((long)(yf.regularMarketPreviousClose * 100), scaleLow, scaleHigh, 0, 255), yf.regularMarketPreviousClose);
  myNex.writeStr(controlDesc);

  return;

}

// Select the current source for Sonos. Has to be in the Sonos favorites.
void selectSource(char* channelName) {
  char postParameter[80];
  sprintf(postParameter, "{\"entity_id\":\"media_player.sonos_5\", \"source\":\"%s\"}", channelName);

  restClient.setHeader(HA_TOKEN);
  restClient.post("/api/services/media_player/select_source", postParameter);

}

void mediaControl(char* command) {

  // See: https://www.home-assistant.io/integrations/media_player

  char service[100];
  sprintf(service, "/api/services/media_player/%s", command);
  ESP_LOGI("CCD","%s","Call: %s", service);
  restClient.setHeader(HA_TOKEN);
  restClient.post(service, "{\"entity_id\":\"media_player.sonos_5\"}");

}

void getNtpTime()
{
  configTime(0, 0, "pool.ntp.org", "time.nist.gov");
  setenv("TZ", "CST6CDT,M3.2.0,M11.1.0", 1);  // Chicago time zone via: https://github.com/nayarsystems/posix_tz_db/blob/master/zones.csv
  tzset();

  ESP_LOGI("CCD","%s","Waiting for NTP time sync: ");
  time_t now = time(NULL);
  while (now < 8 * 3600 * 2) {
    delay(500);
    ESP_LOGI("CCD","%s",".");
    now = time(NULL);
  }
}

void Wifi_disconnected(WiFiEvent_t event, WiFiEventInfo_t info) {
  Serial.println("Disconnected from WIFI access point");
  Serial.print("WiFi lost connection. Reason: ");
  // Serial.println(info.disconnected.reason);
  Serial.println("Reconnecting...");
  WiFi.begin(ssid, password);
}

void setup() {

 
  Serial.begin(115200);
  // Don't "Debug" this out. I want this to print regardless. Not time sensitive and very useful when you find an old ESP lying around.
  Serial.println(F("CCDeskDisplay.cpp Dec 2023"));

  myNex.begin(115200);

  ESP_LOGI("CCD","%s","Connecting to %s", ssid);

  WiFi.mode(WIFI_STA);
  // WiFi.onEvent(Wifi_disconnected, SYSTEM_EVENT_STA_DISCONNECTED);

  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    ESP_LOGI("CCD","%s",".");
  }

  ESP_LOGI("CCD","%s","IP address: %d.%d.%d.%d", WiFi.localIP()[0], WiFi.localIP()[1], WiFi.localIP()[2], WiFi.localIP()[3]);

  getNtpTime();
  time_t now = time(NULL);
  struct tm timeinfo;
  gmtime_r(&now, &timeinfo);
  setNexionTime();

  // Setup MQTT
  // client.setBufferSize(1024); Default size is 256.
  client.setServer(mqttServer, 1883);
  client.setCallback(callback);

  myNex.writeStr("page 0"); 

  ESP_LOGD("CCD","%s","=================SETUP DONE=================");
}

void setNextionBrightness(int brightness)
{
  static int curBrightness = -1;

  if (brightness < 0 || brightness > 100) return;

  if (brightness != curBrightness) {
    char msg[9];
    sprintf(msg, "dim=%d", brightness);
    myNex.writeStr(msg);
    ESP_LOGI("CCD","%s",msg);
    curBrightness = brightness;
  }
}


void getQuote(char* symbol, String field)
{
  Serial.printf("Getting quote for %s\n", symbol);
  
  YahooFin yf = YahooFin(symbol);
  yf.getQuote();

  char quote_msg[30];
  if (yf.isChangeInteresting())
  {
    sprintf(quote_msg, "$%.2f($%.2f/%.2f%%)", yf.regularMarketPrice, yf.regularMarketChange, yf.regularMarketChangePercent * 100);

    myNex.writeStr(field + ".txt", quote_msg);

    if (yf.regularMarketChange < 0) myNex.writeNum(field + ".pco", 63488);
    else myNex.writeNum(field + ".pco", 34784);
  }
  else
  {
    sprintf(quote_msg, "$%.2f", yf.regularMarketPrice);

    myNex.writeStr(field + ".txt", quote_msg);
    myNex.writeNum(field + ".pco", 65535);
  }
  Serial.printf("Quote back: %s\n", quote_msg);
  
}

YahooFin acn = YahooFin("ACN");
YahooFin sp500 = YahooFin("^GSPC");
YahooFin nasdaq = YahooFin("^IXIC");

void showQuote(YahooFin* yf, String field)
{
  yf->getQuote();
  char quote_msg[30];
  
  if(yf->isChangeInteresting())
  {
//    Serial.println("Change is interesting.");
    sprintf(quote_msg, "$%.2f($%.2f/%.2f%%)", yf->regularMarketPrice, yf->regularMarketChange, yf->regularMarketChangePercent * 100);

    myNex.writeStr(field + ".txt", quote_msg);

    if (yf->regularMarketChange < 0) myNex.writeNum(field + ".pco", 63488);
    else myNex.writeNum(field + ".pco", 34784);
  }
  else
  {
    sprintf(quote_msg, "$%.2f", yf->regularMarketPrice);

    myNex.writeStr(field + ".txt", quote_msg);
    myNex.writeNum(field + ".pco", 65535);
    
  }
}

void updateQuotes()
{
  ESP_LOGI("CCD","%s","Cur page: %d", myNex.currentPageId);
  if (myNex.currentPageId != 0) {
    ESP_LOGI("CCD","%s","Not on page0, skipping.");
    return;
  }
  myNex.writeStr("t7.txt", "updating.");
  //getQuote("ACN", "tAcn");
  showQuote(&acn, "tAcn");
  myNex.writeStr("t7.txt", "updating .");
  //getQuote("^GSPC", "tSP");
  showQuote(&sp500, "tSP");
  myNex.writeStr("t7.txt", "updating  .");
  //getQuote("^IXIC", "tNAS");
  showQuote(&nasdaq, "tNAS");
  myNex.writeStr("t7.txt", "");
}

void trigger0() {
  mediaControl("media_play_pause");
}
// Turn office light on/off.
void trigger1() {
  restClient.setHeader(HA_TOKEN);
  int statusCode = restClient.post("/api/services/light/toggle",  "{\"entity_id\":\"light.office_dimmer\"}");
}
void trigger2() {
  selectSource("WXRT Over the Air");
}
void trigger3() {
  ESP_LOGI("CCD","%s","Vol down...");
  mediaControl("volume_down");
}
void trigger4() {
  ESP_LOGI("CCD","%s","Vol up...");
  mediaControl("volume_up");
}

void trigger6() {
  selectSource("Discover Weekly");
}
void trigger7() {
  selectSource("Daily Mix 1");
}
void trigger8() {
  selectSource("Daily Mix 2");
}
void trigger9() {
  selectSource("Daily Mix 3");
}
void trigger10() {
  selectSource("Daily Mix 4");
}
void trigger11() {
  selectSource("Daily Mix 5");
}
void trigger12() {
  selectSource("Daily Mix 6");
}
void trigger13() {
  mediaControl("media_next_track");
}
void trigger14() {
  selectSource("media_previous_track");
}
void trigger16() {
  updateQuotes();
}
void trigger17() {
  updateGraph("ACN");
}
void trigger18() {
  ESP_LOGD("CCD","%s","Update the player info, if possible.");
}
void trigger19() {
  ESP_LOGD("CCD","%s","In trigger 19 aka 0x13");
  client.publish("cmnd/OfficeHeatPlug/Power", "TOGGLE");
}
void trigger20() {
  selectSource("Release Radar");
}


unsigned long lastRefresh = 0;

void loop() {

  myNex.NextionListen();
  if (myNex.currentPageId != myNex.lastCurrentPageId)
  {
    Serial.printf("Cur Page: %d\n", myNex.currentPageId);
    myNex.lastCurrentPageId = myNex.currentPageId;
  }

  // Do MQTT checks
  if (!client.connected()) {
    ESP_LOGI("CCD","%s","reconnecting...");
    reconnect();
  }
  client.loop();

  // Refresh every minute when market is open.
  // Also do basic housekeeping every minute.

  if ((millis() - lastRefresh) > 60000) {

    struct tm timeinfo;
    static int timeSetDay = -1;

    lastRefresh = millis();
    if (!getLocalTime(&timeinfo)) ESP_LOGE("CCD","%s","Couldn't get local time");
    ESP_LOGV("CCD","%s","Current time: %d:%2d:%2d", timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);

    //I'm using Chicago time here, but markets operate on Eastern time.
    // Probably a bad idea (GMT wouldn't be any better), but too much trouble to shift to Eastern.
    // Market open 8:30am to 4pm, M-F. Don't stress about market holidays.

    int secondsSinceMidnight = timeinfo.tm_hour * 3600 + timeinfo.tm_min * 60 + timeinfo.tm_sec;
    if ((timeinfo.tm_wday >= 1 && timeinfo.tm_wday <= 5) &&
        ((secondsSinceMidnight >= 8 * 3600 + 30 * 60) && (secondsSinceMidnight < 15 * 3600 + 35 * 60 ))) // Market open from 8:30am to 3:30pm CST.
    {
      ESP_LOGV("CCD","%s","Market Open.");
      // These functions only update page if page is active.
      updateQuotes();
      updateGraph("ACN");
    }
    else {
      ESP_LOGV("CCD","%s","Market Closed.");
    }

    // Update the Nexion clock around 2am, but only one time! Remember: This block only runs every minute.
    // Update the quotes while we're there to remove the percent change
    if (timeSetDay != timeinfo.tm_mday && timeinfo.tm_hour == 2) {
      setNexionTime();
      ESP_LOGI("CCD","%s","Updated the time on the Nexion");
      timeSetDay = timeinfo.tm_mday; // Limit update to once per day.
      updateQuotes();
    }

    // Dim the Nexion overnight. Could probably even shut it down, or tie it into the office lighting.
    if (timeinfo.tm_hour >= 23 || timeinfo.tm_hour <= 6) {
      setNextionBrightness(2);
    }
    else {
      setNextionBrightness(100);  // Function will only write to device if necessary. No need to track here.
    }
  }

}
