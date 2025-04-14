// Soundfiles player firmware

#define BOARD pico
#include "fraise.h"
#include "hardware/uart.h"
#include "string.h"
#include "pico/rand.h"
#include <stdlib.h>
#include "gd3300.h"
#include "hardware/pwm.h"
#include "hardware/adc.h"

#define printf fraise_printf

const uint LED_PIN = PICO_DEFAULT_LED_PIN;
int ledPeriod = 250;
bool led = false;

const uint PLAYLED_PIN = 4;
const uint BUTTON_PIN = 8;
const uint MP3_TX_PIN = 0;
const uint MP3_RX_PIN = 1;
uart_inst_t *MP3_UART = uart0;
const uint VOLUME_PIN = 26;

bool button, button_last;
int button_count;

GD3300 mp3;

float voladc = 0.0;
float fade = 1.0;
#define PWM_MAX 20000 // 6.25kHz

enum STATE {STOP, PLAYING, FADEOUT, STOPPING, ARM, NEXT} state = STOP;

void setup_pwm_led(int pin) {
    gpio_set_function(pin, GPIO_FUNC_PWM);
    uint slice_num = pwm_gpio_to_slice_num(pin);
    pwm_set_wrap(slice_num, PWM_MAX);
    pwm_set_enabled(slice_num, true);
    pwm_set_gpio_level(pin, 0);
}

void set_pwm_led(int val) {// 0-255
    int pwm = (PWM_MAX * val) / 256;
    pwm_set_gpio_level(PLAYLED_PIN, pwm);
    pwm_set_gpio_level(LED_PIN, pwm);
}

void setup() {
    adc_init();

    gpio_init(BUTTON_PIN);
    gpio_set_dir(BUTTON_PIN, GPIO_IN);
    gpio_pull_up(BUTTON_PIN);

    setup_pwm_led(PLAYLED_PIN);
    setup_pwm_led(LED_PIN);

    adc_gpio_init(VOLUME_PIN);
    adc_select_input(VOLUME_PIN - 26);

    srandom(get_rand_32());

    mp3.setup(MP3_TX_PIN, MP3_RX_PIN, MP3_UART);
}

const int LAST_PLAYED_SIZE = 20;
int last_played[LAST_PLAYED_SIZE] = {0};
int last_played_index = 0;

void play_track(uint8_t n) {
    mp3.play(n);
    last_played[last_played_index] = n;
    last_played_index  = (last_played_index + 1) % LAST_PLAYED_SIZE;
}

bool tracked_played_for_less_than(int track, int num) {
    int last = (last_played_index + LAST_PLAYED_SIZE - 1) % LAST_PLAYED_SIZE;
    for(int i = 0 ; i < num; i++) {
        if(last_played[last] == track) return true;
        last = (last + LAST_PLAYED_SIZE - 1) % LAST_PLAYED_SIZE;
    }
    return false;
}

void play_rnd_track() {
    int ntracks = mp3.get_nb_tracks();
    int nlast = MIN(ntracks / 2, LAST_PLAYED_SIZE);
    int next = 1 + (random() % ntracks);
    int tries = 0;
    while((tries++ < 100) && (tracked_played_for_less_than(next, nlast))) {
        next = 1 + (random() % ntracks);
    }
    printf("playing %d tries %d\n", next, tries);
    play_track(next);
}

void fadeout_then_play_rnd_track() {
    if(mp3.is_playing()) {
        state = FADEOUT;
    } else {
        fade = 1.0;
        play_rnd_track();
    }
}

void update_state() {
    static absolute_time_t timeout = get_absolute_time();
    if(!time_reached(timeout)) return;
    timeout = make_timeout_time_ms(100);

    if(!mp3.get_nb_tracks()) mp3.qTTracks();

    switch(state) {
        case STOP:
        case PLAYING:
            fade = 1.0;
            if(!mp3.is_playing()) state = STOP;
            break;
        case FADEOUT: {
                if((fade == 0.0) || (!mp3.is_playing())) {
                    state = STOPPING;
                    break;
                } else {
                    fade -= 0.15;
                    if(fade < 0.0) fade = 0.0;
                    break;
                }
            }
            break;
        case STOPPING:
            mp3.stop();
            state = ARM;
            break;
        case ARM:
            fade = 1.0;
            state = NEXT;
            break;
        case NEXT:
            play_rnd_track();
            state = PLAYING;
            break;
    }

    static int last_vol = -1;
    int volume = (int)(voladc * fade);
    if(volume != last_vol) {
        last_vol = volume;
        mp3.setVol(volume);
        printf("set vol %d\n", volume);
    }
}

void update_led() {
    static float val, vf1;
    static absolute_time_t timeout = get_absolute_time();
    if(!time_reached(timeout)) return;
    timeout = make_timeout_time_ms(10);
    if(led) val = 0.4 + 0.6 * (random() % 10000) / 10000.0;
    else val = vf1 = 0.0;
    vf1 = vf1 * 0.9 + (val - vf1) * 0.2;
    set_pwm_led(vf1 * 255);
}

#define CLIP(x, a, b) MIN(MAX(x, a), b)
void update_voladc() {
    static const float lopf = 0.05;
    static int adc = 0.0;
    adc = adc * (1.0 - lopf) + lopf * adc_read();

    static absolute_time_t timeout = get_absolute_time();
    if(!time_reached(timeout)) return;
    timeout = make_timeout_time_ms(50);
    float adc_norm = (adc - 700.0) / (2650.0 - 700.0);
    adc_norm = CLIP(adc_norm, 0.0, 1.0);
    voladc = adc_norm * 30.0;
    //printf("A %d %f\n", adc, voladc);
}

void loop() {
    static absolute_time_t nextLed;

    if(mp3.is_playing() && time_reached(nextLed)) {
        led = !led;
        nextLed = make_timeout_time_ms(ledPeriod);
    }
    if(!mp3.is_playing()) led = true;

    if(gpio_get(BUTTON_PIN)) {
        if(button_count > 0) button_count--;
        else button = false;
    } else {
        if(button_count < 1000) button_count++;
        else button = true;
    }
    if(button_last != button) {
        button_last = button;
        printf("b %d\n", button);
        if(button) {
            fadeout_then_play_rnd_track();
        }
    }

    update_voladc();
    mp3.update();
    update_state();
    update_led();
}

void fraise_receivebytes(const char *data, uint8_t len) {
    uint8_t command = fraise_get_uint8();
    switch(command) {
    case 1:
        ledPeriod = (int)fraise_get_uint8() * 10;
        break;
    case 10:
        mp3.receivebytes(data + 1, len - 1);
        break;
    case 20:
        play_rnd_track();
        break;
    case 21:
        fadeout_then_play_rnd_track();
        break;
    case 100 :
        fraise_print_status();
        break;
    default:
        printf("rcvd ");
        for(int i = 0; i < len; i++) printf("%d ", (uint8_t)data[i]);
        putchar('\n');
    }
}

bool string_equal(const char *in, uint8_t len, const char *str) {
    return len >= strlen(str) && !strncmp(in, str, strlen(str));
}

void fraise_receivechars(const char *data, uint8_t len) {
    if(data[0] == 'E') { // Echo
        printf("E%s\n", data + 1);
    }
}

