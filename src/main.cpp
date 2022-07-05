#include <WiFi.h>
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <time.h>

#include "define.h"

#define PIN_SPEAKER_OUTPUT 33
#define PIN_DEBUG_INPUT 23

enum class LEDC_CHANNEL
{
    SPEAKER
};

enum class TIMER_NUM
{
    SPEAKER
};

enum class SOUND_STATE
{
    INITIAL,
    PHASE_1,
    PHASE_2,
    ONESHOT,
    OFF,
    NONE,
};

enum class CONNECTION_STATE
{
    INITIAL,
    CONNECTING,
    CONNECTED,
    BROKEN,
};

HTTPClient http_client;
StaticJsonDocument<JSON_OBJECT_SIZE(2) + JSON_OBJECT_SIZE(2) + JSON_OBJECT_SIZE(4)> doc;
JsonObject json_contents;
CONNECTION_STATE connection_state = CONNECTION_STATE::INITIAL;

void IRAM_ATTR on_timer();
void init_pin();
void init_timer();

void output_sound(SOUND_STATE set_sound_state = SOUND_STATE::NONE)
{
    static SOUND_STATE sound_state = SOUND_STATE::ONESHOT;

    if (set_sound_state != SOUND_STATE::NONE)
    {
        sound_state = set_sound_state;
    }

    switch (sound_state)
    {
    case SOUND_STATE::INITIAL:
    case SOUND_STATE::PHASE_1:
        if (connection_state == CONNECTION_STATE::CONNECTING)
        {
            ledcWriteTone(static_cast<uint8_t>(LEDC_CHANNEL::SPEAKER), 0);
        }
        else
        {
            ledcWriteNote(static_cast<uint8_t>(LEDC_CHANNEL::SPEAKER), NOTE_B, 4);
        }
        sound_state = SOUND_STATE::PHASE_2;
        break;
    case SOUND_STATE::PHASE_2:
        if (connection_state == CONNECTION_STATE::CONNECTING)
        {
            ledcWriteTone(static_cast<uint8_t>(LEDC_CHANNEL::SPEAKER), 0);
        }
        else
        {
            ledcWriteNote(static_cast<uint8_t>(LEDC_CHANNEL::SPEAKER), NOTE_G, 4);
        }
        sound_state = SOUND_STATE::PHASE_1;
        break;
    case SOUND_STATE::ONESHOT:
        ledcWriteNote(static_cast<uint8_t>(LEDC_CHANNEL::SPEAKER), NOTE_G, 5);
        sound_state = SOUND_STATE::OFF;
        break;
    case SOUND_STATE::OFF:
        ledcWriteTone(static_cast<uint8_t>(LEDC_CHANNEL::SPEAKER), 0);
        break;
    default:
        break;
    }
}

hw_timer_t *timer = nullptr;
void IRAM_ATTR on_timer()
{
    output_sound();
}

void init_pin()
{
    pinMode(PIN_SPEAKER_OUTPUT, OUTPUT);
    pinMode(PIN_DEBUG_INPUT, INPUT_PULLUP);
    ledcAttachPin(PIN_SPEAKER_OUTPUT, static_cast<uint8_t>(LEDC_CHANNEL::SPEAKER));
}

void init_timer()
{
    timer = timerBegin(static_cast<uint8_t>(TIMER_NUM::SPEAKER), getApbFrequency() / 1000 * 1000 * 1000, true);
    timerAttachInterrupt(timer, &on_timer, true);
    timerAlarmWrite(timer, 5000, true);
}

void init_wifi()
{
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
}

void setup()
{
    Serial.begin(115200);
    delay(100);

    init_pin();
    init_timer();
    init_wifi();

    configTzTime("JST-9", "ntp.nict.jp");
    delay(5000);

    http_client.begin(API_URL);
    http_client.addHeader("Content-Type", "application/json");

    JsonObject json_spread_sheet = doc.createNestedObject("spread_sheet");
    json_contents = doc.createNestedObject("contents");

    json_spread_sheet["id"] = SPREAD_SHEET_ID;
    json_spread_sheet["sheet"] = SPREAD_SHEET_SHEET;
    json_contents["start_client_unixtime"] = time(NULL);

    timerAlarmEnable(timer);
}

void loop()
{
    static unsigned long last_millis = millis();
    static uint64_t failed_count = 0;
    static CONNECTION_STATE last_connection_state = CONNECTION_STATE::INITIAL;

    const unsigned long new_millis = millis();
    const unsigned long interval_millis = new_millis - last_millis;
    const time_t t = time(NULL);
    const struct tm *tm = localtime(&t);
    String json_string;

    json_contents["client_unixtime"] = t;
    json_contents["interval_millis"] = interval_millis;
    json_contents["failed_count"] = failed_count;

    serializeJson(doc, json_string);

    int status_code = 0;
    connection_state = CONNECTION_STATE::CONNECTING;
    if (digitalRead(PIN_DEBUG_INPUT))
    {
        status_code = http_client.POST(json_string);
    }

    connection_state = (status_code == 200) ? CONNECTION_STATE::CONNECTED : CONNECTION_STATE::BROKEN;

    switch (connection_state)
    {
    case CONNECTION_STATE::CONNECTED:
        if (last_connection_state == CONNECTION_STATE::BROKEN)
        {
            timerWrite(timer, 0);
            output_sound(SOUND_STATE::ONESHOT);
        }
        last_millis = new_millis;
        failed_count = 0;
        break;
    case CONNECTION_STATE::BROKEN:
        if (last_connection_state == CONNECTION_STATE::CONNECTED)
        {
            timerWrite(timer, 0);
            output_sound(SOUND_STATE::INITIAL);
        }
        failed_count += 1;
        delay(10);
        break;
    default:
        return;
        break;
    }

    last_connection_state = connection_state;

    Serial.printf("POST %d|%s\n", status_code, json_string.c_str());
}
