#include <WiFi.h>
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <time.h>

#include "define.h"

#define PIN_SPEAKER_OUTPUT 33
#define PIN_LIGHT_OUTPUT 32
#define PIN_DEBUG_INPUT 23

enum class LEDC_CHANNEL
{
    SPEAKER,
};

enum class TIMER_NUM
{
    SPEAKER,
};

enum class SPEAKER_STATE
{
    OFF,
    ON,
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

void output_speaker(CONNECTION_STATE connection_state)
{
    static SPEAKER_STATE speaker_state = SPEAKER_STATE::OFF;
    static uint8_t speaker_on_phase = 0;
    static uint8_t speaker_off_phase = 0;

    switch (connection_state)
    {
    case CONNECTION_STATE::CONNECTING:
        break;
    case CONNECTION_STATE::CONNECTED:
        speaker_state = SPEAKER_STATE::OFF;
        break;
    case CONNECTION_STATE::BROKEN:
        speaker_state = SPEAKER_STATE::ON;
        break;
    default:
        return;
    }

    switch (speaker_state)
    {
    case SPEAKER_STATE::ON:
        switch (speaker_on_phase)
        {
        default:
        case 0:
            speaker_on_phase = 0;
            ledcWriteNote(static_cast<uint8_t>(LEDC_CHANNEL::SPEAKER), NOTE_B, 4);
            break;
        case 1:
            ledcWriteNote(static_cast<uint8_t>(LEDC_CHANNEL::SPEAKER), NOTE_G, 4);
            break;
        }
        speaker_on_phase += 1;
        speaker_off_phase = 0;
        break;
    case SPEAKER_STATE::OFF:
        switch (speaker_off_phase)
        {
        default:
        case 0:
            ledcWriteNote(static_cast<uint8_t>(LEDC_CHANNEL::SPEAKER), NOTE_G, 5);
            break;
        case 1:
            ledcWriteTone(static_cast<uint8_t>(LEDC_CHANNEL::SPEAKER), 0);
            break;
        }
        speaker_on_phase = 0;
        speaker_off_phase = 1;
        break;
    default:
        return;
    }
}

hw_timer_t *timer_speaker = nullptr;
void IRAM_ATTR on_timer_speaker()
{
    output_speaker(connection_state);
}

void init_pin()
{
    pinMode(PIN_DEBUG_INPUT, INPUT_PULLUP);
    ledcAttachPin(PIN_SPEAKER_OUTPUT, static_cast<uint8_t>(LEDC_CHANNEL::SPEAKER));
    pinMode(PIN_LIGHT_OUTPUT, OUTPUT);
}

void init_timer()
{
    timer_speaker = timerBegin(static_cast<uint8_t>(TIMER_NUM::SPEAKER), getApbFrequency() / 1000 * 1000 * 1000, true);
    timerAttachInterrupt(timer_speaker, &on_timer_speaker, true);
    timerAlarmWrite(timer_speaker, 5000, true);
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

    timerWrite(timer_speaker, 0);
    output_speaker(connection_state);
    timerAlarmEnable(timer_speaker);
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

    int status_code = 0;
    String json_string;

    json_contents["client_unixtime"] = t;
    json_contents["interval_millis"] = interval_millis;
    json_contents["failed_count"] = failed_count;

    serializeJson(doc, json_string);

    connection_state = CONNECTION_STATE::CONNECTING;
    if (digitalRead(PIN_DEBUG_INPUT))
    {
        status_code = http_client.POST(json_string);
    }
    connection_state = (status_code == 200) ? CONNECTION_STATE::CONNECTED : CONNECTION_STATE::BROKEN;

    if (last_connection_state != connection_state)
    {
        timerWrite(timer_speaker, 0);
        output_speaker(connection_state);
    }

    switch (connection_state)
    {
    case CONNECTION_STATE::CONNECTED:
        last_millis = new_millis;
        failed_count = 0;
        break;
    case CONNECTION_STATE::BROKEN:
        failed_count += 1;
        delay(10);
        break;
    default:
        return;
        break;
    }

    last_connection_state = connection_state;

    digitalWrite(PIN_LIGHT_OUTPUT, !digitalRead(PIN_LIGHT_OUTPUT));

    Serial.printf("POST %d|%s\n", status_code, json_string.c_str());
}
