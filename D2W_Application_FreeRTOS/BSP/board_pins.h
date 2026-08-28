#ifndef BOARD_PINS_H
#define BOARD_PINS_H

#include "gd32f30x.h"

//Confirms that PB15 controls reset and PA8 detects reset
#define POWER_CTRL_CONFIRMED_PIN_MAPPING    1U


//Controls Orin power and reset
//PB14 controls the Orin VDD_IN regulator and enables it at a high level
#define ORIN_VDD_RCU    RCU_GPIOB
#define ORIN_VDD_PORT    GPIOB
#define ORIN_VDD_PIN    GPIO_PIN_14
#define ORIN_VDD_ACTIVE_LEVEL    1U

//PA7 controls Orin POWER_EN and asserts it at a high level
#define ORIN_POWER_EN_RCU    RCU_GPIOA
#define ORIN_POWER_EN_PORT    GPIOA
#define ORIN_POWER_EN_PIN    GPIO_PIN_7
#define ORIN_POWER_EN_ACTIVE_LEVEL    1U

//PB15 controls Orin SYS_RESET* and pulls it low through an open drain output
#define ORIN_SYS_RESET_CTRL_RCU    RCU_GPIOB
#define ORIN_SYS_RESET_CTRL_PORT    GPIOB
#define ORIN_SYS_RESET_CTRL_PIN    GPIO_PIN_15
#define ORIN_SYS_RESET_CTRL_ACTIVE_LEVEL    0U

//PA8 detects Orin SYS_RESET* after level translation and treats low as active
#define ORIN_SYS_RESET_SENSE_RCU    RCU_GPIOA
#define ORIN_SYS_RESET_SENSE_PORT    GPIOA
#define ORIN_SYS_RESET_SENSE_PIN    GPIO_PIN_8
#define ORIN_SYS_RESET_SENSE_ACTIVE_LEVEL    0U

//Lets existing modules use PB15 for control and PA8 for detection
#define ORIN_RESET_RCU    ORIN_SYS_RESET_CTRL_RCU
#define ORIN_RESET_PORT    ORIN_SYS_RESET_CTRL_PORT
#define ORIN_RESET_PIN    ORIN_SYS_RESET_CTRL_PIN
#define ORIN_RESET_ACTIVE_LEVEL    ORIN_SYS_RESET_CTRL_ACTIVE_LEVEL

#define ORIN_RESET_STATUS_RCU    ORIN_SYS_RESET_SENSE_RCU
#define ORIN_RESET_STATUS_PORT    ORIN_SYS_RESET_SENSE_PORT
#define ORIN_RESET_STATUS_PIN    ORIN_SYS_RESET_SENSE_PIN
#define ORIN_RESET_STATUS_ACTIVE_LEVEL    ORIN_SYS_RESET_SENSE_ACTIVE_LEVEL


//Controls carrier board peripheral power
//PA0 controls the peripheral 5 V rail
#define PERIPH_5V_EN_RCU    RCU_GPIOA
#define PERIPH_5V_EN_PORT    GPIOA
#define PERIPH_5V_EN_PIN    GPIO_PIN_0
#define PERIPH_5V_EN_ACTIVE_LEVEL    1U

//PA1 controls the peripheral 3.3 V rail
#define PERIPH_3V3_EN_RCU    RCU_GPIOA
#define PERIPH_3V3_EN_PORT    GPIOA
#define PERIPH_3V3_EN_PIN    GPIO_PIN_1
#define PERIPH_3V3_EN_ACTIVE_LEVEL    1U

//PA4 controls the CH634 3.3 V supply
#define CH634_3V3_EN_RCU    RCU_GPIOA
#define CH634_3V3_EN_PORT    GPIOA
#define CH634_3V3_EN_PIN    GPIO_PIN_4
#define CH634_3V3_EN_ACTIVE_LEVEL    1U

//PC8 controls the connector 3.3 V supply
#define CONNECTOR_3V3_EN_RCU    RCU_GPIOC
#define CONNECTOR_3V3_EN_PORT    GPIOC
#define CONNECTOR_3V3_EN_PIN    GPIO_PIN_8
#define CONNECTOR_3V3_EN_ACTIVE_LEVEL    1U


//Detects Orin and USB status signals
//PA11 detects Orin SHUTDOWN_REQ* and triggers EXTI when the signal goes low
#define SHUTDOWN_REQ_RCU    RCU_GPIOA
#define SHUTDOWN_REQ_PORT    GPIOA
#define SHUTDOWN_REQ_PIN    GPIO_PIN_11
#define SHUTDOWN_REQ_ACTIVE_LEVEL    0U
#define SHUTDOWN_REQ_PORT_SOURCE    GPIO_PORT_SOURCE_GPIOA
#define SHUTDOWN_REQ_PIN_SOURCE    GPIO_PIN_SOURCE_11
#define SHUTDOWN_REQ_EXTI_LINE    EXTI_11
#define SHUTDOWN_REQ_IRQ    EXTI10_15_IRQn

//PD1 detects the USB VBUS enable state and treats low as active
#define USB_VBUS_DET_RCU    RCU_GPIOD
#define USB_VBUS_DET_PORT    GPIOD
#define USB_VBUS_DET_PIN    GPIO_PIN_1
#define USB_VBUS_DET_ACTIVE_LEVEL    0U


//Controls and detects the external watchdog
//PB6 controls WDI and toggles it to feed the watchdog
#define WATCHDOG_WDI_RCU    RCU_GPIOB
#define WATCHDOG_WDI_PORT    GPIOB
#define WATCHDOG_WDI_PIN    GPIO_PIN_6

//PB7 detects WDO* and treats a low level as a watchdog timeout
#define WATCHDOG_WDO_RCU    RCU_GPIOB
#define WATCHDOG_WDO_PORT    GPIOB
#define WATCHDOG_WDO_PIN    GPIO_PIN_7
#define WATCHDOG_WDO_ACTIVE_LEVEL    0U
#define WATCHDOG_WDO_PORT_SOURCE    GPIO_PORT_SOURCE_GPIOB
#define WATCHDOG_WDO_PIN_SOURCE    GPIO_PIN_SOURCE_7
#define WATCHDOG_WDO_EXTI_LINE    EXTI_7
#define WATCHDOG_WDO_IRQ    EXTI5_9_IRQn



//Detects the monitored power rail voltages
//ADC0 samples the four voltage sense inputs below
#define VOLTAGE_ADC    ADC0
#define VOLTAGE_ADC_RCU    RCU_ADC0
#define VOLTAGE_ADC_GPIO_RCU    RCU_GPIOC

//PC1 detects Orin VDD_IN
#define ORIN_VDD_SENSE_PORT    GPIOC
#define ORIN_VDD_SENSE_PIN    GPIO_PIN_1
#define ORIN_VDD_SENSE_CHANNEL    ADC_CHANNEL_11

//PC2 detects the system 5 V rail
#define SYSTEM_5V_SENSE_PORT    GPIOC
#define SYSTEM_5V_SENSE_PIN    GPIO_PIN_2
#define SYSTEM_5V_SENSE_CHANNEL    ADC_CHANNEL_12

//PC3 detects the system 3.3 V rail
#define SYSTEM_3V3_SENSE_PORT    GPIOC
#define SYSTEM_3V3_SENSE_PIN    GPIO_PIN_3
#define SYSTEM_3V3_SENSE_CHANNEL    ADC_CHANNEL_13

//PC5 detects the system 1.8 V rail
#define SYSTEM_1V8_SENSE_PORT    GPIOC
#define SYSTEM_1V8_SENSE_PIN    GPIO_PIN_5
#define SYSTEM_1V8_SENSE_CHANNEL    ADC_CHANNEL_15


//Detects a power failure
//PB8 detects PFO* and triggers EXTI when the signal goes low
#define POWER_FAIL_PORT    GPIOB
#define POWER_FAIL_PIN    GPIO_PIN_8
#define POWER_FAIL_GPIO_RCU    RCU_GPIOB
#define POWER_FAIL_ACTIVE_LEVEL    0U

#define POWER_FAIL_PORT_SOURCE    GPIO_PORT_SOURCE_GPIOB
#define POWER_FAIL_PIN_SOURCE    GPIO_PIN_SOURCE_8
#define POWER_FAIL_EXTI_LINE    EXTI_8
#define POWER_FAIL_IRQ    EXTI5_9_IRQn


//Communicates with the HYM8563S calendar RTC
//I2C1 exchanges date and time data with the HYM8563S RTC
#define RTC_CALENDAR_I2C_PERIPH    I2C1
#define RTC_CALENDAR_I2C_RCU    RCU_I2C1
#define RTC_CALENDAR_GPIO_RCU    RCU_GPIOB

//PB10 provides the I2C1 clock
#define RTC_CALENDAR_SCL_PORT    GPIOB
#define RTC_CALENDAR_SCL_PIN    GPIO_PIN_10

//PB11 carries bidirectional I2C1 data
#define RTC_CALENDAR_SDA_PORT    GPIOB
#define RTC_CALENDAR_SDA_PIN    GPIO_PIN_11


//Communicates with the LK1220D GNSS and controls its reset input
//PA2 sends configuration data to LK1220D RXD0
#define GNSS_UART_PERIPH    USART1
#define GNSS_UART_RCU    RCU_USART1
#define GNSS_UART_IRQ    USART1_IRQn
#define GNSS_UART_TX_GPIO_RCU    RCU_GPIOA
#define GNSS_UART_TX_PORT    GPIOA
#define GNSS_UART_TX_PIN    GPIO_PIN_2

//PA3 receives NMEA time data from LK1220D TXD0
#define GNSS_UART_RX_GPIO_RCU    RCU_GPIOA
#define GNSS_UART_RX_PORT    GPIOA
#define GNSS_UART_RX_PIN    GPIO_PIN_3

//PB5 controls LK1220D RESET and releases reset at a high level
#define GNSS_RESET_RCU    RCU_GPIOB
#define GNSS_RESET_PORT    GPIOB
#define GNSS_RESET_PIN    GPIO_PIN_5
#define GNSS_RESET_ACTIVE_LEVEL    0U

//PD4 detects LK1220D PPS1 and triggers EXTI on its rising edge
#define GNSS_PPS_INPUT_RCU    RCU_GPIOD
#define GNSS_PPS_INPUT_PORT    GPIOD
#define GNSS_PPS_INPUT_PIN    GPIO_PIN_4
#define GNSS_PPS_INPUT_ACTIVE_LEVEL    1U
#define GNSS_PPS_INPUT_PORT_SOURCE    GPIO_PORT_SOURCE_GPIOD
#define GNSS_PPS_INPUT_PIN_SOURCE    GPIO_PIN_SOURCE_4
#define GNSS_PPS_INPUT_EXTI_LINE    EXTI_4
#define GNSS_PPS_INPUT_IRQ    EXTI4_IRQn


//Sends UTC data and PPS to RK3588
//PC10 sends NMEA time data
#define RK_TIME_UART_PERIPH    UART3
#define RK_TIME_UART_RCU    RCU_UART3
#define RK_TIME_UART_IRQ    UART3_IRQn
#define RK_TIME_UART_TX_GPIO_RCU    RCU_GPIOC
#define RK_TIME_UART_TX_PORT    GPIOC
#define RK_TIME_UART_TX_PIN    GPIO_PIN_10

//PC11 receives reserved RK3588 serial data
#define RK_TIME_UART_RX_GPIO_RCU    RCU_GPIOC
#define RK_TIME_UART_RX_PORT    GPIOC
#define RK_TIME_UART_RX_PIN    GPIO_PIN_11

//PD0 sends a rising edge PPS to RK3588
#define RK_PPS_OUTPUT_RCU    RCU_GPIOD
#define RK_PPS_OUTPUT_PORT    GPIOD
#define RK_PPS_OUTPUT_PIN    GPIO_PIN_0
#define RK_PPS_OUTPUT_ACTIVE_LEVEL    1U


//Sends UTC data and PPS to the Airy connectors
//PA9 sends NMEA data
#define AIRY_J16_TIME_UART_PERIPH    USART0
#define AIRY_J16_TIME_UART_RCU    RCU_USART0
#define AIRY_J16_TIME_UART_IRQ    USART0_IRQn
#define AIRY_J16_TIME_UART_TX_GPIO_RCU    RCU_GPIOA
#define AIRY_J16_TIME_UART_TX_PORT    GPIOA
#define AIRY_J16_TIME_UART_TX_PIN    GPIO_PIN_9

//PD6 sends a rising edge PPS
#define AIRY_J16_PPS_OUTPUT_RCU    RCU_GPIOD
#define AIRY_J16_PPS_OUTPUT_PORT    GPIOD
#define AIRY_J16_PPS_OUTPUT_PIN    GPIO_PIN_6
#define AIRY_J16_PPS_OUTPUT_ACTIVE_LEVEL    1U

//PC12 sends NMEA data
#define AIRY_J22_TIME_UART_PERIPH    UART4
#define AIRY_J22_TIME_UART_RCU    RCU_UART4
#define AIRY_J22_TIME_UART_IRQ    UART4_IRQn
#define AIRY_J22_TIME_UART_TX_GPIO_RCU    RCU_GPIOC
#define AIRY_J22_TIME_UART_TX_PORT    GPIOC
#define AIRY_J22_TIME_UART_TX_PIN    GPIO_PIN_12

//PD3 sends a rising edge PPS
#define AIRY_J22_PPS_OUTPUT_RCU    RCU_GPIOD
#define AIRY_J22_PPS_OUTPUT_PORT    GPIOD
#define AIRY_J22_PPS_OUTPUT_PIN    GPIO_PIN_3
#define AIRY_J22_PPS_OUTPUT_ACTIVE_LEVEL    1U

#endif 
