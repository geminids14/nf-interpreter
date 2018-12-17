//
// Copyright (c) 2017 The nanoFramework project contributors
// See LICENSE file in the project root for full license information.
//

#include <ch.h>
#include <hal.h>
#include <cmsis_os.h>

#include "usbcfg.h"
#include <swo.h>
#include <CLR_Startup_Thread.h>
#include <WireProtocol_ReceiverThread.h>
#include <nanoCLR_Application.h>
#include <nanoPAL_BlockStorage.h>
#include <nanoHAL_v2.h>
#include <targetPAL.h>

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <ff.h>


// need to declare the Receiver thread here
osThreadDef(ReceiverThread, osPriorityHigh, 2048, "ReceiverThread");
// declare CLRStartup thread here 
osThreadDef(CLRStartupThread, osPriorityNormal, 4096, "CLRStartupThread"); 

// Temporary for debug of file system
extern int tiny_sprintf(char *out, const char *format, ...);
char buffer[255];

  // *************** DEMO CODE FOR FATFS *******************

/*===========================================================================*/
/* Card insertion monitor.                                                   */
/*===========================================================================*/

#define POLLING_INTERVAL                10
#define POLLING_DELAY                   10

/**
 * @brief   Card monitor timer.
 */
static virtual_timer_t tmr;

/**
 * @brief   Debounce counter.
 */
static unsigned cnt;

/**
 * @brief   Card event sources.
 */
static event_source_t inserted_event, removed_event;

/**
 * @brief   Insertion monitor timer callback function.
 *
 * @param[in] p         pointer to the @p BaseBlockDevice object
 *
 * @notapi
 */
static void tmrfunc(void *p) {
  BaseBlockDevice *bbdp = p;

  chSysLockFromISR();
  if (cnt > 0) {
    if (blkIsInserted(bbdp)) {
      if (--cnt == 0) {
        chEvtBroadcastI(&inserted_event);
      }
    }
    else
      cnt = POLLING_INTERVAL;
  }
  else {
    if (!blkIsInserted(bbdp)) {
      cnt = POLLING_INTERVAL;
      chEvtBroadcastI(&removed_event);
    }
  }
  chVTSetI(&tmr, TIME_MS2I(POLLING_DELAY), tmrfunc, bbdp);
  chSysUnlockFromISR();
}

/**
 * @brief   Polling monitor start.
 *
 * @param[in] p         pointer to an object implementing @p BaseBlockDevice
 *
 * @notapi
 */
static void tmr_init(void *p) {

  chEvtObjectInit(&inserted_event);
  chEvtObjectInit(&removed_event);
  chSysLock();
  cnt = POLLING_INTERVAL;
  chVTSetI(&tmr, TIME_MS2I(POLLING_DELAY), tmrfunc, p);
  chSysUnlock();
}

/*===========================================================================*/
/* FatFs related.                                                            */
/*===========================================================================*/

/**
 * @brief FS object.
 */
static FATFS SDC_FS;
static SDCConfig SDC_CFG;

/* FS mounted and ready.*/
static bool fs_ready = FALSE;

///* Generic large buffer.*/
//static uint8_t fbuff[1024];

/*
 * Card insertion event.
 */
static void InsertHandler(eventid_t id) {
  
  // Temporary to indicate this event being fired
  palSetLine(LINE_LED1_RED);
  
  FRESULT err;

  (void)id;
  /*
   * On insertion SDC initialization and FS mount.
   */
  if (sdcConnect(&SDCD1))
    return;

  // Temporary to indicate this event being fired
  SwoPrintString("\r\nFatFs: Initializing SD completed.\r\n");

  osDelay(1000);

  err = f_mount(&SDC_FS, "/", 1);

  if (err != FR_OK)
  {
    tiny_sprintf(buffer, "Error Mounting Drive %d", err);
    SwoPrintString(buffer);
    osDelay(1000);

    sdcDisconnect(&SDCD1);
    return;
  }
  else
  {
    SwoPrintString("\r\nFatFs: Mount completed...\r\n");

    fs_ready = TRUE;
    // Temporary: arriving here means we have an initialized and mounted SD Card
    // Indicated by a green LED
    palSetLine(LINE_LED2_GREEN);
  }

  if (fs_ready)
  {
    osDelay(1000);

    //****** Test - Create a file!
    FIL fileObj;
    err = f_open(&fileObj, "TestMessage.txt", FA_CREATE_ALWAYS);
    f_close(&fileObj);
      
      if (err != FR_OK)
    {
      tiny_sprintf(buffer, "Error Creating File %d", err);
      SwoPrintString(buffer);
    }
    else
    {
      SwoPrintString("\r\nFatFs: file created...\r\n");
    }
    //******* End Test
  }
  
}

/*
 * Card removal event.
 */
static void RemoveHandler(eventid_t id) {

  // To indicate we have ejected the sd card
  palClearLine(LINE_LED1_RED);
  
  (void)id;
  sdcDisconnect(&SDCD1);
  fs_ready = FALSE;
}

  

  // *******************************************************





//  Application entry point.
int main(void) {

  // HAL initialization, this also initializes the configured device drivers
  // and performs the board-specific initializations.
  halInit();

  // init SWO as soon as possible to make it available to output ASAP
  #if (SWO_OUTPUT == TRUE)  
  SwoInit();
  #endif

  // The kernel is initialized but not started yet, this means that
  // main() is executing with absolute priority but interrupts are already enabled.
  osKernelInitialize();

  // start watchdog
  Watchdog_Init();

  //  Initializes a serial-over-USB CDC driver.
  sduObjectInit(&SDU1);
  sduStart(&SDU1, &serusbcfg);

  // Activates the USB driver and then the USB bus pull-up on D+.
  // Note, a delay is inserted in order to not have to disconnect the cable after a reset
  usbDisconnectBus(serusbcfg.usbp);
  chThdSleepMilliseconds(1500);
  usbStart(serusbcfg.usbp, &usbcfg);
  usbConnectBus(serusbcfg.usbp);

  // create the receiver thread
  osThreadCreate(osThread(ReceiverThread), NULL);

  // CLR settings to launch CLR thread
  CLR_SETTINGS clrSettings;
  (void)memset(&clrSettings, 0, sizeof(CLR_SETTINGS));

  clrSettings.MaxContextSwitches         = 50;
  clrSettings.WaitForDebugger            = false;
  clrSettings.EnterDebuggerLoopAfterExit = true;

  // create the CLR Startup thread 
  osThreadCreate(osThread(CLRStartupThread), &clrSettings);

  // start kernel, after this main() will behave like a thread with priority osPriorityNormal
  osKernelStart();


  // *************** DEMO CODE FOR FATFS *******************
  static const evhandler_t evhndl[] = { InsertHandler, RemoveHandler };
  event_listener_t el0, el1;
  /*
   * Activates the  SDC driver 1 using default configuration.
   */

  sdcStart(&SDCD2, &SDC_CFG);

  /*
   * Activates the card insertion monitor.
   */
  tmr_init(&SDCD1);

  chEvtRegister(&inserted_event, &el0, 0);
  chEvtRegister(&removed_event, &el1, 1);

  // *******************************************************
  while (true) { 
    //osDelay(100);
    chEvtDispatch(evhndl, chEvtWaitOneTimeout(ALL_EVENTS, TIME_MS2I(500)));
  }
}
