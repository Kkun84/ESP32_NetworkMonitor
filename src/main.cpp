#include <WiFi.h>
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <time.h>

#include "define.h"

#define PIN_SPEAKER_OUTPUT 33
#define PIN_DEBUG_INPUT 23

const time_t generate_id(void);

HTTPClient http_client;

StaticJsonDocument<JSON_OBJECT_SIZE(2) + JSON_OBJECT_SIZE(2) + JSON_OBJECT_SIZE(4)> doc;
JsonObject json_contents;

const time_t generate_id(void)
{
    return time(NULL);
}

void setup()
{
    Serial.begin(115200);
    delay(100);

    pinMode(PIN_SPEAKER_OUTPUT, OUTPUT);
    pinMode(PIN_DEBUG_INPUT, INPUT_PULLUP);

    ledcSetup(0, 440, 8);
    ledcAttachPin(PIN_SPEAKER_OUTPUT, 0);

    Serial.print("\nConnecting...");

    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    while (WiFi.status() != WL_CONNECTED)
    {
        delay(500);
        Serial.print('.');
    }
    Serial.println();
    Serial.printf("Connected, IP address: ");
    Serial.println(WiFi.localIP());
    Serial.println();

    configTzTime("JST-9", "ntp.nict.jp");
    delay(5000);

    http_client.begin(API_URL);
    http_client.addHeader("Content-Type", "application/json");

    JsonObject json_spread_sheet = doc.createNestedObject("spread_sheet");
    json_contents = doc.createNestedObject("contents");

    json_spread_sheet["id"] = SPREAD_SHEET_ID;
    json_spread_sheet["sheet"] = SPREAD_SHEET_SHEET;
    json_contents["start_client_unixtime"] = generate_id();
}

void loop()
{
    static unsigned long last_millis = millis();
    static unsigned long failed_count = 0;

    const unsigned long new_millis = millis();
    const unsigned long interval_millis = new_millis - last_millis;
    const time_t t = time(NULL);
    const struct tm *tm = localtime(&t);

    json_contents["client_unixtime"] = t;
    json_contents["interval_millis"] = interval_millis;
    json_contents["failed_count"] = failed_count;

    String json_string;
    serializeJson(doc, json_string);

    int status_code = 0;
    if (digitalRead(PIN_DEBUG_INPUT))
    {
        status_code = http_client.POST(json_string);
    }
    Serial.printf("POST %d|%s\n", status_code, json_string.c_str());
    if (status_code == 200)
    {
        ledcWrite(0, 0x00);
        last_millis = new_millis;
        failed_count = 0;
    }
    else
    {
        if ((interval_millis % 2000) < 500)
        {
            ledcWrite(0, 0x80);
        }
        else
        {
            ledcWrite(0, 0x00);
        }
        failed_count += 1;
        delay(10);
    }
}
