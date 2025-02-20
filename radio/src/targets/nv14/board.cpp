/*
 * Copyright (C) OpenTX
 *
 * Based on code named
 *   th9x - http://code.google.com/p/th9x
 *   er9x - http://code.google.com/p/er9x
 *   gruvin9x - http://code.google.com/p/gruvin9x
 *
 * License GPLv2: http://www.gnu.org/licenses/gpl-2.0.html
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */
 
#include "opentx.h"
#include "touch.h"
#if defined(__cplusplus) && !defined(SIMU)
extern "C" {
#endif
#include "usb_dcd_int.h"
#include "usb_bsp.h"
#if defined(__cplusplus) && !defined(SIMU)
}
#endif

extern void flysky_hall_stick_init( void );

HardwareOptions hardwareOptions;

void watchdogInit(unsigned int duration)
{
  IWDG->KR = 0x5555;      // Unlock registers
  IWDG->PR = 3;           // Divide by 32 => 1kHz clock
  IWDG->KR = 0x5555;      // Unlock registers
  IWDG->RLR = duration;   // 1.5 seconds nominal
  IWDG->KR = 0xAAAA;      // reload
  IWDG->KR = 0xCCCC;      // start
}

#if defined(SEMIHOSTING)
extern "C" void initialise_monitor_handles();
#endif

void delay_self(int count)
{
   for (int i = 50000; i > 0; i--)
   {
       for (; count > 0; count--);
   }
}
#define RCC_AHB1PeriphMinimum (PWR_RCC_AHB1Periph |\
                               LCD_RCC_AHB1Periph |\
                               BACKLIGHT_RCC_AHB1Periph |\
                               SDRAM_RCC_AHB1Periph \
                              )
#define RCC_AHB1PeriphOther   (SD_RCC_AHB1Periph |\
                               AUDIO_RCC_AHB1Periph |\
                               MONITOR_RCC_AHB1Periph |\
                               KEYS_RCC_AHB1Periph |\
                               ADC_RCC_AHB1Periph |\
                               AUX_SERIAL_RCC_AHB1Periph |\
                               TELEMETRY_RCC_AHB1Periph |\
                               TRAINER_RCC_AHB1Periph |\
                               AUDIO_RCC_AHB1Periph |\
                               HAPTIC_RCC_AHB1Periph |\
                               INTMODULE_RCC_AHB1Periph |\
                               FLYSKY_HALL_RCC_AHB1Periph |\
                               EXTMODULE_RCC_AHB1Periph\
                              )
#define RCC_AHB3PeriphMinimum (SDRAM_RCC_AHB3Periph)

#define RCC_APB1PeriphMinimum (INTERRUPT_xMS_RCC_APB1Periph |\
                               TIMER_2MHz_RCC_APB1Periph |\
                               BACKLIGHT_RCC_APB1Periph \
                              )

#define RCC_APB1PeriphOther   (TELEMETRY_RCC_APB1Periph |\
                               TRAINER_RCC_APB1Periph |\
                               INTMODULE_RCC_APB1Periph |\
                               FLYSKY_HALL_RCC_APB1Periph |\
                               EXTMODULE_RCC_APB1Periph |\
                               INTMODULE_RCC_APB1Periph |\
                               AUX_SERIAL_RCC_APB1Periph |\
                               MIXER_SCHEDULER_TIMER_RCC_APB1Periph \
                              )
#define RCC_APB2PeriphMinimum (LCD_RCC_APB2Periph)

#define RCC_APB2PeriphOther   (ADC_RCC_APB2Periph |\
                               HAPTIC_RCC_APB2Periph |\
                               AUX_SERIAL_RCC_APB2Periph |\
                               AUDIO_RCC_APB2Periph |\
                               EXTMODULE_RCC_APB2Periph \
                              )

void boardInit()
{
#if defined(SEMIHOSTING)
  initialise_monitor_handles();
#endif

#if !defined(SIMU)
  RCC_AHB1PeriphClockCmd(RCC_AHB1PeriphMinimum | RCC_AHB1PeriphOther, ENABLE);
  RCC_AHB3PeriphClockCmd(RCC_AHB3PeriphMinimum, ENABLE);
  RCC_APB1PeriphClockCmd(RCC_APB1PeriphMinimum | RCC_APB1PeriphOther, ENABLE);
  RCC_APB2PeriphClockCmd(RCC_APB2PeriphMinimum | RCC_APB2PeriphOther, ENABLE);

  // enable interrupts
  __enable_irq();
#endif

#if defined(DEBUG) && defined(AUX_SERIAL)
  auxSerialInit(UART_MODE_DEBUG, 0); // default serial mode (None if DEBUG not defined)
#endif
#if defined(DEBUG) && defined(AUX2_SERIAL)
  aux2SerialInit(UART_MODE_DEBUG, 0); // default serial mode (None if DEBUG not defined)
#endif

  TRACE("\nNV14 board started :)");
  delay_ms(10);
  TRACE("RCC->CSR = %08x", RCC->CSR);

  pwrInit();
  extModuleInit();
  battery_charge_init();
#if defined(FLYSKY_HALL_STICKS)
  flysky_hall_stick_init();
#endif
  init2MhzTimer();
  init1msTimer();
  TouchInit();

  uint32_t press_start = 0;
  uint32_t press_end = 0;

  if (UNEXPECTED_SHUTDOWN()) {
    pwrOn();
  }
  
  while (boardState == BOARD_POWER_OFF) {
    uint32_t now = get_tmr10ms();
    if (pwrPressed()) {
      press_end = now;
      if (press_start == 0) press_start = now;
      if ((now - press_start) > POWER_ON_DELAY) {
          pwrOn();
          break;
      }
    }
    else {
      uint32_t press_end_touch = press_end;
      if (touchPanelEventOccured()) {
        touchPanelRead();
        press_end_touch = get_tmr10ms();
      }
      press_start = 0;
      handle_battery_charge(press_end_touch);
      delay_ms(20);
      press_end = 0;
    }
  }

  keysInit();
  audioInit();
  // we need to initialize g_FATFS_Obj here, because it is in .ram section (because of DMA access)
  // and this section is un-initialized
  memset(&g_FATFS_Obj, 0, sizeof(g_FATFS_Obj));
  monitorInit();
  adcInit();
  backlightInit();
  lcdInit();
  usbInit();
  hapticInit();
  boardState = BOARD_STARTED;
#if defined(DEBUG)
  DBGMCU_APB1PeriphConfig(DBGMCU_IWDG_STOP|DBGMCU_TIM1_STOP|DBGMCU_TIM2_STOP|DBGMCU_TIM3_STOP|DBGMCU_TIM4_STOP|DBGMCU_TIM5_STOP|DBGMCU_TIM6_STOP|DBGMCU_TIM7_STOP|DBGMCU_TIM8_STOP|DBGMCU_TIM9_STOP|DBGMCU_TIM10_STOP|DBGMCU_TIM11_STOP|DBGMCU_TIM12_STOP|DBGMCU_TIM13_STOP|DBGMCU_TIM14_STOP, ENABLE);
#endif

}
void boardOff()
{
  lcd->drawFilledRect(0, 0, LCD_WIDTH, LCD_HEIGHT, SOLID, HEADER_BGCOLOR);
  lcdOff();

  while (pwrPressed()) {
    WDG_RESET();
  }
 
  SysTick->CTRL = 0; // turn off systick
  pwrOff();
#if !defined (SIMU)
#if defined(HAPTIC) && !defined(BOOT)
  haptic.event(AU_ERROR);
  delay_ms(50);
#endif
  while(1) {
    NVIC_SystemReset();
  }
#endif
}
