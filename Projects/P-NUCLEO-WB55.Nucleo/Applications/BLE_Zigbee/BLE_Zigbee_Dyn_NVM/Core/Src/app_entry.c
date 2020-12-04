/**
 ******************************************************************************
 * @file    app_entry.c
 * @author  MCD Application Team
 * @brief   Entry point of the Application
 ******************************************************************************
 * @attention
 *
 * <h2><center>&copy; Copyright (c) 2019 STMicroelectronics.
 * All rights reserved.</center></h2>
 *
 * This software component is licensed by ST under Ultimate Liberty license
 * SLA0044, the "License"; You may not use this file except in compliance with
 * the License. You may obtain a copy of the License at:
 *                             www.st.com/SLA0044
 *
 ******************************************************************************
 */


/* Includes ------------------------------------------------------------------*/
#include "app_common.h"
#include "main.h"
#include "app_entry.h"
#include "app_zigbee.h"
#include "app_conf.h"
#include "hw_conf.h"
#include "stm32_seq.h"
#include "stm_logging.h"
#include "dbg_trace.h"
#include "shci_tl.h"
#include "stm32_lpm.h"
#include "app_ble.h"
#include "shci.h"

/* Private includes -----------------------------------------------------------*/
/* USER CODE BEGIN Includes */

/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private defines -----------------------------------------------------------*/
/* POOL_SIZE = 2(TL_PacketHeader_t) + 258 (3(TL_EVT_HDR_SIZE) + 255(Payload size)) */
#define POOL_SIZE (CFG_TLBLE_EVT_QUEUE_LENGTH*4*DIVC(( sizeof(TL_PacketHeader_t) + TL_BLE_EVENT_FRAME_SIZE ), 4))
#define T_1S_NB_TICKS           (1*1000*1000/CFG_TS_TICK_VAL) /**< 1s */
#define T_1MS_NB_TICKS          (1*1000/CFG_TS_TICK_VAL) /**< 1s */
#define T_50MS                  (50*T_1MS_NB_TICKS)
#define T_100MS                 (100*T_1MS_NB_TICKS)
#define T_200MS                 (200*T_1MS_NB_TICKS)
#define T_2S                    (2*T_1S_NB_TICKS)
/* USER CODE BEGIN PD */

/* USER CODE END PD */
/* Private variables ---------------------------------------------------------*/

extern RTC_HandleTypeDef hrtc;  /**< RTC handler declaration */
extern uint8_t ZbStackType;     /* ZB stack type, static or dynamic */

PLACE_IN_SECTION("MB_MEM2") ALIGN(4) static uint8_t EvtPool[POOL_SIZE];
PLACE_IN_SECTION("MB_MEM2") ALIGN(4) static TL_CmdPacket_t SystemCmdBuffer;
PLACE_IN_SECTION("MB_MEM2") ALIGN(4) static uint8_t	SystemSpareEvtBuffer[sizeof(TL_PacketHeader_t) + TL_EVT_HDR_SIZE + 255];
PLACE_IN_SECTION("MB_MEM2") ALIGN(4) static uint8_t	BleSpareEvtBuffer[sizeof(TL_PacketHeader_t) + TL_EVT_HDR_SIZE + 255];

/* SELECT THE PROTOCOL THAT WILL START FIRST (BLE or ZIGBEE) */

/* As the NVM is used for data persistence, BLE has to be started first.
   Indeed, we need BLE to be running for being able to organize FLASH operations
   and respect BLE timming */
static SHCI_C2_CONCURRENT_Mode_Param_t ConcurrentMode = BLE_ENABLE;

/* Global variables ----------------------------------------------------------*/

/* Global function prototypes -----------------------------------------------*/
size_t DbgTraceWrite(int handle, const unsigned char * buf, size_t bufSize);

/* Private function prototypes -----------------------------------------------*/
static void SystemPower_Config( void );
static void Init_Debug( void );
static void APPE_SysStatusNot( SHCI_TL_CmdStatus_t status );
static void APPE_SysUserEvtRx( void * pPayload );

static void appe_Tl_Init( void );
/* USER CODE BEGIN PFP */
static void Led_Init( void );
static void Button_Init( void );
/* USER CODE END PFP */

static void Process_Start_ZB(void);


/* Functions Definition ------------------------------------------------------*/
void APPE_Init( void )
{
  SystemPower_Config(); /**< Configure the system Power Mode */
  
  HW_TS_Init(hw_ts_InitMode_Full, &hrtc); /**< Initialize the TimerServer */

  Init_Debug();

  APP_DBG("ConcurrentMode = %d", ConcurrentMode);

  /* Task to start Zigbee process */
  UTIL_SEQ_RegTask( 1<<CFG_Task_Switch_Protocol, UTIL_SEQ_RFU, Process_Start_ZB);

  /**
   * The Standby mode should not be entered before the initialization is over
   * The default state of the Low Power Manager is to allow the Standby Mode so an request is needed here
   */
  UTIL_LPM_SetOffMode(1 << CFG_LPM_APP, UTIL_LPM_DISABLE);

  Led_Init();

  Button_Init();

  appe_Tl_Init(); /**< Initialize all transport layers */

  /**
   * From now, the application is waiting for the ready event ( VS_HCI_C2_Ready )
   * received on the system channel before starting the BLE or Zigbee Stack
   * This system event is received with APPE_UserEvtRx()
   */

  return;
}

/** Scheduler tasks **/
static void Process_Start_ZB(void)
{
  if (1) {
  //if (ConcurrentMode == BLE_ENABLE) {
    APP_DBG("Process_Start_Zigbee");

    APP_ZIGBEE_Init_Step2();

  }
}

/*************************************************************
 *
 * LOCAL FUNCTIONS
 *
 *************************************************************/
static void Init_Debug( void )
{
#if (CFG_DEBUGGER_SUPPORTED == 1)
  /**
   * Keep debugger enabled while in any low power mode
   */
  HAL_DBGMCU_EnableDBGSleepMode();

  /***************** ENABLE DEBUGGER *************************************/
  LL_EXTI_EnableIT_32_63(LL_EXTI_LINE_48);
  LL_C2_EXTI_EnableIT_32_63(LL_EXTI_LINE_48);

#else

  GPIO_InitTypeDef gpio_config = {0};

  gpio_config.Pull = GPIO_NOPULL;
  gpio_config.Mode = GPIO_MODE_ANALOG;

  gpio_config.Pin = GPIO_PIN_15 | GPIO_PIN_14 | GPIO_PIN_13;
  __HAL_RCC_GPIOA_CLK_ENABLE();
  HAL_GPIO_Init(GPIOA, &gpio_config);
  __HAL_RCC_GPIOA_CLK_DISABLE();

  gpio_config.Pin = GPIO_PIN_4 | GPIO_PIN_3;
  __HAL_RCC_GPIOB_CLK_ENABLE();
  HAL_GPIO_Init(GPIOB, &gpio_config);
  __HAL_RCC_GPIOB_CLK_DISABLE();

  HAL_DBGMCU_DisableDBGSleepMode();
  HAL_DBGMCU_DisableDBGStopMode();
  HAL_DBGMCU_DisableDBGStandbyMode();

#endif /* (CFG_DEBUGGER_SUPPORTED == 1) */

#if(CFG_DEBUG_TRACE != 0)
  DbgTraceInit();
#endif

  return;
}

/**
 * @brief  Configure the system for power optimization
 *
 * @note  This API configures the system to be ready for low power mode
 *
 * @param  None
 * @retval None
 */
static void SystemPower_Config( void )
{

  /**
   * Select HSI as system clock source after Wake Up from Stop mode
   */
  LL_RCC_SetClkAfterWakeFromStop(LL_RCC_STOP_WAKEUPCLOCK_HSI);

  /* Initialize low power manager */
  UTIL_LPM_Init( );

#if (CFG_USB_INTERFACE_ENABLE != 0)
  /**
   *  Enable USB power
   */
  HAL_PWREx_EnableVddUSB();
#endif

  return;
}

static void appe_Tl_Init( void )
{
  TL_MM_Config_t tl_mm_config;
  SHCI_TL_HciInitConf_t SHci_Tl_Init_Conf;

  /**< Reference table initialization */
  TL_Init();

  /**< System channel initialization */
  UTIL_SEQ_RegTask( 1<< CFG_TASK_SYSTEM_HCI_ASYNCH_EVT_ID, UTIL_SEQ_RFU, shci_user_evt_proc );
  SHci_Tl_Init_Conf.p_cmdbuffer = (uint8_t*)&SystemCmdBuffer;
  SHci_Tl_Init_Conf.StatusNotCallBack = APPE_SysStatusNot;
  shci_init(APPE_SysUserEvtRx, (void*) &SHci_Tl_Init_Conf);

  /**< Memory Manager channel initialization */
  tl_mm_config.p_BleSpareEvtBuffer = BleSpareEvtBuffer;
  tl_mm_config.p_SystemSpareEvtBuffer = SystemSpareEvtBuffer;
  tl_mm_config.p_AsynchEvtPool = EvtPool;
  tl_mm_config.AsynchEvtPoolSize = POOL_SIZE;
  TL_MM_Init( &tl_mm_config );

  TL_Enable();

  return;
}

static void APPE_SysStatusNot( SHCI_TL_CmdStatus_t status )
{
  UNUSED(status);
  return;
}

/**
 * The type of the payload for a system user event is tSHCI_UserEvtRxParam
 * When the system event is both :
 *    - a ready event (subevtcode = SHCI_SUB_EVT_CODE_READY)
 *    - reported by the FUS (sysevt_ready_rsp == RSS_FW_RUNNING)
 * The buffer shall not be released
 * ( eg ((tSHCI_UserEvtRxParam*)pPayload)->status shall be set to SHCI_TL_UserEventFlow_Disable )
 * When the status is not filled, the buffer is released by default
 */
static void APPE_SysUserEvtRx( void * pPayload )
{
  TL_AsynchEvt_t *p_sys_event;

  p_sys_event = (TL_AsynchEvt_t*)(((tSHCI_UserEvtRxParam*)pPayload)->pckt->evtserial.evt.payload);

  switch(p_sys_event->subevtcode){
  case SHCI_SUB_EVT_CODE_READY:

    /* Traces channel initialization */
    TL_TRACES_Init( );

    if (ConcurrentMode == BLE_ENABLE) { 
      /* Start Zigbee first */
      APP_DBG("==> Start_BLE");
      APP_BLE_Init_Step1();
      APP_BLE_Init_Step2();

      APP_DBG("==> Start Zigbee");
      APP_ZIGBEE_Init_Step1();
      switch (ZbStackType) { /* Check ZB stack type */
      case INFO_STACK_TYPE_BLE_ZIGBEE_FFD_STATIC: /* Start ZB only */
        APP_DBG("==> Start Zigbee Networking");
        APP_ZIGBEE_Init_Step2();
        break;
      case INFO_STACK_TYPE_BLE_ZIGBEE_FFD_DYNAMIC:  /* Start ZB/BLE Dynamic mode */
        HAL_Delay(100);
        APP_DBG("==> Start_BLE Advertising");
        UTIL_SEQ_SetTask(1U << CFG_TASK_APP_BLE_START, CFG_SCH_PRIO_1);
        HAL_Delay(100);
        APP_DBG("==> Start Zigbee Networking");
        APP_ZIGBEE_Init_Step2();
        break;
      default:
        /* No Zigbee device supported ! */
        APP_DBG("FW Type : No ZB STACK type detected");
        break;
      }
    } else {
      /* Cannot start Zigbee first when using NVM */
      return;
    }
    
    case SHCI_SUB_EVT_CODE_CONCURRENT_802154_EVT:
      APP_ZIGBEE_WaitUntilNext_802154_Evt_cb();
      break;
  }

//  UTIL_LPM_SetOffMode(1U << CFG_LPM_APP, UTIL_LPM_ENABLE);

  return;
}

static void Led_Init( void )
{
#if (CFG_LED_SUPPORTED == 1U)
  /**
   * Leds Initialization
   */

  BSP_LED_Init(LED_BLUE);
  BSP_LED_Init(LED_GREEN);
  BSP_LED_Init(LED_RED);

  //BSP_LED_On(LED_GREEN);
#endif

  return;
}

static void Button_Init( void )
{

#if (CFG_BUTTON_SUPPORTED == 1U)
  /**
   * Button Initialization
   */

  BSP_PB_Init(BUTTON_SW1, BUTTON_MODE_EXTI);
  BSP_PB_Init(BUTTON_SW2, BUTTON_MODE_EXTI);
  BSP_PB_Init(BUTTON_SW3, BUTTON_MODE_EXTI);
#endif

  return;
}

/*************************************************************
 *
 * WRAP FUNCTIONS
 *
 *************************************************************/

void UTIL_SEQ_Idle( void )
{
#if ( CFG_LPM_SUPPORTED == 1)
  UTIL_LPM_EnterLowPower( );
#endif

  return;
}


/**
  * @brief  This function is called by the scheduler each time an event
  *         is pending.
  *
  * @param  evt_waited_bm : Event pending.
  * @retval None
  */
void UTIL_SEQ_EvtIdle( UTIL_SEQ_bm_t task_id_bm, UTIL_SEQ_bm_t evt_waited_bm )
{
  switch (evt_waited_bm) {
    case EVENT_ACK_FROM_M0_EVT:
      /* Run only the task CFG_TASK_REQUEST_FROM_M0_TO_M4 to process
      * direct requests from the M0 (e.g. ZbMalloc), but no stack notifications
      * until we're done the request to the M0. */
      UTIL_SEQ_Run((1U << CFG_TASK_REQUEST_FROM_M0_TO_M4));
      break;

    case EVENT_SYNCHRO_BYPASS_IDLE:
      UTIL_SEQ_SetEvt(EVENT_SYNCHRO_BYPASS_IDLE);
      /* Process notifications and requests from the M0 */
      UTIL_SEQ_Run((1U << CFG_TASK_NOTIFY_FROM_M0_TO_M4) | (1U << CFG_TASK_REQUEST_FROM_M0_TO_M4));
      break;

        default:
            /* default case */
            UTIL_SEQ_Run( UTIL_SEQ_DEFAULT );
            break;
    }
}

void shci_notify_asynch_evt(void* pdata)
{
  UNUSED(pdata);
  UTIL_SEQ_SetTask(1U << CFG_TASK_SYSTEM_HCI_ASYNCH_EVT_ID, CFG_SCH_PRIO_0);
  return;
}

void shci_cmd_resp_release(uint32_t flag)
{
  UNUSED(flag);
  UTIL_SEQ_SetEvt(1U << CFG_IDLEEVT_HCI_CMD_EVT_RSP_ID);
  return;
}

void shci_cmd_resp_wait(uint32_t timeout)
{
  UNUSED(timeout);
  UTIL_SEQ_WaitEvt(1U << CFG_IDLEEVT_HCI_CMD_EVT_RSP_ID);
  return;
}

/* Received trace buffer from M0 */
void TL_TRACES_EvtReceived( TL_EvtPacket_t * hcievt )
{
#if(CFG_DEBUG_TRACE != 0)
  /* Call write/print function using DMA from dbg_trace */
  /* - Cast to TL_AsynchEvt_t* to get "real" payload (without Sub Evt code 2bytes),
     - (-2) to size to remove Sub Evt Code */
  DbgTraceWrite(1U, (const unsigned char *) ((TL_AsynchEvt_t *)(hcievt->evtserial.evt.payload))->payload, hcievt->evtserial.evt.plen - 2U);
#endif /* CFG_DEBUG_TRACE */
  /* Release buffer */
  TL_MM_EvtDone( hcievt );
}
/**
  * @brief  Initialization of the trace mechanism
  * @param  None
  * @retval None
  */
#if(CFG_DEBUG_TRACE != 0)
void DbgOutputInit( void )
{
  HW_UART_Init(CFG_DEBUG_TRACE_UART);
  return;
}

/**
  * @brief  Management of the traces
  * @param  p_data : data
  * @param  size : size
  * @param  call-back :
  * @retval None
  */
void DbgOutputTraces(  uint8_t *p_data, uint16_t size, void (*cb)(void) )
{
  HW_UART_Transmit_DMA(CFG_DEBUG_TRACE_UART, p_data, size, cb);

  return;
}
#endif

/**
 * @brief This function manage the Push button action
 * @param  GPIO_Pin : GPIO pin which has been activated
 * @retval None
 */
void HAL_GPIO_EXTI_Callback( uint16_t GPIO_Pin )
{
  switch (GPIO_Pin)
  {
  case BUTTON_SW1_PIN:
//    APP_DBG("BUTTON 1 PUSHED ! : ZIGBEE/BLE MESSAGE SENDING");
//    UTIL_SEQ_SetTask(1U << CFG_TASK_BUTTON_SW1, CFG_SCH_PRIO_1);
//    //HAL_Delay(T_50MS);
//    //APP_BLE_Key_Button1_Action();
//    break;
    //if (ConcurrentMode == ZIGBEE_ENABLE) {
    if (1) {
      APP_DBG("BUTTON 1 PUSHED ! : ZIGBEE MESSAGE SENDING");
      UTIL_SEQ_SetTask(1U << CFG_TASK_BUTTON_SW1, CFG_SCH_PRIO_1);
    } else {
      APP_BLE_Key_Button1_Action();
    }
    break;

  case BUTTON_SW2_PIN:
    UTIL_SEQ_SetTask(1U << CFG_TASK_BUTTON_SW2, CFG_SCH_PRIO_1);
    break;

  case BUTTON_SW3_PIN:
    APP_DBG("BUTTON 3 PUSHED ! : START ADV BLE");

    /* Start BLE App + Advertising */
    APP_DBG("==> Start_BLE Advertising");
    UTIL_SEQ_SetTask(1U << CFG_TASK_APP_BLE_START, CFG_SCH_PRIO_1);

    break;

  default:
    break;
  }
  return;
}

/************************ (C) COPYRIGHT STMicroelectronics *****END OF FILE****/