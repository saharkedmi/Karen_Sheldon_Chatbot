#include <Arduino.h>
#include <Wire.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <driver/i2s.h>
#include "config.h"

// הגדרות מסך OLED
#define SCREEN_WIDTH  64
#define SCREEN_HEIGHT 48
#define OLED_RESET    -1
#define I2C_SDA        8
#define I2C_SCL        9
#define SCREEN_ADDRESS 0x3C
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// שליחת פקודה ישירה למסך - נדרש לתיקון ה-Offset הפנימי של מסכי 0.66"
void sendOLEDCommand(uint8_t c) {
    Wire.beginTransmission(SCREEN_ADDRESS);
    Wire.write(0x00); // 0x00 מסמן פקודה
    Wire.write(c);
    Wire.endTransmission();
}

// הגדרות פינים I2S
#define I2S_MIC_WS    41
#define I2S_MIC_SCK   42
#define I2S_MIC_SD     1
#define I2S_AMP_LRC    4
#define I2S_AMP_BCLK   5
#define I2S_AMP_DOUT   6

// פין חיישן המגע TTP223
#define TOUCH_SENSOR_PIN 10

// הגדרות שרת ודגימה
const char* server_url = "http://10.0.0.33:5000/ask";
#define SAMPLE_RATE     16000
#define MAX_RECORD_TIME_SEC 20 

// נפח הבאפר המקסימלי בבייטים עבור 20 שניות (16kHz * 2 בייטים * 20)
#define MAX_BUFFER_SIZE (SAMPLE_RATE * 2 * MAX_RECORD_TIME_SEC) 

int16_t* audio_buffer = NULL;

// משתנים גלובליים לניהול האנימציה הלא-חוסמת של העיניים
unsigned long last_blink_time = 0;
bool eyes_closed = false;

void update_display(const char* line1, const char* line2 = "") {
    display.clearDisplay();
    display.setCursor(0,0);
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    display.println(line1);
    display.setCursor(0,16);
    display.println(line2);
    display.display();
}

// פונקציה לשרטוט עיניים ממורכזות ומצמצות (^^) ללא חסימת ה-CPU
void draw_eyes(bool closed) {
    display.clearDisplay();
    
    if (closed) {
        // עיניים עצומות - קווים אופקיים ישרים במרכז האנכי (Y=21)
        display.drawLine(12, 21, 26, 21, SSD1306_WHITE); // עין שמאל
        display.drawLine(38, 21, 52, 21, SSD1306_WHITE); // עין ימין
    } else {
        // עיניים פתוחות - צורת קארט (V הפוך) חדה ורובוטית
        // עין שמאל: קו עולה לקודקוד (19,16) וקו יורד לבסיס (26,26)
        display.drawLine(12, 26, 19, 16, SSD1306_WHITE);
        display.drawLine(19, 16, 26, 26, SSD1306_WHITE);
        
        // עין ימין: קו עולה לקודקוד (45,16) וקו יורד לבסיס (52,26)
        display.drawLine(38, 26, 45, 16, SSD1306_WHITE);
        display.drawLine(45, 16, 52, 26, SSD1306_WHITE);
    }
    display.display();
}

void init_microphone() {
    i2s_config_t i2s_config = {
        .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX),
        .sample_rate = SAMPLE_RATE,
        .bits_per_sample = I2S_BITS_PER_SAMPLE_32BIT,
        .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT,
        .communication_format = I2S_COMM_FORMAT_STAND_I2S,
        .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
        .dma_buf_count = 8,
        .dma_buf_len = 256,
        .use_apll = false
    };
    i2s_pin_config_t pin_config = {
        .bck_io_num = I2S_MIC_SCK,
        .ws_io_num = I2S_MIC_WS,
        .data_out_num = I2S_PIN_NO_CHANGE,
        .data_in_num = I2S_MIC_SD
    };
    i2s_driver_install(I2S_NUM_0, &i2s_config, 0, NULL);
    i2s_set_pin(I2S_NUM_0, &pin_config);
}

void init_speaker() {
    i2s_config_t i2s_config = {
        .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX),
        .sample_rate = SAMPLE_RATE,
        .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
        .channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT,
        .communication_format = I2S_COMM_FORMAT_STAND_I2S,
        .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
        .dma_buf_count = 8,
        .dma_buf_len = 256,
        .use_apll = false,
        .tx_desc_auto_clear = true 
    };
    i2s_pin_config_t pin_config = {
        .bck_io_num = I2S_AMP_BCLK,
        .ws_io_num = I2S_AMP_LRC,
        .data_out_num = I2S_AMP_DOUT,
        .data_in_num = I2S_PIN_NO_CHANGE
    };
    i2s_driver_install(I2S_NUM_1, &i2s_config, 0, NULL);
    i2s_set_pin(I2S_NUM_1, &pin_config);
}

void setup() {
    Serial.begin(115200);
    
    pinMode(TOUCH_SENSOR_PIN, INPUT_PULLDOWN);

    // אתחול ה-I2C במהירות גבוהה של 400kHz התואמת לבדיקה שלך
    Wire.begin(I2C_SDA, I2C_SCL, 400000);
    if(!display.begin(SSD1306_SWITCHCAPVCC, SCREEN_ADDRESS)) {
        Serial.println("OLED Failed");
        while (true) { delay(1000); }
    }

    // החלת תיקון ה-OFFSET המדויק עבור פאנל 0.66"
    sendOLEDCommand(0xD3); sendOLEDCommand(0x00);
    sendOLEDCommand(0xDA); sendOLEDCommand(0x12);
    sendOLEDCommand(0x40);

    update_display("TARS System", "Connecting WiFi...");
    
    WiFi.begin(ssid, password);
    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print(".");
    }
    
    update_display("WiFi Connected", WiFi.localIP().toString().c_str());
    delay(1500);

    init_microphone();
    init_speaker();
    
    audio_buffer = (int16_t*)ps_malloc(MAX_BUFFER_SIZE);
    if (audio_buffer == NULL) {
        update_display("Memory Error", "PSRAM Failed");
        while (true) { delay(1000); }
    }

    // ציור העיניים לראשונה במצב פתוח עם סיום ה-Setup
    draw_eyes(false);
    last_blink_time = millis();
}

void record_and_send() {
    update_display("[ Listening ]", "Talk now...");
    
    size_t bytes_read = 0;
    int32_t raw_sample = 0;
    int actual_samples_recorded = 0;
    
    unsigned long start_time = millis();
    unsigned long max_duration_ms = MAX_RECORD_TIME_SEC * 1000;

    while (digitalRead(TOUCH_SENSOR_PIN) == HIGH && (millis() - start_time < max_duration_ms)) {
        i2s_read(I2S_NUM_0, &raw_sample, sizeof(raw_sample), &bytes_read, portMAX_DELAY);
        
        audio_buffer[actual_samples_recorded] = (raw_sample >> 14);
        actual_samples_recorded++;
        
        if (actual_samples_recorded >= (SAMPLE_RATE * MAX_RECORD_TIME_SEC)) {
            break;
        }
    }

    size_t final_payload_size = actual_samples_recorded * 2;

    if (actual_samples_recorded < (SAMPLE_RATE * 0.5)) {
        update_display("Too Short", "Hold longer!");
        delay(1500);
        // החזרת העיניים למסך
        draw_eyes(false);
        return;
    }

    update_display("[ Processing ]", "Sending to TARS...");

    if (WiFi.status() == WL_CONNECTED) {
        HTTPClient http;
        http.begin(server_url);
        
        http.setTimeout(30000);
        http.addHeader("Content-Type", "application/octet-stream");

        int httpResponseCode = http.POST((uint8_t*)audio_buffer, final_payload_size);

        if (httpResponseCode == 200) {
            update_display("[ Speaking ]", "TARS Responding...");
            
            WiFiClient* stream = http.getStreamPtr();
            
            uint8_t header_junk[44];
            stream->readBytes(header_junk, 44);
            
            uint8_t play_buf[512];       
            int16_t stereo_buf[512];     
            size_t bytes_written = 0;

            while (stream->available() || stream->connected()) {
                int len = stream->readBytes(play_buf, sizeof(play_buf));
                if (len > 0) {
                    int16_t* mono_samples = (int16_t*)play_buf;
                    int mono_count = len / 2;
                    for (int i = 0; i < mono_count; i++) {
                        stereo_buf[i * 2]     = mono_samples[i];
                        stereo_buf[i * 2 + 1] = mono_samples[i];
                    }
                    i2s_write(I2S_NUM_1, stereo_buf, mono_count * 2 * sizeof(int16_t), &bytes_written, portMAX_DELAY);
                } 
                else {
                    break;
                }
            }
        } else {
            update_display("Server Error", String(httpResponseCode).c_str());
            delay(2000);
        }
        http.end();
    } else {
        update_display("WiFi Lost", "Reconnecting...");
    }
    
    // החזרת העיניים למצב פתוח בסיום האינטראקציה ואיפוס טיימר המצמוץ
    draw_eyes(false);
    eyes_closed = false;
    last_blink_time = millis();
}

void loop() {
    // בדיקת לחיצה אקטיבית
    if (digitalRead(TOUCH_SENSOR_PIN) == HIGH) {
        delay(50); 
        if (digitalRead(TOUCH_SENSOR_PIN) == HIGH) {
            record_and_send();
        }
    } 
    else {
        // ניהול האנימציה הלא-חוסמת של העיניים במצב המתנה (Idle)
        unsigned long current_time = millis();
        unsigned long interval = eyes_closed ? 200 : 5000; // 200ms סגור, 5000ms פתוח
        
        if (current_time - last_blink_time >= interval) {
            eyes_closed = !eyes_closed;
            last_blink_time = current_time;
            draw_eyes(eyes_closed);
        }
    }
    delay(10);
}