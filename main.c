/* Includes ----------------------------------------------------------*/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "fmt.h"
#include "msg.h"
#include "net/loramac.h"
#include "periph/adc.h"
#include "periph/gpio.h"
#include "periph/pm.h"
#include "periph/rtc.h"
#include "scaling.h"
#include "semtech_loramac.h"
#include "thread.h"
#include "xtimer.h"

/* Private typedef ---------------------------------------------------*/

/* Private define ----------------------------------------------------*/
#define SENDER_PRIO (THREAD_PRIORITY_MAIN - 1)
#define RECV_MSG_QUEUE (4U)
#define ON_TIME 7

#define OUT0 GPIO_PIN(PORT_A, 0)
#define OUT1 GPIO_PIN(PORT_A, 1)
#define OUT2 GPIO_PIN(PORT_A, 2)
#define OUT3 GPIO_PIN(PORT_A, 3)

#ifndef DEFAULT_PERIOD_SENDING
#define DEFAULT_PERIOD_SENDING 600
#endif /* DEFAULT_PERIOD_SENDING */

#ifndef DEFAULT_RESOLUTION
#define DEFAULT_RESOLUTION 0
#endif /* DEFAULT_RESOLUTION */

#define ADC_VREF_INT 11
#define SEC_PER_HOUR 3600

/* Private macro -----------------------------------------------------*/

/* Private variables -------------------------------------------------*/
static kernel_pid_t sender_pid;
static char sender_stack[THREAD_STACKSIZE_MAIN / 2];
static char _recv_stack[THREAD_STACKSIZE_DEFAULT];
static uint8_t deveui[LORAMAC_DEVEUI_LEN];
static uint8_t appeui[LORAMAC_APPEUI_LEN];
static uint8_t appkey[LORAMAC_APPKEY_LEN];
static msg_t _recv_queue[RECV_MSG_QUEUE];
semtech_loramac_t loramac;
uint8_t message[50] = {0};
uint32_t interval_sec = DEFAULT_PERIOD_SENDING;
uint8_t resolution = DEFAULT_RESOLUTION;
struct tm received_data_time_s;

/* Private function prototypes ---------------------------------------*/
static void rtc_cb(void *arg);
static int _prepare_next_alarm(void);
static void _send_message(uint8_t length);
static void *sender(void *arg);
static void *_recv(void *arg);
float _get_vcc(void);
void _init_unused_pins(void);

/* Private functions -------------------------------------------------*/
int main(void) {
    uint8_t number_join_tries = 0;
    uint8_t loramac_datarate = 0;

    /* Init peripherie */
    _init_unused_pins();
    adc_init(ADC_LINE(ADC_VREF_INT));
    gpio_init(OUT0, GPIO_OUT);
    gpio_init(OUT1, GPIO_OUT);
    gpio_init(OUT2, GPIO_OUT);
    gpio_init(OUT3, GPIO_OUT);

    /* led test */
    gpio_clear(OUT0);
    gpio_clear(OUT1);
    gpio_clear(OUT2);
    gpio_clear(OUT3);
    xtimer_sleep(3);
    gpio_set(OUT0);
    gpio_set(OUT1);
    gpio_set(OUT2);
    gpio_set(OUT3);

    /* Convert identifiers and application key */
    fmt_hex_bytes(deveui, DEVEUI);
    fmt_hex_bytes(appeui, APPEUI);
    fmt_hex_bytes(appkey, APPKEY);

    /* Initialize the loramac stack */
    semtech_loramac_init(&loramac);
    semtech_loramac_set_deveui(&loramac, deveui);
    semtech_loramac_set_appeui(&loramac, appeui);
    semtech_loramac_set_appkey(&loramac, appkey);

    /* Use a fast datarate, e.g. BW125/SF7 in EU868 */
    semtech_loramac_set_dr(&loramac, LORAMAC_DR_5);

    /* Use ADR */
    semtech_loramac_set_adr(&loramac, true);

    /* Use unconfirmed data mode */
    semtech_loramac_set_tx_mode(&loramac, LORAMAC_TX_UNCNF);

    /* Use port 1 for uplink masseges */
    semtech_loramac_set_tx_port(&loramac, 1);

    /* Start the Over-The-Air Activation (OTAA) procedure to retrieve the
     * generated device address and to get the network and application session
     * keys.
     */
    puts("Starting join procedure");
    while (semtech_loramac_join(&loramac, LORAMAC_JOIN_OTAA) != SEMTECH_LORAMAC_JOIN_SUCCEEDED) {
        puts("Join procedure failed, try in 30s again");
        xtimer_sleep(22);

        /* increase datarate after 3 join tries */
        number_join_tries++;
        loramac_datarate = semtech_loramac_get_dr(&loramac);
        if (number_join_tries > 2 && loramac_datarate > LORAMAC_DR_0) {
            number_join_tries = 0;
            loramac_datarate--;
            semtech_loramac_set_dr(&loramac, loramac_datarate);
        }
    }
    puts("Join procedure succeeded");

    /* start the sender thread */
    sender_pid = thread_create(sender_stack, sizeof(sender_stack), SENDER_PRIO, 0, sender, NULL, "sender");

    /* start the receive thread */
    thread_create(_recv_stack, sizeof(_recv_stack), THREAD_PRIORITY_MAIN - 1, 0, _recv, NULL, "recv thread");

    /* trigger the first send */
    msg_t msg;
    msg_send(&msg, sender_pid);
    return 0;
}

static void rtc_cb(void *arg) {
    (void)arg;
    msg_t msg;
    msg_send(&msg, sender_pid);
}

static int _prepare_next_alarm(void) {
    struct tm time;
    struct tm alarm;
    int rc;
    uint8_t tries = 3;

    rtc_get_time(&time);
    time.tm_sec += (interval_sec - ON_TIME);
    mktime(&time);

    do {
        rtc_set_alarm(&time, rtc_cb, NULL);
        rtc_get_alarm(&alarm);
        rc = rtc_tm_compare(&time, &alarm);
        tries--;
    } while ((rc != 0) && (tries != 0));

    if (rc == 0) {
        puts("RTC alarm set\n");
    } else {
        puts("RTC alarm not set\n");
    }

    return rc;
}

static void _send_message(uint8_t length) {
    printf("Sending\n");
    /* Try to send the message */
    uint8_t ret = semtech_loramac_send(&loramac, message, length);
    if (ret != SEMTECH_LORAMAC_TX_DONE) {
        printf("Cannot send message, ret code: %d\n", ret);
        return;
    }
}

static void *sender(void *arg) {
    (void)arg;

    msg_t msg;
    msg_t msg_queue[8];
    msg_init_queue(msg_queue, 8);

    while (1) {
        msg_receive(&msg);

        /* turn off output after 24h */
        struct tm now_s;
        uint32_t now;
        uint32_t received_data_time;
        rtc_get_time(&now_s);
        now = rtc_mktime(&now_s);
        received_data_time = rtc_mktime(&received_data_time_s);

        if (now - received_data_time > 24*SEC_PER_HOUR) {
            gpio_set(OUT0);
            gpio_set(OUT1);
            gpio_set(OUT2);
            gpio_set(OUT3);
        }

        if (resolution == 0) {
            semtech_loramac_set_tx_port(&loramac, 1);  //TODO Port enum
            uint8_t vbat = (uint8_t)scaling_float(_get_vcc(), 2.0, 4.0, 0.0, 255.0, LIMIT_OUTPUT);
            message[0] = (uint8_t)vbat;
            _send_message(1);
        } else if (resolution == 1) {
            semtech_loramac_set_tx_port(&loramac, 2);  //TODO Port enum
            uint16_t vbat = (uint16_t)scaling_float(_get_vcc(), 2.0, 4.0, 0.0, 65535.0, LIMIT_OUTPUT);
            message[0] = (uint8_t)(vbat >> 8);
            message[1] = (uint8_t)(vbat);
            _send_message(2);
        }
        /* wait some time to give receive thread time to work */
        xtimer_sleep(3);

        /* Schedule the next wake-up alarm */
        if (_prepare_next_alarm() == 0) {
            /* going to deep sleep */
            puts("Going to sleep");
            pm_set(1);
        } else {
            /* rtc can not set, try reboot */
            pm_reboot();
        }
    }

    /* this should never be reached */
    return NULL;
}

static void *_recv(void *arg) {
    msg_init_queue(_recv_queue, RECV_MSG_QUEUE);

    (void)arg;
    uint16_t received_interval = 0;
    uint8_t received_resolution = 0;

    while (1) {
        /* blocks until some data is received */
        semtech_loramac_recv(&loramac);
        loramac.rx_data.payload[loramac.rx_data.payload_len] = 0;
        puts("Data received:");
        for (uint8_t i = 0; i < loramac.rx_data.payload_len; i++) {
            print_byte_hex(loramac.rx_data.payload[i]);
        }
        puts("\nPort:");
        print_u32_dec((uint32_t)loramac.rx_data.port);
        puts("");

        /* process received data */
        switch (loramac.rx_data.port) {
            case 1: /* resolution */
                received_resolution = loramac.rx_data.payload[0];
                if (received_resolution <= 1) {
                    resolution = received_resolution;
                }
                break;
            case 2: /* time of period sending */
                received_interval = (loramac.rx_data.payload[0] << 8) + loramac.rx_data.payload[1];
                if (received_interval >= 3) {
                    interval_sec = received_interval * 10;
                }
                break;
            case 3: /* System reboot */
                if (loramac.rx_data.payload[0] == 1) {
                    pm_reboot();
                }
                break;
            case 4: /* rubbish data */
                rtc_get_time(&received_data_time_s);
                if (loramac.rx_data.payload[0] & 1) {
                    gpio_clear(OUT0);
                } else {
                    gpio_set(OUT0);
                }
                if (loramac.rx_data.payload[0] & 2) {
                    gpio_clear(OUT1);
                } else {
                    gpio_set(OUT1);
                }
                if (loramac.rx_data.payload[0] & 4) {
                    gpio_clear(OUT2);
                } else {
                    gpio_set(OUT2);
                }
                if (loramac.rx_data.payload[0] & 8) {
                    gpio_clear(OUT3);
                } else {
                    gpio_set(OUT3);
                }
                break;
        }
    }
    return NULL;
}

float _get_vcc(void) {
    float vbat = 4957.744 / adc_sample(ADC_LINE(ADC_VREF_INT), ADC_RES_12BIT);
    return vbat;
}

void _init_unused_pins(void) {
    gpio_t unused_pins[] = {
        //GPIO_PIN(PORT_A, 0), OUT0
        //GPIO_PIN(PORT_A, 1), OUT1
        //GPIO_PIN(PORT_A, 2), OUT2
        //GPIO_PIN(PORT_A, 3), OUT3
        GPIO_PIN(PORT_A, 4),
        GPIO_PIN(PORT_A, 5),
        GPIO_PIN(PORT_A, 6),
        GPIO_PIN(PORT_A, 7),
        //GPIO_PIN(PORT_A, 8),MCO
        //GPIO_PIN(PORT_A, 9),
        //GPIO_PIN(PORT_A, 10),
        GPIO_PIN(PORT_A, 11),
        //GPIO_PIN(PORT_A, 12), //USB pullup
        GPIO_PIN(PORT_A, 13),
        GPIO_PIN(PORT_A, 14),
        GPIO_PIN(PORT_A, 15),
        GPIO_PIN(PORT_B, 0),
        GPIO_PIN(PORT_B, 1),
        GPIO_PIN(PORT_B, 3),
        GPIO_PIN(PORT_B, 4),
        //GPIO_PIN(PORT_B, 5), DIO2
        //GPIO_PIN(PORT_B, 6), DIO1
        //GPIO_PIN(PORT_B, 7), DIO0
        GPIO_PIN(PORT_B, 8),
        GPIO_PIN(PORT_B, 9),
        GPIO_PIN(PORT_B, 10),
        GPIO_PIN(PORT_B, 11),
        //GPIO_PIN(PORT_B, 12), NNS
        //GPIO_PIN(PORT_B, 13), SCK
        //GPIO_PIN(PORT_B, 14), MISO
        //GPIO_PIN(PORT_B, 15), MOSI
        GPIO_PIN(PORT_C, 13)};

    for (uint8_t i = 0; i < (sizeof(unused_pins) / sizeof(gpio_t)); i++) {
        gpio_init(unused_pins[i], GPIO_IN_PD);
    }
}
