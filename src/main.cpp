#include <WiFi.h>
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <time.h>

#define PIN_DEBUG_INPUT 23

const char *api_url = "https://script.google.com/macros/s/AKfycbyu_4RquORW2SyUHv0RQjGniXkfEGH-mXoOauUl6COUvI957OHa9OOB2ArlN5ihav7c/exec";

HTTPClient http_client;

void setup()
{
    Serial.begin(115200);
    delay(100);

    pinMode(PIN_DEBUG_INPUT, INPUT_PULLUP);

    Serial.print("\nConnecting");

    WiFi.begin();
    while (WiFi.status() != WL_CONNECTED)
    {
        Serial.print('.');
        delay(500);
    }
    Serial.println();
    Serial.printf("Connected, IP address: ");
    Serial.println(WiFi.localIP());

    configTzTime("JST-9", "ntp.nict.jp");

    http_client.begin(api_url);
    http_client.addHeader("Content-Type", "application/json");

    delay(5000);
}

void loop()
{
    static unsigned long last_millis = 0;
    static unsigned long failed_count = 0;

    const unsigned long new_millis = millis();
    const unsigned long interval_millis = new_millis - last_millis;
    const time_t t = time(NULL);
    const struct tm *tm = localtime(&t);

    StaticJsonDocument<JSON_OBJECT_SIZE(3)> json_array;
    json_array["client_unixtime"] = t;
    json_array["interval_millis"] = interval_millis;
    json_array["failed_count"] = failed_count;

    String json_string;
    serializeJson(json_array, json_string);

    const int status_code = http_client.POST(json_string);
    Serial.printf("POST|%d|%6lu|%lu\n", status_code, interval_millis, failed_count);

    if (status_code == 200 and digitalRead(PIN_DEBUG_INPUT))
    {
        last_millis = new_millis;
        failed_count = 0;
    }
    else
    {
        failed_count += 1;
    }
}
