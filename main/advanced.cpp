#include "advanced.hpp"
#include "board.h"
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_random.h"
#include "cJSON.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

extern const char advanced_html[] asm("_binary_advanced_html_start");
extern const char advanced_js[] asm("_binary_advanced_js_start");
extern const char navigation_css[] asm("_binary_navigation_css_start");

namespace p4radio {
namespace {
// POD snapshots only under this lock: no LVGL, filesystem, allocation or HTTP.
portMUX_TYPE job_lock = portMUX_INITIALIZER_UNLOCKED;
struct Job {
    bool busy = false;
    bool success = false;
    char message[192] = "Ready to capture the radio screen.";
};
Job job;

void finish(bool success, const char *message)
{
    Job result;
    result.success = success;
    std::snprintf(result.message, sizeof(result.message), "%s", message);
    portENTER_CRITICAL(&job_lock);
    job = result;
    portEXIT_CRITICAL(&job_lock);
}

// Capture into explicitly allocated PSRAM, independent of the shared skin pool.
// The caller frees allocation after writing; no LVGL object escapes the lock.
const char *capture(lv_draw_buf_t &image, void *&allocation)
{
    if (!board_display_lock(2000)) return "Display is busy. Try again.";
    const char *error = nullptr;
    lv_display_t *display = lv_display_get_default();
    lv_obj_t *screen = display ? lv_display_get_screen_active(display) : nullptr;
    if (!screen) error = "Display is unavailable.";
    else {
        lv_obj_update_layout(screen);
        const uint32_t w = lv_obj_get_width(screen);
        const uint32_t h = lv_obj_get_height(screen);
        const uint32_t stride = lv_draw_buf_width_to_stride(w, LV_COLOR_FORMAT_RGB565);
        const size_t bytes = static_cast<size_t>(stride) * h;
        allocation = heap_caps_aligned_alloc(LV_DRAW_BUF_ALIGN, bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!allocation) error = "Not enough PSRAM for a screenshot. Try again when playback is idle.";
        else if (lv_draw_buf_init(&image, w, h, LV_COLOR_FORMAT_RGB565, stride, allocation, bytes) != LV_RESULT_OK ||
                 lv_snapshot_take_to_draw_buf(screen, LV_COLOR_FORMAT_RGB565, &image) != LV_RESULT_OK)
            error = "Could not capture the display.";
    }
    board_display_unlock();
    return error;
}

void put32(uint8_t *p, uint32_t value)
{
    for (unsigned i = 0; i < 4; ++i) p[i] = static_cast<uint8_t>(value >> (8 * i));
}

// Windows BMP with RGB565 bit masks, bottom-up rows and four-byte row alignment.
bool write_bmp(FILE *file, const lv_draw_buf_t &image)
{
    const uint32_t row_bytes = image.header.w * 2;
    const uint32_t padded = (row_bytes + 3) & ~3U;
    const uint32_t pixels = padded * image.header.h;
    uint8_t header[66]{};
    header[0] = 'B'; header[1] = 'M';
    put32(header + 2, sizeof(header) + pixels);
    put32(header + 10, sizeof(header));
    put32(header + 14, 40);
    put32(header + 18, image.header.w);
    put32(header + 22, image.header.h);
    header[26] = 1;
    header[28] = 16;
    put32(header + 30, 3); // BI_BITFIELDS
    put32(header + 34, pixels);
    put32(header + 54, 0xf800);
    put32(header + 58, 0x07e0);
    put32(header + 62, 0x001f);
    if (std::fwrite(header, 1, sizeof(header), file) != sizeof(header)) return false;
    const uint8_t padding[3]{};
    for (uint32_t y = image.header.h; y > 0; --y) {
        const uint8_t *row = image.data + (y - 1) * image.header.stride;
        if (std::fwrite(row, 1, row_bytes, file) != row_bytes ||
            std::fwrite(padding, 1, padded - row_bytes, file) != padded - row_bytes) return false;
        // Let audio and network tasks run while writing a large screen.
        if (y % 16 == 0) vTaskDelay(1);
    }
    return true;
}

void screenshot_task(void *)
{
    char message[192]{};
    const esp_err_t mounted = board_sdcard_mount();
    if (mounted != ESP_OK) {
        std::snprintf(message, sizeof(message), "Cannot mount SD card (%s). Insert a FAT-formatted card and retry. The card was not formatted.", esp_err_to_name(mounted));
        finish(false, message);
        vTaskDelete(nullptr);
        return;
    }
    bool success = false;
    void *allocation = nullptr;
    lv_draw_buf_t image{};
    const char *error = capture(image, allocation);
    if (error) std::snprintf(message, sizeof(message), "%s", error);
    else {
        char directory[96], path[128]{};
        std::snprintf(directory, sizeof(directory), "%s/screens", board_sdcard_path());
        if (mkdir(directory, 0775) != 0 && errno != EEXIST)
            std::snprintf(message, sizeof(message), "Cannot create the screens folder. Check the SD card is writable and has free space.");
        else {
            // Short FAT names, exclusive creation, and no overwriting older captures.
            int fd = -1;
            for (unsigned attempt = 0; attempt < 32; ++attempt) {
                std::snprintf(path, sizeof(path), "%s/%08lx.bmp", directory, static_cast<unsigned long>(esp_random()));
                fd = open(path, O_WRONLY | O_CREAT | O_EXCL, 0664);
                if (fd >= 0 || errno != EEXIST) break;
            }
            FILE *file = fd >= 0 ? fdopen(fd, "wb") : nullptr;
            if (!file) {
                if (fd >= 0) { close(fd); unlink(path); }
                std::snprintf(message, sizeof(message), "Cannot create a screenshot file. Check SD card space and write protection.");
            } else {
                success = write_bmp(file, image);
                if (std::fclose(file) != 0) success = false;
                if (success) std::snprintf(message, sizeof(message), "Saved %lux%lu screenshot to %s.",
                    static_cast<unsigned long>(image.header.w), static_cast<unsigned long>(image.header.h), path);
                else {
                    unlink(path);
                    std::snprintf(message, sizeof(message), "Screenshot write failed. Check the SD card and its free space.");
                }
            }
        }
    }
    heap_caps_free(allocation);
    const esp_err_t unmounted = board_sdcard_unmount();
    if (unmounted != ESP_OK) {
        success = false;
        std::snprintf(message, sizeof(message), "SD card unmount failed (%s). Check the card before removing it.", esp_err_to_name(unmounted));
    }
    ESP_LOGI("screenshot", "%s (stack minimum free: %u bytes)", message,
             static_cast<unsigned>(uxTaskGetStackHighWaterMark(nullptr)));
    finish(success, message);
    vTaskDelete(nullptr);
}

esp_err_t status(httpd_req_t *request)
{
    Job snapshot;
    portENTER_CRITICAL(&job_lock);
    snapshot = job;
    portEXIT_CRITICAL(&job_lock);
    cJSON *json = cJSON_CreateObject();
    if (!json) return httpd_resp_send_err(request, HTTPD_500_INTERNAL_SERVER_ERROR, "Out of memory");
    const bool valid = cJSON_AddBoolToObject(json, "busy", snapshot.busy) &&
        cJSON_AddBoolToObject(json, "success", snapshot.success) &&
        cJSON_AddStringToObject(json, "message", snapshot.message);
    char *body = valid ? cJSON_PrintUnformatted(json) : nullptr;
    cJSON_Delete(json);
    if (!body) return httpd_resp_send_err(request, HTTPD_500_INTERNAL_SERVER_ERROR, "Out of memory");
    httpd_resp_set_type(request, "application/json");
    httpd_resp_set_hdr(request, "Cache-Control", "no-store");
    const esp_err_t result = httpd_resp_send(request, body, HTTPD_RESP_USE_STRLEN);
    cJSON_free(body);
    return result;
}

esp_err_t start(httpd_req_t *request)
{
    if (request->content_len != 0)
        return httpd_resp_send_err(request, HTTPD_400_BAD_REQUEST, "No request body expected");
    Job pending;
    pending.busy = true;
    std::snprintf(pending.message, sizeof(pending.message), "Capturing screen and saving to SD card. Keep the card inserted...");
    portENTER_CRITICAL(&job_lock);
    const bool busy = job.busy;
    if (!busy) job = pending;
    portEXIT_CRITICAL(&job_lock);
    if (busy) httpd_resp_set_status(request, "409 Conflict");
    // Snapshot rendering runs the skin draw callbacks here. Match the BSP's
    // 8 KB LVGL task stack; pixel storage remains in PSRAM, outside this stack.
    else if (xTaskCreate(screenshot_task, "screenshot", 8192, nullptr, 2, nullptr) != pdPASS) {
        finish(false, "Cannot start screenshot task: not enough memory.");
        httpd_resp_set_status(request, "503 Service Unavailable");
    } else httpd_resp_set_status(request, "202 Accepted");
    return status(request);
}

esp_err_t asset(httpd_req_t *request)
{
    const char *data = static_cast<const char *>(request->user_ctx);
    httpd_resp_set_type(request, data == advanced_js ? "text/javascript; charset=utf-8" :
        data == navigation_css ? "text/css; charset=utf-8" : "text/html; charset=utf-8");
    httpd_resp_set_hdr(request, "Cache-Control", "no-store");
    return httpd_resp_send(request, data, HTTPD_RESP_USE_STRLEN);
}
} // namespace

void register_advanced_http(httpd_handle_t server)
{
    const httpd_uri_t routes[] = {
        {.uri = "/advanced", .method = HTTP_GET, .handler = asset, .user_ctx = const_cast<char *>(advanced_html)},
        {.uri = "/advanced.js", .method = HTTP_GET, .handler = asset, .user_ctx = const_cast<char *>(advanced_js)},
        {.uri = "/navigation.css", .method = HTTP_GET, .handler = asset, .user_ctx = const_cast<char *>(navigation_css)},
        {.uri = "/api/screenshot", .method = HTTP_GET, .handler = status, .user_ctx = nullptr},
        {.uri = "/api/screenshot", .method = HTTP_POST, .handler = start, .user_ctx = nullptr},
    };
    for (const auto &route : routes) ESP_ERROR_CHECK(httpd_register_uri_handler(server, &route));
}
} // namespace p4radio
