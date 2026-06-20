#include <inttypes.h>
#include <stdio.h>

#include "lv_conf.h"

#include "driver/gpio.h"
#include "esp_err.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_lcd_st7796.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "ui.h"
#include "ui_ipc.h"
#include "ui_helpers.h"
#include "global_state.h"
#include "system.h"
#include "macros.h"
#include "button.h"

#include "nvs_config.h"
#include "displayDriver.h"

#pragma GCC diagnostic ignored "-Wmissing-field-initializers"

static const char *TAG = "TDisplayS3";

#ifdef NERDQX
#define SPLASH1_TIMEOUT_MS 3000
#define SPLASH2_TIMEOUT_MS 5000
#elif defined(Q1370) || defined(Q1373)
#define SPLASH1_TIMEOUT_MS 3000
#define SPLASH2_TIMEOUT_MS 5000
#else
#define SPLASH1_TIMEOUT_MS 3000
#define SPLASH2_TIMEOUT_MS 3000
#endif

// small helpers
static inline int64_t now_us() { return esp_timer_get_time(); }
static inline int32_t elapsed_ms(int64_t start_us, int64_t now) {
    return static_cast<int32_t>((now - start_us) / 1000);
}

static void formatHashrate(char *buf, int len, float hashrate) {
    if (hashrate >= 10000.0) {
        snprintf(buf, len, "%d", (int) (hashrate + 0.5f));
    } else {
        snprintf(buf, len, "%.1f", hashrate);
    }
}

DisplayDriver::DisplayDriver() {
    m_animationsEnabled = false;
    m_lastKeypressTime = 0;
    m_displayIsOn = false;
    m_countdownActive = false;
    m_countdownStartTime = 0;
    m_btcPrice = 0;
    m_blockHeight = 0;
    m_isActiveOverlay = false;
    m_lvglMutex = PTHREAD_MUTEX_INITIALIZER;
    m_isAutoScreenOffEnabled = false;
    m_tempControlMode = 0;
    m_fanSpeed = 0;
    m_shutdownCountdownActive = false;
    m_shutdownStartTime = 0;
    m_shutdownLabel = nullptr;
    m_buttonIgnoreUntil_us = 0;
}

void DisplayDriver::loadSettings() {
    PThreadGuard lock(m_lvglMutex);
    m_isAutoScreenOffEnabled = Config::isAutoScreenOffEnabled();
    m_tempControlMode = Config::getTempControlMode();
    m_fanSpeed = Config::getFanSpeed();
    m_showFoundBlockEnabled = Config::isShowBlockFoundEnabled();

    // when setting was changed, turn on the display LED
    if (!m_isAutoScreenOffEnabled) {
        displayTurnOn();
    }
}

bool DisplayDriver::notifyLvglFlushReady(esp_lcd_panel_io_handle_t panelIo, esp_lcd_panel_io_event_data_t* edata,
                                   void* userCtx) {
    lv_disp_drv_t* dispDriver = (lv_disp_drv_t*)userCtx;
    lv_disp_flush_ready(dispDriver);
    return false;
}

void DisplayDriver::lvglFlushCallback(lv_disp_drv_t* drv, const lv_area_t* area, lv_color_t* colorMap) {
    esp_lcd_panel_handle_t panelHandle = (esp_lcd_panel_handle_t)drv->user_data;
    int offsetx1 = area->x1;
    int offsetx2 = area->x2;
    int offsety1 = area->y1;
    int offsety2 = area->y2;

    // Copy buffer content to the display
    esp_lcd_panel_draw_bitmap(panelHandle, offsetx1, offsety1, offsetx2 + 1, offsety2 + 1, colorMap);
}

/************ DISPLAY TURN ON/OFF FUNCTIONS *************/
bool DisplayDriver::displayTurnOff(void) {
    if (!m_displayIsOn) {
        return false;
    }
    gpio_set_level(TDISPLAYS3_PIN_NUM_BK_LIGHT, TDISPLAYS3_LCD_BK_LIGHT_OFF_LEVEL);
    gpio_set_level(TDISPLAYS3_PIN_PWR, false);
    ESP_LOGI(TAG, "Screen off");
    m_displayIsOn = false;
    return true;
}

bool DisplayDriver::displayTurnOn(void) {
    if (m_displayIsOn) {
        return false;
    }
    gpio_set_level(TDISPLAYS3_PIN_PWR, true);
    gpio_set_level(TDISPLAYS3_PIN_NUM_BK_LIGHT, TDISPLAYS3_LCD_BK_LIGHT_ON_LEVEL);
    ESP_LOGI(TAG, "Screen on");
    m_displayIsOn = true;
    return true;
}

/************ AUTO TURN OFF DISPLAY FUNCTIONS *************/
void DisplayDriver::startCountdown(void) {
    m_countdownActive = true;
    m_countdownStartTime = esp_timer_get_time();

    if (m_countdownLabel == NULL) {
        lv_obj_t* currentScreen = lv_scr_act();
        lv_obj_t* blackBox = lv_obj_create(currentScreen);
        lv_obj_set_size(blackBox, 200, 100);
        lv_obj_set_style_bg_color(blackBox, lv_color_black(), LV_PART_MAIN);
        lv_obj_set_style_border_width(blackBox, 0, LV_PART_MAIN);
        lv_obj_align(blackBox, LV_ALIGN_CENTER, 0, 0);

        m_countdownLabel = lv_label_create(blackBox);
        lv_label_set_text(m_countdownLabel, "Turning screen off...");
        lv_obj_set_style_text_color(m_countdownLabel, lv_color_white(), LV_PART_MAIN);
        lv_obj_center(m_countdownLabel);
    }
}

void DisplayDriver::displayHideCountdown(void) {
    if (m_countdownLabel) {
        lv_obj_del(lv_obj_get_parent(m_countdownLabel));
        m_countdownLabel = NULL;
    }
}

void DisplayDriver::checkAutoTurnOffScreen(void) {
    if (!m_displayIsOn)
        return;

    int64_t currentTime = esp_timer_get_time();

    if ((currentTime - m_lastKeypressTime) > 30000000) {  // 30 seconds timeout
        if (!m_countdownActive) {
            startCountdown();
        }

        int64_t elapsedTime = (currentTime - m_countdownStartTime) / 1000000;  // Convert to seconds

        if (elapsedTime > 5) {
            displayHideCountdown();
            displayTurnOff();
            m_countdownActive = false;
        }

    } else {
        if (m_countdownActive) {
            displayHideCountdown();
            m_countdownActive = false;
        }
    }
}

void DisplayDriver::startShutdownCountdown() {
    if (m_shutdownCountdownActive) return;

    m_shutdownCountdownActive = true;
    m_isActiveOverlay = true;
    m_shutdownStartTime = esp_timer_get_time();

    lv_obj_t* scr = lv_scr_act();
    lv_obj_t* box = lv_obj_create(scr);
    lv_obj_set_size(box, 200, 100);
    lv_obj_set_style_bg_color(box, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_border_width(box, 0, LV_PART_MAIN);
    lv_obj_align(box, LV_ALIGN_CENTER, 0, 0);

    m_shutdownLabel = lv_label_create(box);
    lv_label_set_text(m_shutdownLabel, "Shutdown in 5");
    lv_obj_set_style_text_color(m_shutdownLabel, lv_color_white(), LV_PART_MAIN);
    lv_obj_center(m_shutdownLabel);
}

void DisplayDriver::updateShutdownCountdown() {
    if (!m_shutdownCountdownActive) return;

    int elapsed = (esp_timer_get_time() - m_shutdownStartTime) / 1000000;
    int remaining = 5 - elapsed;
    if (remaining < 0) remaining = 0;

    if (m_shutdownLabel) {
        char buf[32];
        snprintf(buf, sizeof(buf), "Shutdown in %d", remaining);
        lv_label_set_text(m_shutdownLabel, buf);
    }

    // After 5s → trigger shutdown
    if (elapsed >= 5) {
        hideShutdownCountdown();
        enterState(UiState::PowerOff, esp_timer_get_time());
    }
}

void DisplayDriver::hideShutdownCountdown() {
    if (m_shutdownLabel) {
        lv_obj_del(lv_obj_get_parent(m_shutdownLabel));
        m_shutdownLabel = nullptr;
    }
    m_shutdownCountdownActive = false;
    m_isActiveOverlay = false;
}


void DisplayDriver::increaseLvglTick() {
    lv_tick_inc(TDISPLAYS3_LVGL_TICK_PERIOD_MS);
}

// Refresh screen values
void DisplayDriver::refreshScreen(void) {
    // NOP
}

void DisplayDriver::showError(const char *error_message, uint32_t error_code) {
    PThreadGuard lock(m_lvglMutex);
    // hide the overlay and free the memory in case it was open
    m_ui->hideErrorOverlay();

    // now show the (new) error overlay
    m_ui->showErrorOverlay(error_message, error_code);
    m_isActiveOverlay = true;
    refreshScreen();
}

void DisplayDriver::hideError() {
    PThreadGuard lock(m_lvglMutex);
    // hide the overlay and free the memory
    m_ui->hideErrorOverlay();
    m_isActiveOverlay = false;
}

void DisplayDriver::showFoundBlockOverlay() {
    PThreadGuard lock(m_lvglMutex);
    // hide the overlay and free the memory in case it was open
    m_ui->hideImageOverlay();

    // not enabled?
    if (!m_showFoundBlockEnabled) {
        return;
    }

    // Dagon block-found climax: a block is a once-in-forever event, so just switch
    // the screen to the bright "flared sigil" image and leave it up as a trophy
    // (static cover, no animation -> no ongoing compute). Cleared on reboot.
    LV_IMG_DECLARE(ui_img_sigil_climax);
    LV_FONT_DECLARE(ui_font_OpenSansBold45);
    lv_obj_t *climax = lv_img_create(lv_scr_act());
    lv_img_set_src(climax, &ui_img_sigil_climax);
    lv_obj_set_align(climax, LV_ALIGN_TOP_LEFT);
    lv_obj_set_pos(climax, 0, 0);

    // large "SIGIL FOUND" title centered over the flare (dark text + light shadow
    // so it reads against the bright center)
    lv_obj_t *shadow = lv_label_create(climax);
    lv_label_set_text(shadow, "SIGIL FOUND");
    lv_obj_set_style_text_font(shadow, &ui_font_OpenSansBold45, LV_PART_MAIN);
    lv_obj_set_style_text_color(shadow, lv_color_hex(0xFFE6B0), LV_PART_MAIN);
    lv_obj_align(shadow, LV_ALIGN_CENTER, 3, 3);

    lv_obj_t *title = lv_label_create(climax);
    lv_label_set_text(title, "SIGIL FOUND");
    lv_obj_set_style_text_font(title, &ui_font_OpenSansBold45, LV_PART_MAIN);
    lv_obj_set_style_text_color(title, lv_color_hex(0x1A0604), LV_PART_MAIN);
    lv_obj_align(title, LV_ALIGN_CENTER, 0, 0);

    refreshScreen();
}

void DisplayDriver::hideFoundBlockOverlay() {
    // hide the overlay and free the memory
    m_ui->hideImageOverlay();
    m_isActiveOverlay = false;
}

void DisplayDriver::lvglTimerTaskWrapper(void *param) {
    DisplayDriver *display = (DisplayDriver*) param;
    display->lvglTimerTask(NULL);
}

void DisplayDriver::safe_screen_change(lv_obj_t * new_scr, lv_scr_load_anim_t anim_type, uint32_t speed, uint32_t delay)
{
    m_screenAnimationRunning = true;
    _ui_screen_change(new_scr, anim_type, speed, delay);

}

bool DisplayDriver::enterState(UiState s, int64_t now)
{
    // we already are in this state
    if (m_state == s) {
        return true;
    }
    UiState previousState = m_state;

    m_state = s;
    m_stateStart_us = now;

    switch (m_state) {
    case UiState::NOP:
        // NOP
        break;
    case UiState::Splash1:
        ESP_LOGI(TAG, "enter state splash1");
        enableLvglAnimations(true);
        break;

    case UiState::Splash2:
        ESP_LOGI(TAG, "enter state splash2");
        enableLvglAnimations(true);
        // Splash1 and Splash2 share the same Dagon background image; fading between
        // them just blinks the identical picture. Switch instantly so the only visible
        // change is the "SUMMONING..." label appearing.
        safe_screen_change(m_ui->ui_Splash2, LV_SCR_LOAD_ANIM_NONE, 0, 0);
        if (m_ui->ui_Splash1) { lv_obj_clean(m_ui->ui_Splash1); m_ui->ui_Splash1 = NULL; }
        break;

    case UiState::Wait:
        ESP_LOGI(TAG, "enter state wait");
        // Keep Splash2 visible — system task decides next screen (Portal or Mining).
        // Cleaning Splash2 here would leave an empty (white) screen with no replacement.
        break;

    case UiState::Portal:
        ESP_LOGI(TAG, "enter state portal");
        if (m_ui->ui_Splash2) { lv_obj_clean(m_ui->ui_Splash2); m_ui->ui_Splash2 = NULL; }
        enableLvglAnimations(true);
        safe_screen_change(m_ui->ui_PortalScreen, LV_SCR_LOAD_ANIM_FADE_ON, 500, 0);
        break;

    case UiState::Mining:
        ESP_LOGI(TAG, "enter state mining");
        if (m_ui->ui_Splash2) { lv_obj_clean(m_ui->ui_Splash2); m_ui->ui_Splash2 = NULL; }
        enableLvglAnimations(true);
        if (previousState == UiState::Splash2 || previousState == UiState::Wait) {
            // boot: bring the sigil background up solid (no fade), then fade the stats in
            safe_screen_change(m_ui->ui_MiningScreen, LV_SCR_LOAD_ANIM_NONE, 0, 0);
            fadeInMiningStats();
        } else if (previousState == UiState::GlobalStats) {
            safe_screen_change(m_ui->ui_MiningScreen, LV_SCR_LOAD_ANIM_MOVE_LEFT, 350, 0);
        } else {
            safe_screen_change(m_ui->ui_MiningScreen, LV_SCR_LOAD_ANIM_FADE_ON, 500, 0);
        }
        break;

    case UiState::SettingsScreen:
        ESP_LOGI(TAG, "enter state settings screen");
        enableLvglAnimations(true);
        safe_screen_change(m_ui->ui_SettingsScreen, LV_SCR_LOAD_ANIM_MOVE_LEFT, 350, 0);
        break;

    case UiState::BTCScreen:
        ESP_LOGI(TAG, "enter state btc screen");
        enableLvglAnimations(true);
        safe_screen_change(m_ui->ui_BTCScreen, LV_SCR_LOAD_ANIM_MOVE_LEFT, 350, 0);
        break;

    case UiState::GlobalStats:
        ESP_LOGI(TAG, "enter state global stats");
        enableLvglAnimations(true);
        safe_screen_change(m_ui->ui_GlobalStats, LV_SCR_LOAD_ANIM_MOVE_LEFT, 350, 0);
        break;
    case UiState::ShowQR:
        ESP_LOGI(TAG, "enter qr state");
        enableLvglAnimations(true);
        safe_screen_change(m_ui->ui_qrScreen, LV_SCR_LOAD_ANIM_FADE_ON, 500, 0);
        break;
    case UiState::Identify:
        ESP_LOGI(TAG, "enter state identify (blink %lums)", m_identifyDuration_ms);
        break;
    case UiState::PowerOff:
        if (!m_ui->ui_PowerOffScreen) {
            m_ui->powerOffScreenInit();
        }
        enableLvglAnimations(false);
        safe_screen_change(m_ui->ui_PowerOffScreen, LV_SCR_LOAD_ANIM_NONE, 0, 0);
        POWER_MANAGEMENT_MODULE.shutdown();
        break;
    }
    return true;
}


void DisplayDriver::updateState(int64_t now, bool btn1Press, bool btn2Press, bool btnBothLongPress)
{
    const int ms = elapsed_ms(m_stateStart_us, now);

    if (btnBothLongPress) {
        enterState(UiState::PowerOff, now);
        return;
    }

    switch (m_state) {
    case UiState::NOP:
        // NOP
        break;
    case UiState::Splash1:
        if (ms >= SPLASH1_TIMEOUT_MS) {
            enterState(UiState::Splash2, now);
        }
        break;

    case UiState::Splash2:
        if (ms >= SPLASH2_TIMEOUT_MS) {
            enterState(UiState::Wait, now);
        }
        break;

    case UiState::Wait:
        // NOP
        break;

    case UiState::Portal:
        enterState(UiState::Portal, now);
        break;

    case UiState::Mining:
        if (ledControl(btn1Press, btn2Press)) {
            break;
        }
        if (btn1Press) {
            APIs_FETCHER.enableFetching();
            enterState(UiState::SettingsScreen, now);
        } else {
            enterState(UiState::Mining, now);
        }
        break;
    case UiState::SettingsScreen:
        if (ledControl(btn1Press, btn2Press)) {
            break;
        }
        if (btn1Press) {
            enterState(UiState::BTCScreen, now);
            APIs_FETCHER.enableFetching();
        }
        break;
    case UiState::BTCScreen:
        if (ledControl(btn1Press, btn2Press)) {
            break;
        }
        if (btn1Press) {
            enterState(UiState::GlobalStats, now);
        }
        break;
    case UiState::GlobalStats:
        if (ledControl(btn1Press, btn2Press)) {
            break;
        }
        if (btn1Press) {
            enterState(UiState::Mining, now);
        }
        break;
    case UiState::ShowQR:
        if (btn1Press || btn2Press) {
            // abort enrollment
            otp.disableEnrollment();
            enterState(UiState::Mining, now);
        }
        break;
    case UiState::Identify:
        if (btn1Press || btn2Press || (uint32_t) ms >= m_identifyDuration_ms) {
            m_isActiveOverlay = false;
            displayTurnOn();
            enterState(UiState::Mining, now);
        } else {
            // blink: 500ms on, 500ms off
            if ((ms / 500) % 2 == 0) {
                displayTurnOn();
            } else {
                displayTurnOff();
            }
        }
        break;
    case UiState::PowerOff:
        // NOP
        break;
    }

}



void DisplayDriver::waitForSplashs() {
    // wait until state is not Splash1 or Splash2
    while ((int) m_state < (int) UiState::Wait) {
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

bool DisplayDriver::ledControl(bool btn1, bool btn2) {
    // btn1 turns it on
    if (btn1) {
        return displayTurnOn();
    }

    // btn2 toggles the LED
    if (btn2) {
        if (!m_displayIsOn) {
            return displayTurnOn();
        }
        return displayTurnOff();
    }
    return false;
}

uint32_t DisplayDriver::handleLvglTick(int32_t &elapsed_Ani_cycles)
{
    uint32_t wait_ms = 0;

    {
        PThreadGuard lock(m_lvglMutex);
        increaseLvglTick();
        wait_ms = lv_timer_handler();
    }

    if (m_animationsEnabled) {
        const uint32_t fast_cap = 5; // ~200 FPS
        if (++elapsed_Ani_cycles > 80) {
            m_animationsEnabled = false;
            elapsed_Ani_cycles = 0;
        }
        uint32_t sleep_ms = std::min(wait_ms, fast_cap);
        vTaskDelay(pdMS_TO_TICKS(sleep_ms));
        return sleep_ms;
    }

    const uint32_t idle_cap = 50;
    uint32_t sleep_ms = (wait_ms > 0 && wait_ms < idle_cap) ? wait_ms : idle_cap;
    vTaskDelay(pdMS_TO_TICKS(sleep_ms));
    return sleep_ms;
}

void DisplayDriver::processButtons(Button &btn1, Button &btn2, int64_t tnow,
                                   bool &btn1Press, bool &btn2Press, bool &btnBothLongPress)
{
    btn1.update();
    btn2.update();

    uint32_t evt1 = btn1.getEvent();
    uint32_t evt2 = btn2.getEvent();
    bool bothPressed = (evt1 & BTN_EVENT_PRESSED) && (evt2 & BTN_EVENT_PRESSED);
    bool anyPressed = (evt1 & BTN_EVENT_PRESSED) || (evt2 & BTN_EVENT_PRESSED);

    if (anyPressed) {
        m_lastKeypressTime = tnow;
    }

    // Ignore all button events within 200ms of both released
    if (esp_timer_get_time() < m_buttonIgnoreUntil_us) {
        btn1.clearEvent();
        btn2.clearEvent();
        return;
    }

    // Hide overlay if active
    if ((evt1 & BTN_EVENT_SHORTPRESS || evt2 & BTN_EVENT_SHORTPRESS) && m_isActiveOverlay) {
        hideFoundBlockOverlay();
        btn1.clearEvent();
        btn2.clearEvent();
        return;
    }

    // --- Shutdown countdown handling ---
    if (bothPressed) {
        if (!m_shutdownCountdownActive) {
            startShutdownCountdown();
        } else {
            updateShutdownCountdown();
        }
        return;
    }

    if (m_shutdownCountdownActive && !bothPressed) {
        hideShutdownCountdown();
        m_buttonIgnoreUntil_us = esp_timer_get_time() + 200 * 1000; // 200ms ignore
        btn1.clearEvent();
        btn2.clearEvent();
        return;
    }

    // Normal button events
    if ((evt1 & BTN_EVENT_LONGPRESS) && (evt2 & BTN_EVENT_LONGPRESS)) {
        btnBothLongPress = true;
        btn1.clearEvent();
        btn2.clearEvent();
    } else {
        if (evt1 & BTN_EVENT_SHORTPRESS) {
            m_lastKeypressTime = tnow;
            btn1Press = true;
            btn1.clearEvent();
        }
        if (evt2 & BTN_EVENT_SHORTPRESS) {
            m_lastKeypressTime = tnow;
            btn2Press = true;
            btn2.clearEvent();
        }
    }
}

void DisplayDriver::handleUiQueueMessages(ui_msg_t &msg, int64_t tnow)
{
    if (xQueueReceive(g_ui_queue, &msg, 0) != pdTRUE) return;

    switch (msg.type) {
        case UI_CMD_SHOW_QR: {
            if (!otp.isEnrollmentActive()) {
                ESP_LOGE(TAG, "no otp enrollment active");
                break;
            }
            int size = 0;
            uint8_t* qrBuf = otp.getQrCode(&size);
            m_ui->createQRScreen(qrBuf, size);
            if (m_ui->ui_qrScreen)
                enterState(UiState::ShowQR, tnow);
            m_isActiveOverlay = true;
            break;
        }
        case UI_CMD_HIDE_QR:
            m_isActiveOverlay = false;
            enterState(UiState::Mining, tnow);
            break;
        case UI_CMD_IDENTIFY:
            m_identifyDuration_ms = msg.param;
            m_isActiveOverlay = true;
            enterState(UiState::Identify, tnow);
            break;
    }

    if (msg.payload) {
        free(msg.payload);
        msg.payload = nullptr;
    }
}

void DisplayDriver::handleAutoOffAndOverlays()
{
    if (m_isActiveOverlay) {
        displayTurnOn();
    } else if (m_isAutoScreenOffEnabled) {
        checkAutoTurnOffScreen();
    }
}

void DisplayDriver::lvglTimerTask(void *param)
{
    displayTurnOn();
    m_lastKeypressTime = now_us();
    enterState(UiState::Splash1, now_us());

    int32_t elapsed_Ani_cycles = 0;
    Button btn1(PIN_BUTTON_1, 5000);
    Button btn2(PIN_BUTTON_2, 5000);
    ui_msg_t msg;

    while (true) {
        const int64_t tnow = now_us();
        uint32_t wait_ms = handleLvglTick(elapsed_Ani_cycles);

        if (POWER_MANAGEMENT_MODULE.isShutdown()) {
            // switch into poweroff state
            enterState(UiState::PowerOff, tnow);
            vTaskDelay(pdMS_TO_TICKS(wait_ms));
            continue;
        }

        // --- Handle buttons ---
        bool btn1Press = false, btn2Press = false, btnBothLongPress = false;
        processButtons(btn1, btn2, tnow, btn1Press, btn2Press, btnBothLongPress);

        // animation is running, ignore buttons
        if (m_screenAnimationRunning) {
            btn1Press = false;
            btn2Press = false;
            btnBothLongPress = false;
        }

        // --- Handle queued UI messages ---
        handleUiQueueMessages(msg, tnow);

        // --- Handle auto turn-off and overlays ---
        handleAutoOffAndOverlays();

        // --- Update FSM state ---
        updateState(tnow, btn1Press, btn2Press, btnBothLongPress);
    }
}


// Función para activar las actualizaciones
void DisplayDriver::enableLvglAnimations(bool enable)
{
    m_animationsEnabled = enable;
}

void DisplayDriver::mainCreatSysteTasks(void)
{
    xTaskCreatePinnedToCore(lvglTimerTaskWrapper, "lvgl Timer", 6000, (void*) this, 4, NULL, 1); // Antes 10000
}

lv_obj_t *DisplayDriver::initTDisplayS3(void)
{
    static lv_disp_draw_buf_t disp_buf; // contains internal graphic buffer(s) called draw buffer(s)
    static lv_disp_drv_t disp_drv;      // contains callback functions
    // GPIO configuration
    ESP_LOGI(TAG, "Turn off LCD backlight");
    gpio_config_t bk_gpio_config = {.pin_bit_mask = 1ULL << TDISPLAYS3_PIN_NUM_BK_LIGHT, .mode = GPIO_MODE_OUTPUT};
    ESP_ERROR_CHECK(gpio_config(&bk_gpio_config));

    gpio_pad_select_gpio(TDISPLAYS3_PIN_NUM_BK_LIGHT);
    gpio_pad_select_gpio(TDISPLAYS3_PIN_RD);
    gpio_pad_select_gpio(TDISPLAYS3_PIN_PWR);
    // esp_rom_gpio_pad_select_gpio(TDISPLAYS3_PIN_NUM_BK_LIGHT);
    // esp_rom_gpio_pad_select_gpio(TDISPLAYS3_PIN_RD);
    // esp_rom_gpio_pad_select_gpio(TDISPLAYS3_PIN_PWR);

    gpio_set_direction(TDISPLAYS3_PIN_NUM_BK_LIGHT, GPIO_MODE_OUTPUT);
    gpio_set_direction(TDISPLAYS3_PIN_RD, GPIO_MODE_OUTPUT);
    gpio_set_direction(TDISPLAYS3_PIN_PWR, GPIO_MODE_OUTPUT);

    gpio_set_level(TDISPLAYS3_PIN_RD, true);
    gpio_set_level(TDISPLAYS3_PIN_NUM_BK_LIGHT, TDISPLAYS3_LCD_BK_LIGHT_OFF_LEVEL);

    ESP_LOGI(TAG, "Initialize Intel 8080 bus");
    esp_lcd_i80_bus_handle_t i80_bus = NULL;
    esp_lcd_i80_bus_config_t bus_config = {.dc_gpio_num = TDISPLAYS3_PIN_NUM_DC,
                                           .wr_gpio_num = TDISPLAYS3_PIN_NUM_PCLK,
                                           .clk_src = LCD_CLK_SRC_DEFAULT,
                                           .data_gpio_nums =
                                               {
                                                   TDISPLAYS3_PIN_NUM_DATA0,
                                                   TDISPLAYS3_PIN_NUM_DATA1,
                                                   TDISPLAYS3_PIN_NUM_DATA2,
                                                   TDISPLAYS3_PIN_NUM_DATA3,
                                                   TDISPLAYS3_PIN_NUM_DATA4,
                                                   TDISPLAYS3_PIN_NUM_DATA5,
                                                   TDISPLAYS3_PIN_NUM_DATA6,
                                                   TDISPLAYS3_PIN_NUM_DATA7,
                                               },
                                           .bus_width = 8,
                                           .max_transfer_bytes = LVGL_LCD_BUF_SIZE * sizeof(uint16_t),
                                           .psram_trans_align = LCD_PSRAM_TRANS_ALIGN,
                                           .sram_trans_align = LCD_SRAM_TRANS_ALIGN};
    ESP_ERROR_CHECK(esp_lcd_new_i80_bus(&bus_config, &i80_bus));
    esp_lcd_panel_io_handle_t io_handle = NULL;
    esp_lcd_panel_io_i80_config_t io_config = {
        .cs_gpio_num = TDISPLAYS3_PIN_NUM_CS,
        .pclk_hz = TDISPLAYS3_LCD_PIXEL_CLOCK_HZ,
        .trans_queue_depth = 20,
        .on_color_trans_done = notifyLvglFlushReady,
        .user_ctx = &disp_drv,
        .lcd_cmd_bits = TDISPLAYS3_LCD_CMD_BITS,
        .lcd_param_bits = TDISPLAYS3_LCD_PARAM_BITS,
        .dc_levels =
            {
                .dc_idle_level = 0,
                .dc_cmd_level = 0,
                .dc_dummy_level = 0,
                .dc_data_level = 1,
            }
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_i80(i80_bus, &io_config, &io_handle));

    ESP_LOGI(TAG, "Install LCD driver of st7796 (480x320 big-screen variant)");
    esp_lcd_panel_handle_t panel_handle = NULL;

    esp_lcd_panel_dev_config_t panel_config = {
        .reset_gpio_num = TDISPLAYS3_PIN_NUM_RST,
        // big-screen ST7796 is wired BGR (red/blue swapped vs the stock panel)
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_BGR,
        .bits_per_pixel = 16,
    };

    ESP_ERROR_CHECK(esp_lcd_new_panel_st7796(io_handle, &panel_config, &panel_handle));

    esp_lcd_panel_reset(panel_handle);
    esp_lcd_panel_init(panel_handle);
    esp_lcd_panel_invert_color(panel_handle, true);

    esp_lcd_panel_swap_xy(panel_handle, true);

    Board *board = SYSTEM_MODULE.getBoard();
    // big-screen variant: the panel's mirror axes differ from the stock 1.9" panel.
    // stock showed 180-rotated + h-mirrored (== a single vertical flip); toggling the
    // mirror_x axis (screen-vertical after swap_xy) corrects it. flipscreen still = 180.
    if (!board->isFlipScreenEnabled()) {
        esp_lcd_panel_mirror(panel_handle, false, false);
    } else {
        esp_lcd_panel_mirror(panel_handle, true, true);
    }

    // the gap is LCD panel specific, even panels with the same driver IC, can have different gap value
    // stock 1.9" panel uses a 35px column offset (170 visible cols centered in the 240-col ST7789
    // controller). the big-screen variant exposes the full 240 cols, so the offset is 0.
    esp_lcd_panel_set_gap(panel_handle, 0, 0);

    ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(panel_handle, true));

    ESP_LOGI(TAG, "Turn on LCD backlight");
    gpio_set_level(TDISPLAYS3_PIN_PWR, true);
    gpio_set_level(TDISPLAYS3_PIN_NUM_BK_LIGHT, TDISPLAYS3_LCD_BK_LIGHT_ON_LEVEL);

    ESP_LOGI(TAG, "Initialize LVGL library");
    lv_init();
    // alloc draw buffers used by LVGL
    // it's recommended to choose the size of the draw buffer(s) to be at least 1/10 screen sized
    lv_color_t *buf1 = (lv_color_t*) MALLOC_DMA(LVGL_LCD_BUF_SIZE * sizeof(lv_color_t));
    assert(buf1);
    // initialize LVGL draw buffers
    lv_disp_draw_buf_init(&disp_buf, buf1, NULL, LVGL_LCD_BUF_SIZE);

    ESP_LOGI(TAG, "Register display driver to LVGL");
    lv_disp_drv_init(&disp_drv);
    disp_drv.hor_res = TDISPLAYS3_LCD_H_RES;
    disp_drv.ver_res = TDISPLAYS3_LCD_V_RES;
    disp_drv.flush_cb = lvglFlushCallback;
    disp_drv.draw_buf = &disp_buf;
    disp_drv.user_data = panel_handle;
    lv_disp_t *disp = lv_disp_drv_register(&disp_drv);

    // Configuration is completed.

    ESP_LOGI(TAG, "Install LVGL tick timer");
    // Tick interface for LVGL (using esp_timer to generate 2ms periodic event)
    /*const esp_timer_create_args_t lvgl_tick_timer_args = {
        .callback = &example_increaseLvglTick,
        .name = "lvgl_tick"
    };*/
    esp_timer_handle_t lvgl_tick_timer = NULL;
    // ESP_ERROR_CHECK(esp_timer_create(&lvgl_tick_timer_args, &lvgl_tick_timer));
    // ESP_ERROR_CHECK(esp_timer_start_periodic(lvgl_tick_timer, TDISPLAYS3_LVGL_TICK_PERIOD_MS * 1000));

    ESP_LOGI(TAG, "Display LVGL animation");
    lv_obj_t *scr = lv_disp_get_scr_act(disp);

    return scr;
}

void DisplayDriver::updateHashrate(System *module, StratumManager* manager, float power, int pool)
{
    char strData[20];
    char strDataActive[20];

    float hr = SYSTEM_MODULE.getCurrentHashrate() / 1000.0f;
    float efficiency = (hr > 0) ? power / hr : 10000.0f;
    float hashrate = SYSTEM_MODULE.getCurrentHashrate();
    formatHashrate(strData, sizeof(strData), hashrate);

    // Dagon sigil screen: show the 60-second (1-minute) average as "X.XX TH/s"
    float hr1m = SYSTEM_MODULE.getCurrentHashrate1m() / 1000.0f;
    char thStr[24];
    snprintf(thStr, sizeof(thStr), "%.2f TH/s", hr1m);
    lv_label_set_text(m_ui->ui_lbHashrate, thStr);    // Update hashrate (60s avg, TH/s)

    // let it toggle on the pool view page
    if (manager && manager->isDualPool()) {
        auto *m = static_cast<StratumManagerDualPool*>(manager);
        float activeHashrate = m->getActivePoolHashrate(pool);
        formatHashrate(strDataActive, sizeof(strDataActive), activeHashrate);
        lv_label_set_text(m_ui->ui_lbHashrateSet, strDataActive); // Update hashrate
    }

    if (manager && manager->isFallback()) {
        lv_label_set_text(m_ui->ui_lbHashrateSet, strData); // Update hashrate
    }

    lv_label_set_text(m_ui->ui_lblHashPrice, strData);  // Update hashrate

    snprintf(strData, sizeof(strData), "%.1f", efficiency);
    lv_label_set_text(m_ui->ui_lbEficiency, (efficiency < 10000.0f) ? strData : "n/a"); // Update eficiency label

    snprintf(strData, sizeof(strData), "%.3fW", power);
    lv_label_set_text(m_ui->ui_lbPower, strData); // Actualiza el label
}

void DisplayDriver::updateShares(StratumManager *manager, int pool)
{
    if (!manager) return;

    char strData[20];

    if (manager->isDualPool()) {
        auto *manager = static_cast<StratumManagerDualPool*>(STRATUM_MANAGER);
        snprintf(strData, sizeof(strData), "%lld/%lld", manager->getSharesAccepted(pool), manager->getSharesRejected(pool));
        lv_label_set_text(m_ui->ui_lbShares, strData); // Update shares
    }

    if (manager->isFallback()) {
        auto *manager = static_cast<StratumManagerFallback*>(STRATUM_MANAGER);
        snprintf(strData, sizeof(strData), "%lld/%lld", manager->getSharesAccepted(), manager->getSharesRejected());
        lv_label_set_text(m_ui->ui_lbShares, strData); // Update shares
    }

    lv_label_set_text(m_ui->ui_lbBestDifficulty, manager->getBestDiffString());    // Update Bestdifficulty
    lv_label_set_text(m_ui->ui_lbBestDifficultySet, manager->getBestDiffString()); // Update Bestdifficulty
}
void DisplayDriver::fadeInMiningStats()
{
    // On boot the sigil background + animation come up solid; fade in just the text
    // labels (stat values and captions) so the data "appears" a beat later. The
    // background image and the lava animimg are not labels, so they stay solid.
    if (!m_ui->ui_MiningScreen) return;
    uint32_t cnt = lv_obj_get_child_cnt(m_ui->ui_MiningScreen);
    for (uint32_t i = 0; i < cnt; i++) {
        lv_obj_t *c = lv_obj_get_child(m_ui->ui_MiningScreen, i);
        if (lv_obj_check_type(c, &lv_label_class)) {
            lv_obj_fade_in(c, 600, 250); // 600ms fade, 250ms after the bg is up
        }
    }
}

void DisplayDriver::updateTime(System *module)
{
    int64_t now_us = esp_timer_get_time();

    // ui_lbTime is repurposed on the Dagon sigil screen to show TOTAL MINED time:
    // a cumulative seconds counter persisted to NVS so it survives reboots.
    if (!m_runtimeLoaded) {
        m_totalRuntimeBase = Config::cfgGetU64(NVS_CONFIG_TOTAL_RUNTIME, 0ULL);
        // One-shot seed: the pre-fix persistence bug never accumulated the device's
        // real prior runtime, so the saved base is stale. Seed it once to the known
        // actual runtime (5 days 13 hours); the flag prevents this ever re-running.
        if (!Config::cfgGetBool(NVS_CONFIG_RUNTIME_SEEDED, false)) {
            m_totalRuntimeBase = 478800ULL; // 5d 13h
            Config::cfgSetU64(NVS_CONFIG_TOTAL_RUNTIME, m_totalRuntimeBase);
            Config::cfgSetBool(NVS_CONFIG_RUNTIME_SEEDED, true);
            Config::flush();
        }
        m_lastRuntimePersist_us = now_us;
        m_runtimeLoaded = true;
    }
    uint64_t session_s = (uint64_t)((now_us - module->getStartTime()) / 1000000);
    uint64_t total_s = m_totalRuntimeBase + session_s;

    // checkpoint to NVS every 5 minutes (wear-friendly)
    if (now_us - m_lastRuntimePersist_us > 300LL * 1000000LL) {
        Config::cfgSetU64(NVS_CONFIG_TOTAL_RUNTIME, total_s);
        Config::flush(); // cfgSetU64 only marks dirty; flush signals the persist task
        m_lastRuntimePersist_us = now_us;
    }

    uint64_t years = total_s / (365ULL * 24 * 3600);
    uint64_t rem   = total_s % (365ULL * 24 * 3600);
    uint64_t days  = rem / (24 * 3600);
    uint64_t hours = (rem % (24 * 3600)) / 3600;
    char mined[40];
    snprintf(mined, sizeof(mined), "%llu years, %llu days, %llu hours",
             (unsigned long long)years, (unsigned long long)days, (unsigned long long)hours);
    lv_label_set_text(m_ui->ui_lbTime, mined);
}

void DisplayDriver::updateCurrentSettings(int pool)
{
    PThreadGuard lock(m_lvglMutex);
    char strData[20];
    if (m_ui->ui_SettingsScreen == NULL || !STRATUM_MANAGER)
        return;

    Board *board = SYSTEM_MODULE.getBoard();

    if (STRATUM_MANAGER->isDualPool()) {
        auto *manager = static_cast<StratumManagerDualPool*>(STRATUM_MANAGER);
        snprintf(strData, sizeof(strData), "%s", manager->getPoolHost(pool));
        lv_label_set_text(m_ui->ui_lbPoolSet, strData); // Update label
        snprintf(strData, sizeof(strData), "%d", manager->getPoolPort(pool));
        lv_label_set_text(m_ui->ui_lbPortSet, strData); // Update label
        snprintf(strData, sizeof(strData), "%d", pool + 1);
        lv_label_set_text(m_ui->ui_lbPoolNr, strData);
    }

    if (STRATUM_MANAGER->isFallback()) {
        auto *manager = static_cast<StratumManagerFallback*>(STRATUM_MANAGER);
        lv_label_set_text(m_ui->ui_lbPoolSet, manager->getCurrentPoolHost()); // Update label
        snprintf(strData, sizeof(strData), "%d", manager->getCurrentPoolPort());
        lv_label_set_text(m_ui->ui_lbPortSet, strData); // Update label
    }

    snprintf(strData, sizeof(strData), "%d", board->getAsicFrequency());
    lv_label_set_text(m_ui->ui_lbFreqSet, strData); // Update label

    snprintf(strData, sizeof(strData), "%d", board->getAsicVoltageMillis());
    lv_label_set_text(m_ui->ui_lbVcoreSet, strData); // Update label

    switch (m_tempControlMode) {
        case 1:
            lv_label_set_text(m_ui->ui_lbFanSet, "AUTO"); // Update label
            break;
        case 2:
            lv_label_set_text(m_ui->ui_lbFanSet, "PID"); // Update label
            break;
        default:
            snprintf(strData, sizeof(strData), "%d", m_fanSpeed);
            lv_label_set_text(m_ui->ui_lbFanSet, strData); // Update label
            break;
    }
}


void DisplayDriver::updateBTCprice(void)
{
    char price_str[32];

    if ((m_state != UiState::BTCScreen) && (m_btcPrice != 0))
        return;

    m_btcPrice = APIs_FETCHER.getPrice();
    snprintf(price_str, sizeof(price_str), "%u$", m_btcPrice);
    lv_label_set_text(m_ui->ui_lblBTCPrice, price_str); // Update label
}

void DisplayDriver::updateGlobalMiningStats(void)
{
    char strData[32];

    if ((m_state != UiState::GlobalStats) && (m_blockHeight != 0))
        return;


    m_blockHeight = APIs_FETCHER.getBlockHeight();
    snprintf(strData, sizeof(strData), "%lu", m_blockHeight);
    lv_label_set_text(m_ui->ui_lblBlock, strData); // Update label

    snprintf(strData, sizeof(strData), "%lu", APIs_FETCHER.getBlocksToHalving());
    lv_label_set_text(m_ui->ui_lblBlocksToHalving, strData); // Update label

    snprintf(strData, sizeof(strData), "%lu%%", APIs_FETCHER.getHalvingPercent());
    lv_label_set_text(m_ui->ui_lblHalvingPercent, strData); // Update label

    snprintf(strData, sizeof(strData), "%llu", APIs_FETCHER.getNetHash());
    lv_label_set_text(m_ui->ui_lblGlobalHash, strData); // Update label

    snprintf(strData, sizeof(strData), "%lluT", APIs_FETCHER.getNetDifficulty());
    lv_label_set_text(m_ui->ui_lblDifficulty, strData); // Update label

    snprintf(strData, sizeof(strData), "%lu", APIs_FETCHER.getLowestFee());
    lv_label_set_text(m_ui->ui_lbllowFee, strData); // Update label

    snprintf(strData, sizeof(strData), "%lu", APIs_FETCHER.getMidFee());
    lv_label_set_text(m_ui->ui_lblmedFee, strData); // Update label

    snprintf(strData, sizeof(strData), "%lu", APIs_FETCHER.getFastestFee());
    lv_label_set_text(m_ui->ui_lblhighFee, strData); // Update label
}

void DisplayDriver::updateGlobalState(int pool)
{
    PThreadGuard lock(m_lvglMutex);
    char strData[20];

    if (m_ui->ui_MiningScreen == NULL)
        return;
    if (m_ui->ui_SettingsScreen == NULL)
        return;

    // snprintf(strData, sizeof(strData), "%.0f", power_management->chip_temp);
    snprintf(strData, sizeof(strData), "%.0f", POWER_MANAGEMENT_MODULE.getChipTempMax());
    lv_label_set_text(m_ui->ui_lbTemp, strData);       // Update label
    lv_label_set_text(m_ui->ui_lblTempPrice, strData); // Update label

    snprintf(strData, sizeof(strData), "%d", POWER_MANAGEMENT_MODULE.getFanRPM(0));
    lv_label_set_text(m_ui->ui_lbRPM, strData); // Update label

    snprintf(strData, sizeof(strData), "%.3fW", POWER_MANAGEMENT_MODULE.getPower());
    lv_label_set_text(m_ui->ui_lbPower, strData); // Update label

    snprintf(strData, sizeof(strData), "%imA", (int) POWER_MANAGEMENT_MODULE.getCurrent());
    lv_label_set_text(m_ui->ui_lbIntensidad, strData); // Update label

    snprintf(strData, sizeof(strData), "%imV", (int) POWER_MANAGEMENT_MODULE.getVoltage());
    lv_label_set_text(m_ui->ui_lbVinput, strData); // Update label

    updateTime(&SYSTEM_MODULE);
    updateShares(STRATUM_MANAGER, pool);
    updateHashrate(&SYSTEM_MODULE, STRATUM_MANAGER, POWER_MANAGEMENT_MODULE.getPower(), pool);
    updateBTCprice();
    updateGlobalMiningStats();

    Board *board = SYSTEM_MODULE.getBoard();
    uint16_t vcore = (int) (board->getVout() * 1000.0f);
    snprintf(strData, sizeof(strData), "%umV", vcore);
    lv_label_set_text(m_ui->ui_lbVcore, strData); // Update label

    // Dagon sigil side groups (plain digital readouts)
    if (m_ui->ui_lbSigPower) {
        char s[24];
        snprintf(s, sizeof(s), "POWER  %.0f W", POWER_MANAGEMENT_MODULE.getPower());
        lv_label_set_text(m_ui->ui_lbSigPower, s);
        snprintf(s, sizeof(s), "FREQ  %d MHz", board->getAsicFrequency());
        lv_label_set_text(m_ui->ui_lbSigFreq, s);
        snprintf(s, sizeof(s), "VCORE  %u mV", vcore);
        lv_label_set_text(m_ui->ui_lbSigVcore, s);
        if (STRATUM_MANAGER) {
            // lifetime accepted shares (persisted to NVS), accumulated by delta so a
            // pool reconnect that resets the session counter doesn't drop shares.
            int64_t now2 = esp_timer_get_time();
            if (!m_acceptedLoaded) {
                m_lifetimeAccepted = Config::cfgGetU64(NVS_CONFIG_TOTAL_ACCEPTED, 0ULL);
                m_lastSessionAccepted = STRATUM_MANAGER->getSharesAccepted();
                m_lastAcceptPersist_us = now2;
                m_acceptedLoaded = true;
            }
            uint64_t sess = STRATUM_MANAGER->getSharesAccepted();
            uint64_t delta = (sess >= m_lastSessionAccepted) ? (sess - m_lastSessionAccepted) : sess;
            m_lifetimeAccepted += delta;
            m_lastSessionAccepted = sess;
            if (now2 - m_lastAcceptPersist_us > 300LL * 1000000LL) {
                Config::cfgSetU64(NVS_CONFIG_TOTAL_ACCEPTED, m_lifetimeAccepted);
                Config::flush(); // cfgSetU64 only marks dirty; flush signals the persist task
                m_lastAcceptPersist_us = now2;
            }

            snprintf(s, sizeof(s), "ACCEPTED  %llu", (unsigned long long) m_lifetimeAccepted);
            lv_label_set_text(m_ui->ui_lbSigAccepted, s);
            snprintf(s, sizeof(s), "REJECTED  %llu", (unsigned long long) STRATUM_MANAGER->getSharesRejected());
            lv_label_set_text(m_ui->ui_lbSigRejected, s);
            snprintf(s, sizeof(s), "BLOCKS  %u", (unsigned) STRATUM_MANAGER->getTotalFoundBlocks());
            lv_label_set_text(m_ui->ui_lbSigBlocks, s);
        }
    }
}

void DisplayDriver::updateIpAddress(const char *ip_address_str)
{
    PThreadGuard lock(m_lvglMutex);
    if (m_ui->ui_MiningScreen == NULL)
        return;
    if (m_ui->ui_SettingsScreen == NULL)
        return;

    lv_label_set_text(m_ui->ui_lbIP, ip_address_str);    // Update label
    lv_label_set_text(m_ui->ui_lbIPSet, ip_address_str); // Update label
}

void DisplayDriver::setNetworkIcon(bool eth_connected)
{
    if (m_ui->ui_imgNet == nullptr)
        return;

    if (eth_connected)
    {
        lv_img_set_src(m_ui->ui_imgNet, &ui_img_eth_png);
    }
    else
    {
        lv_img_set_src(m_ui->ui_imgNet, &ui_img_wifi_png);
    }
}

void DisplayDriver::setCanIcon()
{
    if (m_ui->ui_imgNet == nullptr)
        return;

    lv_img_set_src(m_ui->ui_imgNet, &ui_img_can_png);
}

void DisplayDriver::logMessage(const char *message)
{
    PThreadGuard lock(m_lvglMutex);
    if (m_ui->ui_LogScreen == NULL)
        m_ui->logScreenInit();
    lv_label_set_text(m_ui->ui_LogLabel, message);
    enableLvglAnimations(true);
    _ui_screen_change(m_ui->ui_LogScreen, LV_SCR_LOAD_ANIM_NONE, 500, 0);
}

void DisplayDriver::miningScreen(void)
{
    PThreadGuard lock(m_lvglMutex);
    enterState(UiState::Mining, now_us());
}


void DisplayDriver::portalScreen(const char *message)
{
    PThreadGuard lock(m_lvglMutex);
    strlcpy(m_portalWifiName, message, sizeof(m_portalWifiName));
    lv_label_set_text(m_ui->ui_lbSSID, m_portalWifiName);
    enterState(UiState::Portal, now_us());
}

void DisplayDriver::updateWifiStatus(const char *message)
{
    PThreadGuard lock(m_lvglMutex);
    if (m_ui->ui_lbConnect != NULL)
        lv_label_set_text(m_ui->ui_lbConnect, message); // Actualiza el label
    refreshScreen();
}

void DisplayDriver::buttonsInit(void)
{
    gpio_pad_select_gpio(PIN_BUTTON_1);
    gpio_set_direction(PIN_BUTTON_1, GPIO_MODE_INPUT);
    gpio_set_pull_mode(PIN_BUTTON_1, GPIO_PULLUP_ONLY);

    gpio_pad_select_gpio(PIN_BUTTON_2);
    gpio_set_direction(PIN_BUTTON_2, GPIO_MODE_INPUT);
    gpio_set_pull_mode(PIN_BUTTON_2, GPIO_PULLUP_ONLY);
}

/**
 * @brief Program starts from here
 *
 */
void DisplayDriver::init(Board* board)
{
    ESP_LOGI("INFO", "Setting Up TDisplayS3 Screen");

    // Inicializa el GPIO para el botón
    buttonsInit();

    // init the ipc
    ui_ipc_init();

    lv_obj_t *scr = initTDisplayS3();

    m_ui = new UI();
    m_ui->init(board, this);
    // manual_lvgl_update();

    // startUpdateScreenTask(); //Start screen update task
    mainCreatSysteTasks();
}
/**************************  Useful Electronics  ****************END OF FILE***/
