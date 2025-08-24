#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "esp_log.h"
#include "esp_system.h"
#include "sdkconfig.h"
#include "esp_timer.h"

// GPIO definitions
#define LEFT_BTN_GPIO     GPIO_NUM_4
#define RIGHT_BTN_GPIO    GPIO_NUM_18
#define LEFT_LED_GPIO     GPIO_NUM_2
#define RIGHT_LED_GPIO    GPIO_NUM_5

#define TAG "FSM"

// FSM states
typedef enum {
    IDLE,
    LEFT_ON,
    RIGHT_ON,
    HAZARD
} fsm_state_t;

fsm_state_t current_state = IDLE;

// Blink interval
#define BLINK_INTERVAL_MS     300
#define HOLD_TIME_MS          1000

// PWM channels
#define PWM_FREQ_HZ 1000
#define PWM_RESOLUTION LEDC_TIMER_8_BIT
#define PWM_DUTY_ON   255
#define PWM_DUTY_OFF  0
#define LEDC_SPEED_MODE LEDC_HIGH_SPEED_MODE

// Timers
uint64_t last_blink_time = 0;
bool led_on = false;

// Support functions
uint64_t now_ms() {
    return esp_timer_get_time() / 1000ULL;
}

// LED PWM setup
void setup_pwm_channel(gpio_num_t pin, ledc_channel_t channel) {
    ledc_channel_config_t ledc_channel = {
        .channel    = channel,
        .duty       = 0,
        .gpio_num   = pin,
        .speed_mode = LEDC_SPEED_MODE,
        .hpoint     = 0,
        .timer_sel  = LEDC_TIMER_0
    };
    ledc_channel_config(&ledc_channel);
}

void set_led(ledc_channel_t channel, bool on) {
    ledc_set_duty(LEDC_SPEED_MODE, channel, on ? PWM_DUTY_ON : PWM_DUTY_OFF);
    ledc_update_duty(LEDC_SPEED_MODE, channel);
}

// FSM Task (runs every 100ms)
void fsm_task(void *arg) {
    bool left_pressed_prev = false;
    bool right_pressed_prev = false;
    uint64_t left_press_start = 0;
    uint64_t right_press_start = 0;
    uint64_t both_press_start = 0;

    while (1) {
        bool left_now = gpio_get_level(LEFT_BTN_GPIO) == 0;
        bool right_now = gpio_get_level(RIGHT_BTN_GPIO) == 0;

        uint64_t now = now_ms();

        // Start timers on press
        if (left_now && !left_pressed_prev) left_press_start = now;
        if (right_now && !right_pressed_prev) right_press_start = now;
        if (left_now && right_now && (!left_pressed_prev || !right_pressed_prev)) both_press_start = now;

        // === Hazard Mode Activation ===
        if (left_now && right_now && current_state != HAZARD && now - both_press_start > HOLD_TIME_MS) {
            current_state = HAZARD;
            ESP_LOGI(TAG, ">> HAZARD MODE ON");
            led_on = true;
            last_blink_time = now;
        }

        // === Exit Hazard Mode ===
        if (current_state == HAZARD && ((left_now && !right_now) || (right_now && !left_now))) {
            if ((left_now && now - left_press_start > HOLD_TIME_MS) ||
                (right_now && now - right_press_start > HOLD_TIME_MS)) {
                current_state = IDLE;
                ESP_LOGI(TAG, ">> HAZARD MODE OFF");
                set_led(LEDC_CHANNEL_0, false);
                set_led(LEDC_CHANNEL_1, false);
            }
        }

        // === Handle Left Button ===
        if (left_now && !right_now && now - left_press_start > HOLD_TIME_MS && current_state != HAZARD) {
            if (current_state == RIGHT_ON) {
                ESP_LOGI(TAG, ">> SWITCH RIGHT TO LEFT");
                current_state = LEFT_ON;
            } else if (current_state == LEFT_ON) {
                ESP_LOGI(TAG, ">> LEFT OFF");
                current_state = IDLE;
            } else {
                ESP_LOGI(TAG, ">> LEFT ON");
                current_state = LEFT_ON;
            }
            while (gpio_get_level(LEFT_BTN_GPIO) == 0) vTaskDelay(pdMS_TO_TICKS(10));
        }

        // === Handle Right Button ===
        if (right_now && !left_now && now - right_press_start > HOLD_TIME_MS && current_state != HAZARD) {
            if (current_state == LEFT_ON) {
                ESP_LOGI(TAG, ">> SWITCH LEFT TO RIGHT");
                current_state = RIGHT_ON;
            } else if (current_state == RIGHT_ON) {
                ESP_LOGI(TAG, ">> RIGHT OFF");
                current_state = IDLE;
            } else {
                ESP_LOGI(TAG, ">> RIGHT ON");
                current_state = RIGHT_ON;
            }
            while (gpio_get_level(RIGHT_BTN_GPIO) == 0) vTaskDelay(pdMS_TO_TICKS(10));
        }

        // === Blinking LEDs ===
        if (current_state == LEFT_ON || current_state == RIGHT_ON || current_state == HAZARD) {
            if (now - last_blink_time >= BLINK_INTERVAL_MS) {
                led_on = !led_on;
                last_blink_time = now;

                if (current_state == LEFT_ON) {
                    set_led(LEDC_CHANNEL_0, led_on);
                    set_led(LEDC_CHANNEL_1, false);
                } else if (current_state == RIGHT_ON) {
                    set_led(LEDC_CHANNEL_0, false);
                    set_led(LEDC_CHANNEL_1, led_on);
                } else if (current_state == HAZARD) {
                    set_led(LEDC_CHANNEL_0, led_on);
                    set_led(LEDC_CHANNEL_1, led_on);
                }
            }
        } else {
            set_led(LEDC_CHANNEL_0, false);
            set_led(LEDC_CHANNEL_1, false);
        }

        // Save previous state
        left_pressed_prev = left_now;
        right_pressed_prev = right_now;

        vTaskDelay(pdMS_TO_TICKS(100)); // Scheduler tick every 100ms
    }
}

void app_main(void) {
    // Setup GPIOs
    gpio_config_t btn_conf = {
        .intr_type = GPIO_INTR_DISABLE,
        .mode = GPIO_MODE_INPUT,
        .pin_bit_mask = (1ULL << LEFT_BTN_GPIO) | (1ULL << RIGHT_BTN_GPIO),
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
    };
    gpio_config(&btn_conf);

    // Setup PWM timer
    ledc_timer_config_t ledc_timer = {
        .speed_mode = LEDC_SPEED_MODE,
        .timer_num = LEDC_TIMER_0,
        .freq_hz = PWM_FREQ_HZ,
        .duty_resolution = PWM_RESOLUTION,
        .clk_cfg = LEDC_AUTO_CLK
    };
    ledc_timer_config(&ledc_timer);

    // Setup PWM channels
    setup_pwm_channel(LEFT_LED_GPIO, LEDC_CHANNEL_0);
    setup_pwm_channel(RIGHT_LED_GPIO, LEDC_CHANNEL_1);

    // Start FSM Task
    xTaskCreate(fsm_task, "fsm_task", 4096, NULL, 2, NULL);
}
