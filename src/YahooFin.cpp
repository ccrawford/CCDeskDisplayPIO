#include "Arduino.h"
#include "YahooFin.h"
#include <WiFi.h>
#include <HTTPClient.h>
#include <time.h>
#include "yahoo_cert.h"
#include <ArduinoJson.h>

YahooFin::YahooFin(char* symbol)
{
  _symbol = symbol;
  regularMarketPrice = 0;
  lastUpdateOfDayDone = false;
}

bool YahooFin::isMarketOpen()
{
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)){
    ESP_LOGE("CCD","%s","Couldn't get local time");
  }
  return ((timeinfo.tm_wday > 0 && timeinfo.tm_wday < 6) 
      && ((timeinfo.tm_hour > 8 || (timeinfo.tm_hour==8 && timeinfo.tm_min >=30)) 
      && timeinfo.tm_hour < 15));
}

bool YahooFin::isChangeInteresting()
{
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)){
    ESP_LOGE("CCD","%s","Couldn't get local time");
  }

//  Serial.printf("Day: %d, Hour: %d, Min: %d\n",timeinfo.tm_wday, timeinfo.tm_hour, timeinfo.tm_min);

  //Change is only interesting during the trading day or in the evening after.
  return ((timeinfo.tm_wday > 0 && timeinfo.tm_wday < 6) 
      && ((timeinfo.tm_hour > 8 || (timeinfo.tm_hour==8 && timeinfo.tm_min >=30))));  
}

void YahooFin::getQuote()
{
  Serial.printf("Getting quote for %s. Mkt open? %d price? %f, last update done? %d\n", this->_symbol, this->isMarketOpen(), regularMarketPrice, lastUpdateOfDayDone);
  if (this->isMarketOpen() || regularMarketPrice == 0 || !lastUpdateOfDayDone)
  {
    lastUpdateOfDayDone = !this->isMarketOpen();
    
    HTTPClient client;
  
    client.useHTTP10(true);
    
    DynamicJsonDocument doc(8192);
   ESP_LOGD("CCD","%s","Doc capacity: %d", doc.capacity());
   
   StaticJsonDocument<512> filter;
   filter["chart"]["result"][0]["meta"]["regularMarketPrice"]= true;
   filter["chart"]["result"][0]["meta"]["chartPreviousClose"]= true;
   filter["chart"]["result"][0]["indicators"]["quote"][0]["high"][0]= true;
   filter["chart"]["result"][0]["indicators"]["quote"][0]["low"][0]= true;
   

   char url[80];
   sprintf(url, "https://query1.finance.yahoo.com/v8/finance/chart/%s?interval=1d",_symbol); 
   client.begin(url, cert_DigiCert_SHA2_High_Assurance_Server_CA);
   int httpCode = client.GET();

   if (httpCode > 0) {
     
     auto err = deserializeJson(doc, client.getStream(), DeserializationOption::Filter(filter));
     client.end();

     if (err) {
       ESP_LOGE("CCD","%s","Failed to parse response to JSON with " + String(err.c_str()));
     }

   // serializeJsonPretty(doc, Serial);
  
      regularMarketPrice=doc["chart"]["result"][0]["meta"]["regularMarketPrice"].as<float>(); 
      regularMarketPreviousClose=doc["chart"]["result"][0]["meta"]["chartPreviousClose"].as<float>();
      regularMarketDayHigh=doc["chart"]["result"][0]["indicators"]["quote"][0]["high"][0].as<float>();
      regularMarketDayLow=doc["chart"]["result"][0]["indicators"]["quote"][0]["low"][0].as<float>();

      if(regularMarketPreviousClose != 0)
      {
        regularMarketChangePercent= (regularMarketPrice/regularMarketPreviousClose) - 1;
        regularMarketChange=regularMarketPrice - regularMarketPreviousClose;
      }
      else
      {
        regularMarketChangePercent = 0;
        regularMarketChange = 0;
      }

      time(&lastUpdateTime);
    }
    else {
      ESP_LOGE("CCD","%s","Error on HTTP request");
      ESP_LOGE("CCD","%s",httpCode);
    }
    client.end();
  }
  
}

void YahooFin::getQuoteX()
{
  Serial.printf("Getting quote for %s. Mkt open? %d price? %f, last update done? %d\n", this->_symbol, this->isMarketOpen(), regularMarketPrice, lastUpdateOfDayDone);
  if (this->isMarketOpen() || regularMarketPrice == 0 || !lastUpdateOfDayDone)
  {
    lastUpdateOfDayDone = !this->isMarketOpen();
    
    HTTPClient client;
  
    client.useHTTP10(true);
    
    DynamicJsonDocument doc(6144);
  
    char url[80];
    sprintf(url, "https://query1.finance.yahoo.com/v6/finance/quoteSummary/%s?modules=price",_symbol);
    Serial.printf("Fetching: %s\n",url);  
    client.begin(url, cert_DigiCert_SHA2_High_Assurance_Server_CA);
    int httpCode = client.GET();
  
    if (httpCode > 0) {
      ESP_LOGV("CCD","%s",httpCode);
      auto err = deserializeJson(doc, client.getStream());
      if (err) {
        Serial.println("Failed to parse response to JSON with " + String(err.c_str()));
      }
  
      regularMarketPrice=doc["quoteSummary"]["result"][0]["price"]["regularMarketPrice"]["raw"].as<float>(); 
      regularMarketDayHigh=doc["quoteSummary"]["result"][0]["price"]["regularMarketDayHigh"]["raw"].as<float>();
      regularMarketDayLow=doc["quoteSummary"]["result"][0]["price"]["regularMarketDayLow"]["raw"].as<float>();
      regularMarketChangePercent=doc["quoteSummary"]["result"][0]["price"]["regularMarketChangePercent"]["raw"].as<float>();
      regularMarketChange=doc["quoteSummary"]["result"][0]["price"]["regularMarketChange"]["raw"].as<float>();
      regularMarketPreviousClose=doc["quoteSummary"]["result"][0]["price"]["regularMarketPreviousClose"]["raw"].as<float>();

      time(&lastUpdateTime);
    }
    else {
      ESP_LOGE("CCD","%s","Error on HTTP request");
      ESP_LOGE("CCD","%s",httpCode);
    }
  
    doc.clear();
    client.end();
  }
}

void YahooFin::getChart(){
   HTTPClient client;
   client.useHTTP10(true);
   
   DynamicJsonDocument doc(8192);
   ESP_LOGD("CCD","%s","Doc capacity: %d", doc.capacity());
   
   StaticJsonDocument<112> filter;
   filter["chart"]["result"][0]["indicators"]["quote"][0]["close"] = true;

   char url[80];
   sprintf(url, "https://query1.finance.yahoo.com/v8/finance/chart/%s?interval=2m",_symbol); 
   client.begin(url, cert_DigiCert_SHA2_High_Assurance_Server_CA);
   int httpCode = client.GET();

   if (httpCode > 0) {
     
     auto err = deserializeJson(doc, client.getStream(), DeserializationOption::Filter(filter));
     client.end();

     if (err) {
       ESP_LOGE("CCD","%s","Failed to parse response to JSON with " + String(err.c_str()));
     }
     JsonArray arr = doc["chart"]["result"][0]["indicators"]["quote"][0]["close"].as<JsonArray>();
     int i = 0;
     minuteDataPoints = 0;
     for (JsonVariant value : arr) {
        if(!value.isNull()) {
          if(i>=195) continue;
          minuteQuotes[i++] = value.as<double>();
          minuteDataPoints++;          
        }
     }
  }
  else {
    ESP_LOGE("CCD","%s","Error on HTTP request");
    client.end();
  }
  doc.clear();
}
