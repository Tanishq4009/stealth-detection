#include <TANISHQ000000-project-1_inferencing.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include "secrets.h"
#include <HTTPClient.h>
#include "esp_camera.h"

const float         STEALTH_THRESHOLD = 0.70;
const uint8_t       HITS_REQUIRED     = 3;
const unsigned long COOLDOWN_MS       = 30000;

#define MIC_PIN 14

#define PWDN_GPIO_NUM    32
#define RESET_GPIO_NUM   -1
#define XCLK_GPIO_NUM     0
#define SIOD_GPIO_NUM    26
#define SIOC_GPIO_NUM    27

#define Y9_GPIO_NUM      35
#define Y8_GPIO_NUM      34
#define Y7_GPIO_NUM      39
#define Y6_GPIO_NUM      36
#define Y5_GPIO_NUM      21
#define Y4_GPIO_NUM      19
#define Y3_GPIO_NUM      18
#define Y2_GPIO_NUM       5

#define VSYNC_GPIO_NUM   25
#define HREF_GPIO_NUM    23
#define PCLK_GPIO_NUM    22

typedef struct {
    signed short *buffers[2];
    unsigned char buf_select;
    unsigned char buf_ready;
    unsigned int buf_count;
    unsigned int n_samples;
} inference_t;

static inference_t inference;
static bool debug_nn = false;
static int print_results = -(EI_CLASSIFIER_SLICES_PER_MODEL_WINDOW);
static volatile bool record_status = true;
TaskHandle_t micTaskHandle = NULL;

static uint8_t stealth_hits = 0;
static unsigned long last_upload_ms = 0;

static void capture_samples(void* arg);
static bool microphone_inference_start(uint32_t n_samples);
static void microphone_inference_end(void);
static bool microphone_inference_record(void);
static int microphone_audio_signal_get_data(size_t offset, size_t length, float *out_ptr);
void trigger_camera_and_upload();
void restart_listening();

void setup()
{
    Serial.begin(115200);
    while (!Serial);
    Serial.println("Starting Stealth Intrusion Detection System (AI-Thinker CAM)...");

    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);

    pinMode(PWDN_GPIO_NUM, OUTPUT);
    digitalWrite(PWDN_GPIO_NUM, HIGH);

    analogReadResolution(12);

    run_classifier_init();
    if (microphone_inference_start(EI_CLASSIFIER_SLICE_SIZE) == false) {
        ei_printf("ERR: Could not allocate audio buffer\r\n");
        return;
    }

    ei_printf("System Armed. Listening on Pin 14...\n");
}

void loop()
{
    bool m = microphone_inference_record();
    if (!m) {
        return;
    }

    signal_t signal;
    signal.total_length = EI_CLASSIFIER_SLICE_SIZE;
    signal.get_data = &microphone_audio_signal_get_data;
    ei_impulse_result_t result = {0};

    EI_IMPULSE_ERROR r = run_classifier_continuous(&signal, &result, debug_nn);
    if (r != EI_IMPULSE_OK) {
        return;
    }

    if (++print_results >= (EI_CLASSIFIER_SLICES_PER_MODEL_WINDOW)) {
        float stealth_score = 0.0f;

        for (size_t ix = 0; ix < EI_CLASSIFIER_LABEL_COUNT; ix++) {
            ei_printf("    %s: ", result.classification[ix].label);
            ei_printf_float(result.classification[ix].value);
            ei_printf("\n");

            if (strcmp(result.classification[ix].label, "stealth_movement") == 0) {
                stealth_score = result.classification[ix].value;
            }
        }
        print_results = 0;

        if (stealth_score > STEALTH_THRESHOLD) {
            stealth_hits++;
        } else {
            stealth_hits = 0;
        }

        bool cooldown_over = (last_upload_ms == 0) || (millis() - last_upload_ms > COOLDOWN_MS);

        if (stealth_hits >= HITS_REQUIRED && cooldown_over) {
            stealth_hits = 0;
            ei_printf("\n[ALERT] Stealth Movement Detected! Score: %f\n", stealth_score);

            record_status = false;
            delay(100);

            trigger_camera_and_upload();
            last_upload_ms = millis();

            ei_printf("\nResuming audio surveillance...\n");
            restart_listening();
        }
    }
}

void restart_listening() {
    inference.buf_select = 0;
    inference.buf_count  = 0;
    inference.buf_ready  = 0;
    print_results = -(EI_CLASSIFIER_SLICES_PER_MODEL_WINDOW);
    stealth_hits = 0;
    run_classifier_init();

    record_status = true;
    xTaskCreatePinnedToCore(capture_samples, "CaptureSamples", 4096, NULL, 10, &micTaskHandle, 0);
}

void trigger_camera_and_upload() {
    ei_printf("Connecting to Wi-Fi...\n");
    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

    int retries = 0;
    while (WiFi.status() != WL_CONNECTED && retries < 20) {
        delay(500);
        Serial.print(".");
        retries++;
    }
    if (WiFi.status() != WL_CONNECTED) {
        ei_printf("\nWi-Fi Failed. Aborting upload.\n");
        WiFi.disconnect(true);
        WiFi.mode(WIFI_OFF);
        return;
    }
    ei_printf("\nWi-Fi connected. IP: %s\n", WiFi.localIP().toString().c_str());
    delay(1000);

    IPAddress ip;
    if (!WiFi.hostByName("api.cloudinary.com", ip)) {
        ei_printf("DNS lookup failed. Network ya hotspot check kar.\n");
        WiFi.disconnect(true);
        WiFi.mode(WIFI_OFF);
        return;
    }
    ei_printf("DNS OK: %s\n", ip.toString().c_str());

    digitalWrite(PWDN_GPIO_NUM, LOW);
    delay(300);

    camera_config_t config = {};
    config.ledc_channel = LEDC_CHANNEL_0;
    config.ledc_timer   = LEDC_TIMER_0;
    config.pin_d0 = Y2_GPIO_NUM;
    config.pin_d1 = Y3_GPIO_NUM;
    config.pin_d2 = Y4_GPIO_NUM;
    config.pin_d3 = Y5_GPIO_NUM;
    config.pin_d4 = Y6_GPIO_NUM;
    config.pin_d5 = Y7_GPIO_NUM;
    config.pin_d6 = Y8_GPIO_NUM;
    config.pin_d7 = Y9_GPIO_NUM;
    config.pin_xclk  = XCLK_GPIO_NUM;
    config.pin_pclk  = PCLK_GPIO_NUM;
    config.pin_vsync = VSYNC_GPIO_NUM;
    config.pin_href  = HREF_GPIO_NUM;
    config.pin_sccb_sda = SIOD_GPIO_NUM;
    config.pin_sccb_scl = SIOC_GPIO_NUM;
    config.pin_pwdn  = PWDN_GPIO_NUM;
    config.pin_reset = RESET_GPIO_NUM;
    config.xclk_freq_hz = 20000000;
    config.pixel_format = PIXFORMAT_JPEG;
    config.grab_mode    = CAMERA_GRAB_LATEST;

    if (psramFound()) {
        config.frame_size   = FRAMESIZE_VGA;
        config.jpeg_quality = 12;
        config.fb_count     = 1;
        config.fb_location  = CAMERA_FB_IN_PSRAM;
    } else {
        config.frame_size   = FRAMESIZE_QVGA;
        config.jpeg_quality = 12;
        config.fb_count     = 1;
        config.fb_location  = CAMERA_FB_IN_DRAM;
    }

    uint8_t *requestBody = NULL;
    uint32_t totalLen = 0;

    String boundary = "----ESP32CamBoundary";
    String bodyPrefix = "--" + boundary + "\r\n";
    bodyPrefix += "Content-Disposition: form-data; name=\"upload_preset\"\r\n\r\n";
    bodyPrefix += String(CLOUDINARY_UPLOAD_PRESET) + "\r\n";
    bodyPrefix += "--" + boundary + "\r\n";
    bodyPrefix += "Content-Disposition: form-data; name=\"file\"; filename=\"intruder.jpg\"\r\n";
    bodyPrefix += "Content-Type: image/jpeg\r\n\r\n";
    String bodySuffix = "\r\n--" + boundary + "--\r\n";

    esp_err_t err = esp_camera_init(&config);
    if (err != ESP_OK) {
        ei_printf("Camera init failed! Error 0x%x\n", err);
    } else {
        for (int i = 0; i < 3; i++) {
            camera_fb_t *tmp = esp_camera_fb_get();
            if (tmp) esp_camera_fb_return(tmp);
            delay(100);
        }

        camera_fb_t *fb = esp_camera_fb_get();
        if (!fb) {
            ei_printf("Camera capture failed!\n");
        } else {
            ei_printf("Image captured (%u bytes).\n", (unsigned)fb->len);

            totalLen = bodyPrefix.length() + fb->len + bodySuffix.length();
            requestBody = psramFound() ? (uint8_t *)ps_malloc(totalLen)
                                       : (uint8_t *)malloc(totalLen);
            if (requestBody) {
                memcpy(requestBody, bodyPrefix.c_str(), bodyPrefix.length());
                memcpy(requestBody + bodyPrefix.length(), fb->buf, fb->len);
                memcpy(requestBody + bodyPrefix.length() + fb->len,
                       bodySuffix.c_str(), bodySuffix.length());
            } else {
                ei_printf("Memory allocation for upload failed!\n");
            }
            esp_camera_fb_return(fb);
        }

        esp_camera_deinit();
    }
    digitalWrite(PWDN_GPIO_NUM, HIGH);

    if (requestBody) {
        String imageUrl = "";
        bool uploaded = false;

        {
            ei_printf("Free heap before upload: %u bytes\n", (unsigned)ESP.getFreeHeap());
            ei_printf("Uploading to Cloudinary...\n");

            WiFiClientSecure client;
            client.setInsecure();
            client.setHandshakeTimeout(30);

            HTTPClient http;
            http.begin(client, String("https://api.cloudinary.com/v1_1/") + CLOUDINARY_CLOUD_NAME + "/image/upload");
            http.setTimeout(20000);
            http.addHeader("Content-Type", "multipart/form-data; boundary=" + boundary);

            int code = http.POST(requestBody, totalLen);
            ei_printf("Cloudinary HTTP code: %d (%s)\n", code, http.errorToString(code).c_str());

            if (code == 200) {
                String resp = http.getString();
                int s = resp.indexOf("\"secure_url\":\"");
                if (s >= 0) {
                    s += 14;
                    int e = resp.indexOf('"', s);
                    imageUrl = resp.substring(s, e);
                    imageUrl.replace("\\/", "/");
                }
                uploaded = true;
                ei_printf("Cloudinary Upload Successful! URL: %s\n", imageUrl.c_str());
            } else if (code > 0) {
                ei_printf("Server reply: %s\n", http.getString().c_str());
            }
            http.end();
        }
        free(requestBody);
        requestBody = NULL;

        if (uploaded) {
            ei_printf("Free heap before push: %u bytes\n", (unsigned)ESP.getFreeHeap());
            ei_printf("Sending Push Notification to App...\n");

            WiFiClientSecure pushClient;
            pushClient.setInsecure();
            pushClient.setHandshakeTimeout(30);
            HTTPClient pushHttp;

            String apiKey = String(ONESIGNAL_REST_API_KEY);
            bool v2 = apiKey.startsWith("os_v2");
            String url = v2 ? "https://api.onesignal.com/notifications?c=push"
                            : "https://onesignal.com/api/v1/notifications";
            pushHttp.begin(pushClient, url);
            pushHttp.setTimeout(15000);
            pushHttp.addHeader("Content-Type", "application/json");
            pushHttp.addHeader("Authorization", String(v2 ? "Key " : "Basic ") + apiKey);

            String payload = String("{\"app_id\":\"") + ONESIGNAL_APP_ID + "\",";
            if (v2) payload += "\"target_channel\":\"push\",";
            payload += "\"included_segments\":[\"Total Subscriptions\"],"
                       "\"headings\":{\"en\":\"Intrusion Detected!\"},"
                       "\"contents\":{\"en\":\"Stealth movement detected. Tap to view the capture.\"},"
                       "\"data\":{\"image_url\":\"" + imageUrl + "\"},"
                       "\"big_picture\":\"" + imageUrl + "\"}";

            int pc = pushHttp.POST(payload);
            ei_printf("OneSignal HTTP code: %d (%s)\n", pc, pushHttp.errorToString(pc).c_str());
            if (pc > 0) ei_printf("OneSignal reply: %s\n", pushHttp.getString().c_str());
            pushHttp.end();
        }
    }

    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
    ei_printf("Wi-Fi and Camera OFF. Hardware resources freed.\n");
}

static void capture_samples(void* arg) {
    const uint32_t sample_interval_us = 1000000 / EI_CLASSIFIER_FREQUENCY;

    while (record_status) {
        uint32_t start_time = esp_timer_get_time();

        int16_t sample = analogRead(MIC_PIN);

        inference.buffers[inference.buf_select][inference.buf_count++] = sample;

        if (inference.buf_count >= inference.n_samples) {
            inference.buf_select ^= 1;
            inference.buf_count = 0;
            inference.buf_ready = 1;

            vTaskDelay(pdMS_TO_TICKS(1));
        }

        while (esp_timer_get_time() - start_time < sample_interval_us) {
        }
    }
    vTaskDelete(NULL);
}

static bool microphone_inference_start(uint32_t n_samples)
{
    inference.buffers[0] = (signed short *)malloc(n_samples * sizeof(signed short));
    if (inference.buffers[0] == NULL) return false;

    inference.buffers[1] = (signed short *)malloc(n_samples * sizeof(signed short));
    if (inference.buffers[1] == NULL) {
        ei_free(inference.buffers[0]);
        return false;
    }

    inference.buf_select = 0;
    inference.buf_count  = 0;
    inference.n_samples  = n_samples;
    inference.buf_ready  = 0;
    record_status = true;

    xTaskCreatePinnedToCore(capture_samples, "CaptureSamples", 4096, NULL, 10, &micTaskHandle, 0);

    return true;
}

static bool microphone_inference_record(void)
{
    if (inference.buf_ready == 1) {
        ei_printf("Error sample buffer overrun.\n");
        return false;
    }
    while (inference.buf_ready == 0) {
        delay(1);
    }
    inference.buf_ready = 0;
    return true;
}

static int microphone_audio_signal_get_data(size_t offset, size_t length, float *out_ptr)
{
    numpy::int16_to_float(&inference.buffers[inference.buf_select ^ 1][offset], out_ptr, length);
    return 0;
}

static void microphone_inference_end(void)
{
    record_status = false;
    delay(100);
    ei_free(inference.buffers[0]);
    ei_free(inference.buffers[1]);
}