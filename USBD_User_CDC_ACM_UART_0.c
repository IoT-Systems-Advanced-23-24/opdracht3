/*------------------------------------------------------------------------------
 * MDK Middleware - Component ::USB:Device:CDC
 * Copyright (c) 2004-2020 Arm Limited (or its affiliates). All rights reserved.
 *------------------------------------------------------------------------------
 * Name:    USBD_User_CDC_ACM_UART_0.c
 * Purpose: USB Device Communication Device Class (CDC)
 *          Abstract Control Model (ACM) USB <-> UART Bridge User module
 * Rev.:    V1.0.8
 *----------------------------------------------------------------------------*/
/**
 * \addtogroup usbd_cdcFunctions
 *
 * USBD_User_CDC_ACM_UART_0.c implements the application specific
 * functionality of the CDC ACM class and is used to demonstrate a USB <-> UART
 * bridge. All data received on USB is transmitted on UART and all data
 * received on UART is transmitted on USB.
 *
 * Details of operation:
 *   UART -> USB:
 *     Initial reception on UART is started after the USB Host sets line coding
 *     with SetLineCoding command. Having received a full UART buffer, any
 *     new reception is restarted on the same buffer. Any data received on
 *     the UART is sent over USB using the CDC0_ACM_UART_to_USB_Thread thread.
 *   USB -> UART:
 *     While the UART transmit is not busy, data transmission on the UART is
 *     started in the USBD_CDC0_ACM_DataReceived callback as soon as data is
 *     received on the USB. Further data received on USB is transmitted on
 *     UART in the UART callback routine until there is no more data available.
 *     In this case, the next UART transmit is restarted from the
 *     USBD_CDC0_ACM_DataReceived callback as soon as new data is received
 *     on the USB.
 *
 * The following constants in this module affect the module functionality:
 *
 *  - UART_PORT:        specifies UART Port
 *      default value:  0 (=UART0)
 *  - UART_BUFFER_SIZE: specifies UART data Buffer Size
 *      default value:  512
 *
 * Notes:
 *   If the USB is slower than the UART, data can get lost. This may happen
 *   when USB is pausing during data reception because of the USB Host being
 *   too loaded with other tasks and not polling the Bulk IN Endpoint often
 *   enough (up to 2 seconds of gap in polling Bulk IN Endpoint may occur).
 *   This problem can be solved by using a large enough UART buffer to
 *   compensate up to a few seconds of received UART data or by using UART
 *   flow control.
 *   If the device that receives the UART data (usually a PC) is too loaded
 *   with other tasks it can also loose UART data. This problem can only be
 *   solved by using UART flow control.
 *
 *   This file has to be adapted in case of UART flow control usage.
 */
 
 
//! [code_USBD_User_CDC_ACM]
 
#include <stdio.h>
#include <string.h>
#include "main.h"
#include "rl_usb.h"
#include "Board_LED.h"
#include "Driver_USART.h"
#include <stdlib.h>
#include "GUI.h"
#include "Temp.h"
#include "stm32f7xx_hal_adc.h"

#define USB_RECEIVE_BUFFER_SIZE (512)
uint8_t usb_receive_buffer[USB_RECEIVE_BUFFER_SIZE];
 
// UART Configuration ----------------------------------------------------------
 
#define  UART_PORT              1       // UART Port number
#define  UART_BUFFER_SIZE      (512)    // UART Buffer Size
 
//------------------------------------------------------------------------------
 
#define _UART_Driver_(n)        Driver_USART##n
#define  UART_Driver_(n)       _UART_Driver_(n)
extern   ARM_DRIVER_USART       UART_Driver_(UART_PORT);
#define  ptrUART              (&UART_Driver_(UART_PORT))
 
// External functions
#ifdef   USB_CMSIS_RTOS
extern   void                   CDC0_ACM_UART_to_USB_Thread (void const *arg);
#endif
 
// Local Variables
static            uint8_t       uart_rx_buf[UART_BUFFER_SIZE];
static            uint8_t       uart_tx_buf[UART_BUFFER_SIZE];
 
static   volatile int32_t       uart_rx_cnt         =   0;
static   volatile int32_t       usb_tx_cnt          =   0;
 
static   void                  *cdc_acm_bridge_tid  =   0U;
static   CDC_LINE_CODING        cdc_acm_line_coding = { 0U, 0U, 0U, 0U };
 
 
// Called when UART has transmitted or received requested number of bytes.
// \param[in]   event         UART event
//               - ARM_USART_EVENT_SEND_COMPLETE:    all requested data was sent
//               - ARM_USART_EVENT_RECEIVE_COMPLETE: all requested data was received
static void UART_Callback (uint32_t event) {
  int32_t cnt;
 
  if (event & ARM_USART_EVENT_SEND_COMPLETE) {
    // USB -> UART
    cnt = USBD_CDC_ACM_ReadData(0U, uart_tx_buf, UART_BUFFER_SIZE);
    if (cnt > 0) {
      (void)ptrUART->Send(uart_tx_buf, (uint32_t)(cnt));
    }
  }
 
  if (event & ARM_USART_EVENT_RECEIVE_COMPLETE) {
    // UART data received, restart new reception
    uart_rx_cnt += UART_BUFFER_SIZE;
    (void)ptrUART->Receive(uart_rx_buf, UART_BUFFER_SIZE);
  }
}
 
// Thread: Sends data received on UART to USB
// \param[in]     arg           not used.
#ifdef USB_CMSIS_RTOS2
__NO_RETURN static void CDC0_ACM_UART_to_USB_Thread (void *arg) {
#else
__NO_RETURN        void CDC0_ACM_UART_to_USB_Thread (void const *arg) {
#endif
  int32_t cnt, cnt_to_wrap;
 
  (void)(arg);
 
  for (;;) {
    // UART - > USB
    if (ptrUART->GetStatus().rx_busy != 0U) {
      cnt  = uart_rx_cnt;
      cnt += (int32_t)ptrUART->GetRxCount();
      cnt -= usb_tx_cnt;
      if (cnt >= UART_BUFFER_SIZE) {
        // Dump data received on UART if USB is not consuming fast enough
        usb_tx_cnt += cnt;
        cnt = 0U;
      }
      if (cnt > 0) {
        cnt_to_wrap = (int32_t)(UART_BUFFER_SIZE - ((uint32_t)usb_tx_cnt & (UART_BUFFER_SIZE - 1)));
        if (cnt > cnt_to_wrap) {
          cnt = cnt_to_wrap;
        }
        cnt = USBD_CDC_ACM_WriteData(0U, (uart_rx_buf + ((uint32_t)usb_tx_cnt & (UART_BUFFER_SIZE - 1))), cnt);
        if (cnt > 0) {
          usb_tx_cnt += cnt;
        }
      }
    }
    (void)osDelay(10U);
  }
}
#ifdef USB_CMSIS_RTOS2
#ifdef USB_CMSIS_RTOS2_RTX5
static osRtxThread_t        cdc0_acm_uart_to_usb_thread_cb_mem               __SECTION(.bss.os.thread.cb);
static uint64_t             cdc0_acm_uart_to_usb_thread_stack_mem[512U / 8U] __SECTION(.bss.os.thread.stack);
#endif
static const osThreadAttr_t cdc0_acm_uart_to_usb_thread_attr = {
  "CDC0_ACM_UART_to_USB_Thread",
  0U,
#ifdef USB_CMSIS_RTOS2_RTX5
 &cdc0_acm_uart_to_usb_thread_cb_mem,
  sizeof(osRtxThread_t),
 &cdc0_acm_uart_to_usb_thread_stack_mem[0],
#else
  NULL,
  0U,
  NULL,
#endif
  512U,
  osPriorityNormal,
  0U,
  0U
};
#else
extern const osThreadDef_t os_thread_def_CDC0_ACM_UART_to_USB_Thread;
osThreadDef (CDC0_ACM_UART_to_USB_Thread, osPriorityNormal, 1U, 0U);
#endif
 
#define CMD_BUFFER_SIZE (64)
uint8_t cmd_buffer[CMD_BUFFER_SIZE];
int cmd_buffer_cnt=0;
char storedLCDString[50] = "";
void intToBinaryString(int num, char* binaryString, int size) {
    for (int i = size - 1; i >= 0; i--) {
        binaryString[i] = (num & 1) + '0';
        num >>= 1;
    }
    binaryString[size] = '\0';  // Null-terminate the string
}
void storeLCDString(const char* lcdString) {
    strncpy(storedLCDString, lcdString, sizeof(storedLCDString) - 1);
    storedLCDString[sizeof(storedLCDString) - 1] = '\0'; // Null-terminate the string
}

void process_AT_command(uint8_t* cmd_buffer) {
    if (strncmp((char*)cmd_buffer, "AT+LED", 6) == 0) {
        int led_value = atoi((char*)&cmd_buffer[6]);
        if (led_value >= 1 && led_value <= 8) {
            char binaryString[5];
            intToBinaryString(led_value, binaryString, sizeof(binaryString) - 1);

            // Assuming LED1, LED2, ..., LED8 are defined as consecutive pins
            for (int i = 0; i < 8; i++) {
                if (binaryString[i] == '1') {
                    // Set the corresponding LED
                    switch (i + 1) {
                        case 1:
                            HAL_GPIO_WritePin(LED1_GPIO_Port, LED1_Pin, GPIO_PIN_SET);
                            break;
                        case 2:
                            HAL_GPIO_WritePin(LED2_GPIO_Port, LED2_Pin, GPIO_PIN_SET);
                            break;
                        case 3:
                            HAL_GPIO_WritePin(LED3_GPIO_Port, LED3_Pin, GPIO_PIN_SET);
                            break;
                        case 4:
                            HAL_GPIO_WritePin(LED4_GPIO_Port, LED4_Pin, GPIO_PIN_SET);
                            break;
                        case 5:
                            HAL_GPIO_WritePin(LED5_GPIO_Port, LED5_Pin, GPIO_PIN_SET);
                            break;
                        case 6:
                            HAL_GPIO_WritePin(LED6_GPIO_Port, LED6_Pin, GPIO_PIN_SET);
                            break;
                        case 7:
                            HAL_GPIO_WritePin(LED7_GPIO_Port, LED7_Pin, GPIO_PIN_SET);
                            break;
                        case 8:
                            HAL_GPIO_WritePin(LED8_GPIO_Port, LED8_Pin, GPIO_PIN_SET);
                            break;
                        default:
                            // Handle unexpected case
                            break;
                    }
                } else {
                    // Reset the corresponding LED
                    switch (i + 1) {
                        case 1:
                            HAL_GPIO_WritePin(LED1_GPIO_Port, LED1_Pin, GPIO_PIN_RESET);
                            break;
                        case 2:
                            HAL_GPIO_WritePin(LED2_GPIO_Port, LED2_Pin, GPIO_PIN_RESET);
                            break;
                        case 3:
                            HAL_GPIO_WritePin(LED3_GPIO_Port, LED3_Pin, GPIO_PIN_RESET);
                            break;
                        case 4:
                            HAL_GPIO_WritePin(LED4_GPIO_Port, LED4_Pin, GPIO_PIN_RESET);
                            break;
                        case 5:
                            HAL_GPIO_WritePin(LED5_GPIO_Port, LED5_Pin, GPIO_PIN_RESET);
                            break;
                        case 6:
                            HAL_GPIO_WritePin(LED6_GPIO_Port, LED6_Pin, GPIO_PIN_RESET);
                            break;
                        case 7:
                            HAL_GPIO_WritePin(LED7_GPIO_Port, LED7_Pin, GPIO_PIN_RESET);
                            break;
                        case 8:
                            HAL_GPIO_WritePin(LED8_GPIO_Port, LED8_Pin, GPIO_PIN_RESET);
                            break;
                        default:
                            // Handle unexpected case
                            break;
                    }
                }
            }
        }
          snprintf((char*)uart_tx_buf, UART_BUFFER_SIZE, "LED value set to: %d\r\n", led_value);
    }
else if (strncmp((char*)cmd_buffer, "AT+LCD=", 6) == 0) {
        // Extract the string after "AT+LCD="
        const char* lcdString = (char*)(cmd_buffer + 6);

        // Store the extracted string
        storeLCDString(lcdString);
        //send string to terminal
        snprintf((char*)uart_tx_buf, UART_BUFFER_SIZE, "LCD string set to: %s\r\n", lcdString);
    }
else if (strncmp((char*)cmd_buffer, "AT+BUTTON", 9) == 0) {
    // Check the state of each button

    if (HAL_GPIO_ReadPin(SW1_GPIO_Port, SW1_Pin) == GPIO_PIN_RESET && HAL_GPIO_ReadPin(SW2_GPIO_Port, SW2_Pin) == GPIO_PIN_RESET && HAL_GPIO_ReadPin(SW3_GPIO_Port, SW3_Pin) == GPIO_PIN_RESET && HAL_GPIO_ReadPin(SW4_GPIO_Port, SW4_Pin) == GPIO_PIN_RESET) {
        snprintf((char*)uart_tx_buf, UART_BUFFER_SIZE, "All buttons are pressed\r\n");
    }

    else if (HAL_GPIO_ReadPin(SW1_GPIO_Port, SW1_Pin) == GPIO_PIN_RESET && HAL_GPIO_ReadPin(SW2_GPIO_Port, SW2_Pin) == GPIO_PIN_RESET && HAL_GPIO_ReadPin(SW3_GPIO_Port, SW3_Pin) == GPIO_PIN_RESET){
        snprintf((char*)uart_tx_buf, UART_BUFFER_SIZE, "Button 1, Button 2 and Button 3 are pressed\r\n");
    }
    else if (HAL_GPIO_ReadPin(SW1_GPIO_Port, SW1_Pin) == GPIO_PIN_RESET && HAL_GPIO_ReadPin(SW2_GPIO_Port, SW2_Pin) == GPIO_PIN_RESET && HAL_GPIO_ReadPin(SW4_GPIO_Port, SW4_Pin) == GPIO_PIN_RESET){
        snprintf((char*)uart_tx_buf, UART_BUFFER_SIZE, "Button 1, Button 2 and Button 4 are pressed\r\n");
    }
    else if (HAL_GPIO_ReadPin(SW1_GPIO_Port, SW1_Pin) == GPIO_PIN_RESET && HAL_GPIO_ReadPin(SW3_GPIO_Port, SW3_Pin) == GPIO_PIN_RESET && HAL_GPIO_ReadPin(SW4_GPIO_Port, SW4_Pin) == GPIO_PIN_RESET){
        snprintf((char*)uart_tx_buf, UART_BUFFER_SIZE, "Button 1, Button 3 and Button 4 are pressed\r\n");
    }
    else if (HAL_GPIO_ReadPin(SW2_GPIO_Port, SW2_Pin) == GPIO_PIN_RESET && HAL_GPIO_ReadPin(SW3_GPIO_Port, SW3_Pin) == GPIO_PIN_RESET && HAL_GPIO_ReadPin(SW4_GPIO_Port, SW4_Pin) == GPIO_PIN_RESET){
        snprintf((char*)uart_tx_buf, UART_BUFFER_SIZE, "Button 2, Button 3 and Button 4 are pressed\r\n");
    }


    else if (HAL_GPIO_ReadPin(SW1_GPIO_Port, SW1_Pin) == GPIO_PIN_RESET && HAL_GPIO_ReadPin(SW2_GPIO_Port, SW2_Pin) == GPIO_PIN_RESET) {
        snprintf((char*)uart_tx_buf, UART_BUFFER_SIZE, "Button 1 and Button 2 are pressed\r\n");
    } else if (HAL_GPIO_ReadPin(SW1_GPIO_Port, SW1_Pin) == GPIO_PIN_RESET && HAL_GPIO_ReadPin(SW3_GPIO_Port, SW3_Pin) == GPIO_PIN_RESET){
        snprintf((char*)uart_tx_buf, UART_BUFFER_SIZE, "Button 1 and Button 3 are pressed\r\n");
    }
    else if (HAL_GPIO_ReadPin(SW1_GPIO_Port, SW1_Pin) == GPIO_PIN_RESET && HAL_GPIO_ReadPin(SW4_GPIO_Port, SW4_Pin) == GPIO_PIN_RESET){
        snprintf((char*)uart_tx_buf, UART_BUFFER_SIZE, "Button 1 and Button 4 are pressed\r\n");
    }


    else if (HAL_GPIO_ReadPin(SW2_GPIO_Port, SW2_Pin) == GPIO_PIN_RESET && HAL_GPIO_ReadPin(SW3_GPIO_Port, SW3_Pin) == GPIO_PIN_RESET){
        snprintf((char*)uart_tx_buf, UART_BUFFER_SIZE, "Button 2 and Button 3 are pressed\r\n");
    }
    else if (HAL_GPIO_ReadPin(SW2_GPIO_Port, SW2_Pin) == GPIO_PIN_RESET && HAL_GPIO_ReadPin(SW4_GPIO_Port, SW4_Pin) == GPIO_PIN_RESET){
        snprintf((char*)uart_tx_buf, UART_BUFFER_SIZE, "Button 2 and Button 4 are pressed\r\n");
    }


    else if (HAL_GPIO_ReadPin(SW3_GPIO_Port, SW3_Pin) == GPIO_PIN_RESET && HAL_GPIO_ReadPin(SW4_GPIO_Port, SW4_Pin) == GPIO_PIN_RESET){
        snprintf((char*)uart_tx_buf, UART_BUFFER_SIZE, "Button 3 and Button 4 are pressed\r\n");
    }

    else if (HAL_GPIO_ReadPin(SW1_GPIO_Port, SW1_Pin) == GPIO_PIN_RESET) {
        snprintf((char*)uart_tx_buf, UART_BUFFER_SIZE, "Button 1 is pressed\r\n");
    } else if (HAL_GPIO_ReadPin(SW2_GPIO_Port, SW2_Pin) == GPIO_PIN_RESET) {
        snprintf((char*)uart_tx_buf, UART_BUFFER_SIZE, "Button 2 is pressed\r\n");
    } else if (HAL_GPIO_ReadPin(SW3_GPIO_Port, SW3_Pin) == GPIO_PIN_RESET) {
        snprintf((char*)uart_tx_buf, UART_BUFFER_SIZE, "Button 3 is pressed\r\n");
    } else if (HAL_GPIO_ReadPin(SW4_GPIO_Port, SW4_Pin) == GPIO_PIN_RESET) {
        snprintf((char*)uart_tx_buf, UART_BUFFER_SIZE, "Button 4 is pressed\r\n");
    }else {
        snprintf((char*)uart_tx_buf, UART_BUFFER_SIZE, "No button is pressed\r\n");
    }
    ptrUART->Send(uart_tx_buf, strlen((char*)uart_tx_buf));
	}
  else if (strncmp((char*)cmd_buffer, "AT+POT", 6) == 0) {
    int32_t potValue;
    ReadPot(&potValue);
    snprintf((char*)uart_tx_buf, UART_BUFFER_SIZE, "Potentiometer value: %d\r\n", potValue);
    ptrUART->Send(uart_tx_buf, strlen((char*)uart_tx_buf));
  }
}


// CDC ACM Callbacks -----------------------------------------------------------
 
// Called when new data was received from the USB Host.
// \param[in]   len           number of bytes available to read.
void USBD_CDC0_ACM_DataReceived (uint32_t len) {
  int32_t cnt;
 
  (void)(len);
 
  //if (ptrUART->GetStatus().tx_busy == 0U) {
    //Start USB -> UART
    cnt = USBD_CDC_ACM_ReadData(0U, usb_receive_buffer, USB_RECEIVE_BUFFER_SIZE);
    if (cnt > 0) {
      //(void)ptrUART->Send(uart_tx_buf, (uint32_t)(cnt));
			for(int i=0; i<cnt; i++)
			{
				//check if character is '\r'
				if(usb_receive_buffer[i]=='\r')
				{
					cmd_buffer[cmd_buffer_cnt++]='\0';
					process_AT_command(cmd_buffer);
					cmd_buffer_cnt=0;
				}
				else
				{
					cmd_buffer[cmd_buffer_cnt++]=usb_receive_buffer[i];
				}
			}			
    }
  //}
}
 
// Called during USBD_Initialize to initialize the USB CDC class instance (ACM).
void USBD_CDC0_ACM_Initialize (void) {
  (void)ptrUART->Initialize   (UART_Callback);
  (void)ptrUART->PowerControl (ARM_POWER_FULL);
 
#ifdef USB_CMSIS_RTOS2
  cdc_acm_bridge_tid = osThreadNew (CDC0_ACM_UART_to_USB_Thread, NULL, &cdc0_acm_uart_to_usb_thread_attr);
#else
  cdc_acm_bridge_tid = osThreadCreate (osThread (CDC0_ACM_UART_to_USB_Thread), NULL);
#endif
}
 
 
// Called during USBD_Uninitialize to de-initialize the USB CDC class instance (ACM).
void USBD_CDC0_ACM_Uninitialize (void) {
 
  if (osThreadTerminate (cdc_acm_bridge_tid) == osOK) {
    cdc_acm_bridge_tid = NULL;
  }
 
  (void)ptrUART->Control      (ARM_USART_ABORT_RECEIVE, 0U);
  (void)ptrUART->PowerControl (ARM_POWER_OFF);
  (void)ptrUART->Uninitialize ();
}
 
 
// Called upon USB Bus Reset Event.
void USBD_CDC0_ACM_Reset (void) {
  (void)ptrUART->Control      (ARM_USART_ABORT_SEND,    0U);
  (void)ptrUART->Control      (ARM_USART_ABORT_RECEIVE, 0U);
}
 
 
// Called upon USB Host request to change communication settings.
// \param[in]   line_coding   pointer to CDC_LINE_CODING structure.
// \return      true          set line coding request processed.
// \return      false         set line coding request not supported or not processed.
bool USBD_CDC0_ACM_SetLineCoding (const CDC_LINE_CODING *line_coding) {
  uint32_t data_bits = 0U, parity = 0U, stop_bits = 0U;
  int32_t  status;
 
  (void)ptrUART->Control (ARM_USART_ABORT_SEND,    0U);
  (void)ptrUART->Control (ARM_USART_ABORT_RECEIVE, 0U);
  (void)ptrUART->Control (ARM_USART_CONTROL_TX,    0U);
  (void)ptrUART->Control (ARM_USART_CONTROL_RX,    0U);
 
  switch (line_coding->bCharFormat) {
    case 0:                             // 1 Stop bit
      stop_bits = ARM_USART_STOP_BITS_1;
      break;
    case 1:                             // 1.5 Stop bits
      stop_bits = ARM_USART_STOP_BITS_1_5;
      break;
    case 2:                             // 2 Stop bits
      stop_bits = ARM_USART_STOP_BITS_2;
      break;
    default:
      return false;
  }
 
  switch (line_coding->bParityType) {
    case 0:                             // None
      parity = ARM_USART_PARITY_NONE;
      break;
    case 1:                             // Odd
      parity = ARM_USART_PARITY_ODD;
      break;
    case 2:                             // Even
      parity = ARM_USART_PARITY_EVEN;
      break;
    default:
      return false;
  }
 
  switch (line_coding->bDataBits) {
    case 5:
      data_bits = ARM_USART_DATA_BITS_5;
      break;
    case 6:
      data_bits = ARM_USART_DATA_BITS_6;
      break;
    case 7:
      data_bits = ARM_USART_DATA_BITS_7;
      break;
    case 8:
      data_bits = ARM_USART_DATA_BITS_8;
      break;
    default:
      return false;
  }
 
  status = ptrUART->Control(ARM_USART_MODE_ASYNCHRONOUS  |
                            data_bits                    |
                            parity                       |
                            stop_bits                    ,
                            line_coding->dwDTERate       );

  if (status != ARM_DRIVER_OK) {
    return false;
  }
 
  // Store requested settings to local variable
  cdc_acm_line_coding = *line_coding;
 
  uart_rx_cnt = 0;
  usb_tx_cnt  = 0;
 
  (void)ptrUART->Control (ARM_USART_CONTROL_TX, 1U);
  (void)ptrUART->Control (ARM_USART_CONTROL_RX, 1U);
 
  (void)ptrUART->Receive (uart_rx_buf, UART_BUFFER_SIZE);
 
  return true;
}
 
 
// Called upon USB Host request to retrieve communication settings.
// \param[out]  line_coding   pointer to CDC_LINE_CODING structure.
// \return      true          get line coding request processed.
// \return      false         get line coding request not supported or not processed.
bool USBD_CDC0_ACM_GetLineCoding (CDC_LINE_CODING *line_coding) {
 
  // Load settings from ones stored on USBD_CDC0_ACM_SetLineCoding callback
  *line_coding = cdc_acm_line_coding;
 
  return true;
}
 
 
// Called upon USB Host request to set control line states.
// \param [in]  state         control line settings bitmap.
//                - bit 0: DTR state
//                - bit 1: RTS state
// \return      true          set control line state request processed.
// \return      false         set control line state request not supported or not processed.
bool USBD_CDC0_ACM_SetControlLineState (uint16_t state) {
  // Add code for set control line state
 
  (void)(state);
 
  return true;
}
 
//! [code_USBD_User_CDC_ACM]
