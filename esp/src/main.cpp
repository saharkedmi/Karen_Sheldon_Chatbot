#include <Arduino.h>
#include <Wire.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <driver/i2s.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>
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

// --- מנוע הבעות פנים ---
// הציור עצמו רץ ב-task נפרד על ליבה 0, כי הלולאה הראשית נחסמת לזמן ארוך
// (המתנה לתשובת שרת, כתיבת אודיו) ולא יכולה לרענן אנימציה בעצמה.
enum FaceState { FACE_SLEEP, FACE_LISTENING, FACE_THINKING, FACE_SPEAKING, FACE_MESSAGE };
volatile FaceState face_state = FACE_SLEEP;
volatile bool message_dirty = false;
char status_line1[21] = "";
char status_line2[21] = "";
SemaphoreHandle_t display_mutex;

const int EYE_L_X = 16;
const int EYE_R_X = 48;
const int EYE_Y   = 22;
const int MOUTH_X  = 32;
const int MOUTH_Y  = 34;
const int MOUTH_R  = 8;

// מצב המתנה - "ישן" (Z Z) - שני Z זהים באותו גודל ובאותו גובה
void draw_face_sleep() {
    display.clearDisplay();
    display.setTextColor(SSD1306_WHITE);
    display.setTextSize(2);
    display.setCursor(14, 16);
    display.print("Z");
    display.setCursor(38, 16);
    display.print("Z");
    display.display();
}

// פה מחייך סטטי - קשת תחתונה (חצי עיגול) מעובה בקצת בעזרת שני קווים
void draw_mouth_smile() {
    display.drawCircleHelper(MOUTH_X, MOUTH_Y - 1, MOUTH_R, 0xC, SSD1306_WHITE);
    display.drawCircleHelper(MOUTH_X, MOUTH_Y,     MOUTH_R, 0xC, SSD1306_WHITE);
}

// פה פתוח - לפריימי "דיבור" בהם הפה נפתח
void draw_mouth_open() {
    display.fillRoundRect(MOUTH_X - 6, MOUTH_Y - 3, 12, 7, 3, SSD1306_WHITE);
}

// מצב הקלטה - עיניים פקוחות ומקשיבות, פה מחייך סטטי
void draw_face_listening() {
    display.clearDisplay();
    display.fillCircle(EYE_L_X, EYE_Y, 9, SSD1306_WHITE);
    display.fillCircle(EYE_R_X, EYE_Y, 9, SSD1306_WHITE);
    display.fillCircle(EYE_L_X, EYE_Y, 3, SSD1306_BLACK);
    display.fillCircle(EYE_R_X, EYE_Y, 3, SSD1306_BLACK);
    draw_mouth_smile();
    display.display();
}

// מצב עיבוד - שני עיגולי טעינה פועמים במקום עיניים, פה מחייך סטטי
void draw_face_thinking(int frame) {
    static const uint8_t pulse_lut[6] = {3, 4, 5, 6, 5, 4};
    int r = pulse_lut[frame % 6];
    display.clearDisplay();
    display.drawCircle(EYE_L_X, EYE_Y, r, SSD1306_WHITE);
    display.drawCircle(EYE_R_X, EYE_Y, r, SSD1306_WHITE);
    draw_mouth_smile();
    display.display();
}

// מצב דיבור - עיניים מחייכות שזזות מעלה-מטה, פה נפתח ונסגר לסירוגין
void draw_face_speaking(int frame) {
    static const int8_t bounce_lut[4] = {0, 1, 2, 1};
    int cy = EYE_Y - bounce_lut[frame % 4];
    display.clearDisplay();
    display.fillCircle(EYE_L_X, cy, 7, SSD1306_WHITE);
    display.fillCircle(EYE_R_X, cy, 7, SSD1306_WHITE);
    display.fillRect(EYE_L_X - 8, cy, 16, 8, SSD1306_BLACK);
    display.fillRect(EYE_R_X - 8, cy, 16, 8, SSD1306_BLACK);
    if (frame % 2 == 0) {
        draw_mouth_smile();
    } else {
        draw_mouth_open();
    }
    display.display();
}

void draw_status_message() {
    display.clearDisplay();
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(0, 0);
    display.setTextSize(1);
    display.println(status_line1);
    display.setCursor(0, 16);
    display.println(status_line2);
    display.display();
}

// עדכון מסך טקסטואלי - שימוש רק לפני הרצת display_task (בשלב ה-setup)
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

// קביעת הודעת טקסט על המסך אחרי שה-display_task כבר רץ (נגישה מכל task)
void show_message(const char* line1, const char* line2) {
    strncpy(status_line1, line1, sizeof(status_line1) - 1);
    status_line1[sizeof(status_line1) - 1] = 0;
    strncpy(status_line2, line2, sizeof(status_line2) - 1);
    status_line2[sizeof(status_line2) - 1] = 0;
    message_dirty = true;
    face_state = FACE_MESSAGE;
}

void display_task(void* param) {
    int frame = 0;
    FaceState last_drawn = (FaceState)-1;
    for (;;) {
        FaceState s = face_state;
        bool animated = (s == FACE_THINKING || s == FACE_SPEAKING);
        bool message_changed = (s == FACE_MESSAGE && message_dirty);

        if (animated || s != last_drawn || message_changed) {
            xSemaphoreTake(display_mutex, portMAX_DELAY);
            switch (s) {
                case FACE_SLEEP:     draw_face_sleep();         break;
                case FACE_LISTENING: draw_face_listening();     break;
                case FACE_THINKING:  draw_face_thinking(frame); break;
                case FACE_SPEAKING:  draw_face_speaking(frame); break;
                case FACE_MESSAGE:   draw_status_message();     break;
            }
            xSemaphoreGive(display_mutex);
            last_drawn = s;
            if (s == FACE_MESSAGE) message_dirty = false;
        }
        frame++;
        vTaskDelay(pdMS_TO_TICKS(150));
    }
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

    // מכאן ואילך כל ציור על המסך עובר דרך display_task בלבד
    display_mutex = xSemaphoreCreateMutex();
    face_state = FACE_SLEEP;
    xTaskCreatePinnedToCore(display_task, "display_task", 4096, NULL, 1, NULL, 0);
}

void record_and_send() {
    face_state = FACE_LISTENING;

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
        show_message("Too Short", "Hold longer!");
        delay(1500);
        face_state = FACE_SLEEP;
        return;
    }

    face_state = FACE_THINKING;

    if (WiFi.status() == WL_CONNECTED) {
        HTTPClient http;
        http.begin(server_url);

        http.setTimeout(30000);
        http.addHeader("Content-Type", "application/octet-stream");

        int httpResponseCode = http.POST((uint8_t*)audio_buffer, final_payload_size);

        if (httpResponseCode == 200) {
            face_state = FACE_SPEAKING;

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
            char err_buf[16];
            snprintf(err_buf, sizeof(err_buf), "%d", httpResponseCode);
            show_message("Server Error", err_buf);
            delay(2000);
        }
        http.end();
    } else {
        show_message("WiFi Lost", "Reconnecting...");
    }

    face_state = FACE_SLEEP;
}

void loop() {
    if (digitalRead(TOUCH_SENSOR_PIN) == HIGH) {
        delay(50);
        if (digitalRead(TOUCH_SENSOR_PIN) == HIGH) {
            record_and_send();
        }
    }
    delay(50);
}
