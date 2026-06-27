/**
  ******************************************************************************
  * File Name          : dht11.c
  * Description        : DHT11 temperature and humidity sensor driver
  *                      Single-wire protocol on PB6, SysTick microsecond delay
  ******************************************************************************
  */

#include "dht11.h"
#include <string.h>

/* DWT cycle counter for microsecond precision timing on Cortex-M4 */
#define DWT_CYCCNT   (*(volatile uint32_t *)0xE0001000)
#define DWT_CTRL     (*(volatile uint32_t *)0xE0001000)
#define DWT_DEMCR    (*(volatile uint32_t *)0xE000FC04)
#define DEM_CR_TRCENA (1U << 24)

static uint32_t g_cpu_freq_mhz;

static void dwt_delay_init(void)
{
    /* Enable DWT cycle counter */
    DWT_DEMCR |= DEM_CR_TRCENA;
    DWT_CYCCNT = 0;

    /* F407 HSI = 16 MHz, no PLL */
    g_cpu_freq_mhz = 16U;
}

static void dwt_delay_us(uint32_t us)
{
    uint32_t start = DWT_CYCCNT;
    uint32_t ticks = us * g_cpu_freq_mhz;
    while ((DWT_CYCCNT - start) < ticks) {
        /* spin */
    }
}

static void dht11_set_output(void)
{
    GPIO_InitTypeDef gpio = {0};
    gpio.Pin = DHT11_DATA_PIN;
    gpio.Mode = GPIO_MODE_OUTPUT_OD;
    gpio.Pull = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(DHT11_DATA_PORT, &gpio);
}

static void dht11_set_input(void)
{
    GPIO_InitTypeDef gpio = {0};
    gpio.Pin = DHT11_DATA_PIN;
    gpio.Mode = GPIO_MODE_INPUT;
    gpio.Pull = GPIO_NOPULL;
    HAL_GPIO_Init(DHT11_DATA_PORT, &gpio);
}

static void dht11_write_low(void)
{
    HAL_GPIO_WritePin(DHT11_DATA_PORT, DHT11_DATA_PIN, GPIO_PIN_RESET);
}

static void dht11_write_high(void)
{
    HAL_GPIO_WritePin(DHT11_DATA_PORT, DHT11_DATA_PIN, GPIO_PIN_SET);
}

static uint8_t dht11_read_pin(void)
{
    return (uint8_t)HAL_GPIO_ReadPin(DHT11_DATA_PORT, DHT11_DATA_PIN);
}

HAL_StatusTypeDef DHT11_Init(void)
{
    dwt_delay_init();
    dht11_set_output();
    dht11_write_high();
    HAL_Delay(2000);  /* DHT11 needs 1s+ after power on */
    return HAL_OK;
}

HAL_StatusTypeDef DHT11_Read(uint8_t *humidity, uint8_t *temperature)
{
    uint8_t data[5] = {0};
    uint8_t i, j;
    uint32_t timeout;

    if (humidity == NULL || temperature == NULL) {
        return HAL_ERROR;
    }

    /* ---- Start signal ---- */
    __disable_irq();
    dht11_set_output();
    dht11_write_low();
    HAL_Delay(25);        /* Pull low >= 18ms */
    dht11_write_high();
    dwt_delay_us(30);     /* Wait 20-40us */
    dht11_set_input();

    /* ---- Wait for DHT11 response: low 80us then high 80us ---- */
    timeout = 100;
    while (dht11_read_pin() == GPIO_PIN_SET && timeout-- > 0) {
        dwt_delay_us(1);
    }
    if (timeout == 0) {
        __enable_irq();
        return HAL_TIMEOUT;
    }

    timeout = 100;
    while (dht11_read_pin() == GPIO_PIN_RESET && timeout-- > 0) {
        dwt_delay_us(1);
    }
    if (timeout == 0) {
        __enable_irq();
        return HAL_TIMEOUT;
    }

    timeout = 100;
    while (dht11_read_pin() == GPIO_PIN_SET && timeout-- > 0) {
        dwt_delay_us(1);
    }
    if (timeout == 0) {
        __enable_irq();
        return HAL_TIMEOUT;
    }

    /* ---- Read 40 bits ---- */
    for (i = 0; i < 5; ++i) {
        for (j = 0; j < 8; ++j) {
            /* Each bit: 50us low, then data */
            timeout = 80;
            while (dht11_read_pin() == GPIO_PIN_RESET && timeout-- > 0) {
                dwt_delay_us(1);
            }

            dwt_delay_us(35);  /* Measure high duration */

            data[i] <<= 1;
            if (dht11_read_pin() == GPIO_PIN_SET) {
                data[i] |= 1U;
                /* Wait remaining high time */
                timeout = 50;
                while (dht11_read_pin() == GPIO_PIN_SET && timeout-- > 0) {
                    dwt_delay_us(1);
                }
            }
        }
    }
    __enable_irq();

    /* ---- Verify checksum ---- */
    if (data[4] != (uint8_t)(data[0] + data[1] + data[2] + data[3])) {
        return HAL_ERROR;
    }

    *humidity = data[0];
    *temperature = data[2];
    return HAL_OK;
}
