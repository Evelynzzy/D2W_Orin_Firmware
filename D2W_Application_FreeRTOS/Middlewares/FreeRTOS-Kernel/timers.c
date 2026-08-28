#include <stdlib.h>

#define MPU_WRAPPERS_INCLUDED_FROM_API_FILE

#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "timers.h"

#if ( INCLUDE_xTimerPendFunctionCall == 1 ) && ( configUSE_TIMERS == 0 )
#error configUSE_TIMERS must be set to 1 to make the xTimerPendFunctionCall() function available.
#endif

#undef MPU_WRAPPERS_INCLUDED_FROM_API_FILE

#if ( configUSE_TIMERS == 1 )

#define tmrNO_DELAY    ( ( TickType_t ) 0U )
#define tmrMAX_TIME_BEFORE_OVERFLOW    ( ( TickType_t ) -1 )

#ifndef configTIMER_SERVICE_TASK_NAME
#define configTIMER_SERVICE_TASK_NAME    "Tmr Svc"
#endif

#if ( ( configNUMBER_OF_CORES > 1 ) && ( configUSE_CORE_AFFINITY == 1 ) )

#ifndef configTIMER_SERVICE_TASK_CORE_AFFINITY
#define configTIMER_SERVICE_TASK_CORE_AFFINITY    tskNO_AFFINITY
#endif
#endif

#define tmrSTATUS_IS_ACTIVE    ( 0x01U )
#define tmrSTATUS_IS_STATICALLY_ALLOCATED    ( 0x02U )
#define tmrSTATUS_IS_AUTORELOAD    ( 0x04U )

typedef struct tmrTimerControl
{
	const char * pcTimerName;
	ListItem_t xTimerListItem;
	TickType_t xTimerPeriodInTicks;
	void * pvTimerID;
	portTIMER_CALLBACK_ATTRIBUTE TimerCallbackFunction_t pxCallbackFunction;
	#if ( configUSE_TRACE_FACILITY == 1 )
	UBaseType_t uxTimerNumber;
	#endif
	uint8_t ucStatus;
} xTIMER;

typedef xTIMER Timer_t;

typedef struct tmrTimerParameters
{
	TickType_t xMessageValue;
	Timer_t * pxTimer;
} TimerParameter_t;

typedef struct tmrCallbackParameters
{
	portTIMER_CALLBACK_ATTRIBUTE PendedFunction_t pxCallbackFunction;
	void * pvParameter1;
	uint32_t ulParameter2;
} CallbackParameters_t;

typedef struct tmrTimerQueueMessage
{
	BaseType_t xMessageID;
	union
	{
		TimerParameter_t xTimerParameters;

		#if ( INCLUDE_xTimerPendFunctionCall == 1 )
		CallbackParameters_t xCallbackParameters;
		#endif
	} u;
} DaemonTaskMessage_t;

PRIVILEGED_DATA static List_t xActiveTimerList1;
PRIVILEGED_DATA static List_t xActiveTimerList2;
PRIVILEGED_DATA static List_t * pxCurrentTimerList;
PRIVILEGED_DATA static List_t * pxOverflowTimerList;

PRIVILEGED_DATA static QueueHandle_t xTimerQueue = NULL;
PRIVILEGED_DATA static TaskHandle_t xTimerTaskHandle = NULL;

static void prvCheckForValidListAndQueue( void ) PRIVILEGED_FUNCTION;

static portTASK_FUNCTION_PROTO( prvTimerTask, pvParameters ) PRIVILEGED_FUNCTION;

static void prvProcessReceivedCommands( void ) PRIVILEGED_FUNCTION;

static BaseType_t prvInsertTimerInActiveList( Timer_t * const pxTimer, const TickType_t xNextExpiryTime, const TickType_t xTimeNow, const TickType_t xCommandTime ) PRIVILEGED_FUNCTION;

static void prvReloadTimer( Timer_t * const pxTimer, TickType_t xExpiredTime, const TickType_t xTimeNow ) PRIVILEGED_FUNCTION;

static void prvProcessExpiredTimer( const TickType_t xNextExpireTime, const TickType_t xTimeNow ) PRIVILEGED_FUNCTION;

static void prvSwitchTimerLists( void ) PRIVILEGED_FUNCTION;

static TickType_t prvSampleTimeNow( BaseType_t * const pxTimerListsWereSwitched ) PRIVILEGED_FUNCTION;

static TickType_t prvGetNextExpireTime( BaseType_t * const pxListWasEmpty ) PRIVILEGED_FUNCTION;

static void prvProcessTimerOrBlockTask( const TickType_t xNextExpireTime, BaseType_t xListWasEmpty ) PRIVILEGED_FUNCTION;

static void prvInitialiseNewTimer( const char * const pcTimerName, const TickType_t xTimerPeriodInTicks, const BaseType_t xAutoReload, void * const pvTimerID, TimerCallbackFunction_t pxCallbackFunction, Timer_t * pxNewTimer ) PRIVILEGED_FUNCTION;

BaseType_t xTimerCreateTimerTask( void )
{
	BaseType_t xReturn = pdFAIL;

	traceENTER_xTimerCreateTimerTask();

	prvCheckForValidListAndQueue();

	if( xTimerQueue != NULL )
	{
		#if ( ( configNUMBER_OF_CORES > 1 ) && ( configUSE_CORE_AFFINITY == 1 ) )
		{
			#if ( configSUPPORT_STATIC_ALLOCATION == 1 )
			{
				StaticTask_t * pxTimerTaskTCBBuffer = NULL;
				StackType_t * pxTimerTaskStackBuffer = NULL;
				configSTACK_DEPTH_TYPE uxTimerTaskStackSize;

				vApplicationGetTimerTaskMemory( &pxTimerTaskTCBBuffer, &pxTimerTaskStackBuffer, &uxTimerTaskStackSize );
				xTimerTaskHandle = xTaskCreateStaticAffinitySet( prvTimerTask, configTIMER_SERVICE_TASK_NAME, uxTimerTaskStackSize, NULL, ( ( UBaseType_t ) configTIMER_TASK_PRIORITY ) | portPRIVILEGE_BIT, pxTimerTaskStackBuffer, pxTimerTaskTCBBuffer, configTIMER_SERVICE_TASK_CORE_AFFINITY );

				if( xTimerTaskHandle != NULL )
				{
					xReturn = pdPASS;
				}
			}
			#else
			{
				xReturn = xTaskCreateAffinitySet( prvTimerTask, configTIMER_SERVICE_TASK_NAME, configTIMER_TASK_STACK_DEPTH, NULL, ( ( UBaseType_t ) configTIMER_TASK_PRIORITY ) | portPRIVILEGE_BIT, configTIMER_SERVICE_TASK_CORE_AFFINITY, &xTimerTaskHandle );
			}
			#endif
		}
		#else
		{
			#if ( configSUPPORT_STATIC_ALLOCATION == 1 )
			{
				StaticTask_t * pxTimerTaskTCBBuffer = NULL;
				StackType_t * pxTimerTaskStackBuffer = NULL;
				configSTACK_DEPTH_TYPE uxTimerTaskStackSize;

				vApplicationGetTimerTaskMemory( &pxTimerTaskTCBBuffer, &pxTimerTaskStackBuffer, &uxTimerTaskStackSize );
				xTimerTaskHandle = xTaskCreateStatic( prvTimerTask, configTIMER_SERVICE_TASK_NAME, uxTimerTaskStackSize, NULL, ( ( UBaseType_t ) configTIMER_TASK_PRIORITY ) | portPRIVILEGE_BIT, pxTimerTaskStackBuffer, pxTimerTaskTCBBuffer );

				if( xTimerTaskHandle != NULL )
				{
					xReturn = pdPASS;
				}
			}
			#else
			{
				xReturn = xTaskCreate( prvTimerTask, configTIMER_SERVICE_TASK_NAME, configTIMER_TASK_STACK_DEPTH, NULL, ( ( UBaseType_t ) configTIMER_TASK_PRIORITY ) | portPRIVILEGE_BIT, &xTimerTaskHandle );
			}
			#endif
		}
		#endif
	}
	else
	{
		mtCOVERAGE_TEST_MARKER();
	}

	configASSERT( xReturn );

	traceRETURN_xTimerCreateTimerTask( xReturn );

	return xReturn;
}

#if ( configSUPPORT_DYNAMIC_ALLOCATION == 1 )

TimerHandle_t xTimerCreate( const char * const pcTimerName, const TickType_t xTimerPeriodInTicks, const BaseType_t xAutoReload, void * const pvTimerID, TimerCallbackFunction_t pxCallbackFunction )
{
	Timer_t * pxNewTimer;

	traceENTER_xTimerCreate( pcTimerName, xTimerPeriodInTicks, xAutoReload, pvTimerID, pxCallbackFunction );

	pxNewTimer = ( Timer_t * ) pvPortMalloc( sizeof( Timer_t ) );

	if( pxNewTimer != NULL )
	{
		pxNewTimer->ucStatus = 0x00;
		prvInitialiseNewTimer( pcTimerName, xTimerPeriodInTicks, xAutoReload, pvTimerID, pxCallbackFunction, pxNewTimer );
	}

	traceRETURN_xTimerCreate( pxNewTimer );

	return pxNewTimer;
}

#endif

#if ( configSUPPORT_STATIC_ALLOCATION == 1 )

TimerHandle_t xTimerCreateStatic( const char * const pcTimerName, const TickType_t xTimerPeriodInTicks, const BaseType_t xAutoReload, void * const pvTimerID, TimerCallbackFunction_t pxCallbackFunction, StaticTimer_t * pxTimerBuffer )
{
	Timer_t * pxNewTimer;

	traceENTER_xTimerCreateStatic( pcTimerName, xTimerPeriodInTicks, xAutoReload, pvTimerID, pxCallbackFunction, pxTimerBuffer );

	#if ( configASSERT_DEFINED == 1 )
	{
		volatile size_t xSize = sizeof( StaticTimer_t );
		configASSERT( xSize == sizeof( Timer_t ) );
		( void ) xSize;
	}
	#endif

	configASSERT( pxTimerBuffer );

	pxNewTimer = ( Timer_t * ) pxTimerBuffer;

	if( pxNewTimer != NULL )
	{
		pxNewTimer->ucStatus = ( uint8_t ) tmrSTATUS_IS_STATICALLY_ALLOCATED;

		prvInitialiseNewTimer( pcTimerName, xTimerPeriodInTicks, xAutoReload, pvTimerID, pxCallbackFunction, pxNewTimer );
	}

	traceRETURN_xTimerCreateStatic( pxNewTimer );

	return pxNewTimer;
}

#endif

static void prvInitialiseNewTimer( const char * const pcTimerName, const TickType_t xTimerPeriodInTicks, const BaseType_t xAutoReload, void * const pvTimerID, TimerCallbackFunction_t pxCallbackFunction, Timer_t * pxNewTimer )
{
	configASSERT( ( xTimerPeriodInTicks > 0 ) );

	prvCheckForValidListAndQueue();

	pxNewTimer->pcTimerName = pcTimerName;
	pxNewTimer->xTimerPeriodInTicks = xTimerPeriodInTicks;
	pxNewTimer->pvTimerID = pvTimerID;
	pxNewTimer->pxCallbackFunction = pxCallbackFunction;
	vListInitialiseItem( &( pxNewTimer->xTimerListItem ) );

	if( xAutoReload != pdFALSE )
	{
		pxNewTimer->ucStatus |= ( uint8_t ) tmrSTATUS_IS_AUTORELOAD;
	}

	traceTIMER_CREATE( pxNewTimer );
}

BaseType_t xTimerGenericCommandFromTask( TimerHandle_t xTimer, const BaseType_t xCommandID, const TickType_t xOptionalValue, BaseType_t * const pxHigherPriorityTaskWoken, const TickType_t xTicksToWait )
{
	BaseType_t xReturn = pdFAIL;
	DaemonTaskMessage_t xMessage;

	( void ) pxHigherPriorityTaskWoken;

	traceENTER_xTimerGenericCommandFromTask( xTimer, xCommandID, xOptionalValue, pxHigherPriorityTaskWoken, xTicksToWait );

	configASSERT( xTimer );

	if( xTimerQueue != NULL )
	{
		xMessage.xMessageID = xCommandID;
		xMessage.u.xTimerParameters.xMessageValue = xOptionalValue;
		xMessage.u.xTimerParameters.pxTimer = xTimer;

		configASSERT( xCommandID < tmrFIRST_FROM_ISR_COMMAND );

		if( xCommandID < tmrFIRST_FROM_ISR_COMMAND )
		{
			if( xTaskGetSchedulerState() == taskSCHEDULER_RUNNING )
			{
				xReturn = xQueueSendToBack( xTimerQueue, &xMessage, xTicksToWait );
			}
			else
			{
				xReturn = xQueueSendToBack( xTimerQueue, &xMessage, tmrNO_DELAY );
			}
		}

		traceTIMER_COMMAND_SEND( xTimer, xCommandID, xOptionalValue, xReturn );
	}
	else
	{
		mtCOVERAGE_TEST_MARKER();
	}

	traceRETURN_xTimerGenericCommandFromTask( xReturn );

	return xReturn;
}

BaseType_t xTimerGenericCommandFromISR( TimerHandle_t xTimer, const BaseType_t xCommandID, const TickType_t xOptionalValue, BaseType_t * const pxHigherPriorityTaskWoken, const TickType_t xTicksToWait )
{
	BaseType_t xReturn = pdFAIL;
	DaemonTaskMessage_t xMessage;

	( void ) xTicksToWait;

	traceENTER_xTimerGenericCommandFromISR( xTimer, xCommandID, xOptionalValue, pxHigherPriorityTaskWoken, xTicksToWait );

	configASSERT( xTimer );

	if( xTimerQueue != NULL )
	{
		xMessage.xMessageID = xCommandID;
		xMessage.u.xTimerParameters.xMessageValue = xOptionalValue;
		xMessage.u.xTimerParameters.pxTimer = xTimer;

		configASSERT( xCommandID >= tmrFIRST_FROM_ISR_COMMAND );

		if( xCommandID >= tmrFIRST_FROM_ISR_COMMAND )
		{
			xReturn = xQueueSendToBackFromISR( xTimerQueue, &xMessage, pxHigherPriorityTaskWoken );
		}

		traceTIMER_COMMAND_SEND( xTimer, xCommandID, xOptionalValue, xReturn );
	}
	else
	{
		mtCOVERAGE_TEST_MARKER();
	}

	traceRETURN_xTimerGenericCommandFromISR( xReturn );

	return xReturn;
}

TaskHandle_t xTimerGetTimerDaemonTaskHandle( void )
{
	traceENTER_xTimerGetTimerDaemonTaskHandle();

	configASSERT( ( xTimerTaskHandle != NULL ) );

	traceRETURN_xTimerGetTimerDaemonTaskHandle( xTimerTaskHandle );

	return xTimerTaskHandle;
}

TickType_t xTimerGetPeriod( TimerHandle_t xTimer )
{
	Timer_t * pxTimer = xTimer;

	traceENTER_xTimerGetPeriod( xTimer );

	configASSERT( xTimer );

	traceRETURN_xTimerGetPeriod( pxTimer->xTimerPeriodInTicks );

	return pxTimer->xTimerPeriodInTicks;
}

void vTimerSetReloadMode( TimerHandle_t xTimer, const BaseType_t xAutoReload )
{
	Timer_t * pxTimer = xTimer;

	traceENTER_vTimerSetReloadMode( xTimer, xAutoReload );

	configASSERT( xTimer );
	taskENTER_CRITICAL();
	{
		if( xAutoReload != pdFALSE )
		{
			pxTimer->ucStatus |= ( uint8_t ) tmrSTATUS_IS_AUTORELOAD;
		}
		else
		{
			pxTimer->ucStatus &= ( ( uint8_t ) ~tmrSTATUS_IS_AUTORELOAD );
		}
	}
	taskEXIT_CRITICAL();

	traceRETURN_vTimerSetReloadMode();
}

BaseType_t xTimerGetReloadMode( TimerHandle_t xTimer )
{
	Timer_t * pxTimer = xTimer;
	BaseType_t xReturn;

	traceENTER_xTimerGetReloadMode( xTimer );

	configASSERT( xTimer );
	taskENTER_CRITICAL();
	{
		if( ( pxTimer->ucStatus & tmrSTATUS_IS_AUTORELOAD ) == 0U )
		{
			xReturn = pdFALSE;
		}
		else
		{
			xReturn = pdTRUE;
		}
	}
	taskEXIT_CRITICAL();

	traceRETURN_xTimerGetReloadMode( xReturn );

	return xReturn;
}

UBaseType_t uxTimerGetReloadMode( TimerHandle_t xTimer )
{
	UBaseType_t uxReturn;

	traceENTER_uxTimerGetReloadMode( xTimer );

	uxReturn = ( UBaseType_t ) xTimerGetReloadMode( xTimer );

	traceRETURN_uxTimerGetReloadMode( uxReturn );

	return uxReturn;
}

TickType_t xTimerGetExpiryTime( TimerHandle_t xTimer )
{
	Timer_t * pxTimer = xTimer;
	TickType_t xReturn;

	traceENTER_xTimerGetExpiryTime( xTimer );

	configASSERT( xTimer );
	xReturn = listGET_LIST_ITEM_VALUE( &( pxTimer->xTimerListItem ) );

	traceRETURN_xTimerGetExpiryTime( xReturn );

	return xReturn;
}

#if ( configSUPPORT_STATIC_ALLOCATION == 1 )
BaseType_t xTimerGetStaticBuffer( TimerHandle_t xTimer, StaticTimer_t ** ppxTimerBuffer )
{
	BaseType_t xReturn;
	Timer_t * pxTimer = xTimer;

	traceENTER_xTimerGetStaticBuffer( xTimer, ppxTimerBuffer );

	configASSERT( ppxTimerBuffer != NULL );

	if( ( pxTimer->ucStatus & tmrSTATUS_IS_STATICALLY_ALLOCATED ) != 0U )
	{
		*ppxTimerBuffer = ( StaticTimer_t * ) pxTimer;
		xReturn = pdTRUE;
	}
	else
	{
		xReturn = pdFALSE;
	}

	traceRETURN_xTimerGetStaticBuffer( xReturn );

	return xReturn;
}
#endif

const char * pcTimerGetName( TimerHandle_t xTimer )
{
	Timer_t * pxTimer = xTimer;

	traceENTER_pcTimerGetName( xTimer );

	configASSERT( xTimer );

	traceRETURN_pcTimerGetName( pxTimer->pcTimerName );

	return pxTimer->pcTimerName;
}

static void prvReloadTimer( Timer_t * const pxTimer, TickType_t xExpiredTime, const TickType_t xTimeNow )
{
	while( prvInsertTimerInActiveList( pxTimer, ( xExpiredTime + pxTimer->xTimerPeriodInTicks ), xTimeNow, xExpiredTime ) != pdFALSE )
	{
		xExpiredTime += pxTimer->xTimerPeriodInTicks;

		traceTIMER_EXPIRED( pxTimer );
		pxTimer->pxCallbackFunction( ( TimerHandle_t ) pxTimer );
	}
}

static void prvProcessExpiredTimer( const TickType_t xNextExpireTime, const TickType_t xTimeNow )
{
	Timer_t * const pxTimer = ( Timer_t * ) listGET_OWNER_OF_HEAD_ENTRY( pxCurrentTimerList );

	( void ) uxListRemove( &( pxTimer->xTimerListItem ) );

	if( ( pxTimer->ucStatus & tmrSTATUS_IS_AUTORELOAD ) != 0U )
	{
		prvReloadTimer( pxTimer, xNextExpireTime, xTimeNow );
	}
	else
	{
		pxTimer->ucStatus &= ( ( uint8_t ) ~tmrSTATUS_IS_ACTIVE );
	}

	traceTIMER_EXPIRED( pxTimer );
	pxTimer->pxCallbackFunction( ( TimerHandle_t ) pxTimer );
}

static portTASK_FUNCTION( prvTimerTask, pvParameters )
{
	TickType_t xNextExpireTime;
	BaseType_t xListWasEmpty;

	( void ) pvParameters;

	#if ( configUSE_DAEMON_TASK_STARTUP_HOOK == 1 )
	{
		vApplicationDaemonTaskStartupHook();
	}
	#endif

	for( ; configCONTROL_INFINITE_LOOP(); )
	{
		xNextExpireTime = prvGetNextExpireTime( &xListWasEmpty );

		prvProcessTimerOrBlockTask( xNextExpireTime, xListWasEmpty );

		prvProcessReceivedCommands();
	}
}

static void prvProcessTimerOrBlockTask( const TickType_t xNextExpireTime, BaseType_t xListWasEmpty )
{
	TickType_t xTimeNow;
	BaseType_t xTimerListsWereSwitched;

	vTaskSuspendAll();
	{
		xTimeNow = prvSampleTimeNow( &xTimerListsWereSwitched );

		if( xTimerListsWereSwitched == pdFALSE )
		{
			if( ( xListWasEmpty == pdFALSE ) && ( xNextExpireTime <= xTimeNow ) )
			{
				( void ) xTaskResumeAll();
				prvProcessExpiredTimer( xNextExpireTime, xTimeNow );
			}
			else
			{
				if( xListWasEmpty != pdFALSE )
				{
					xListWasEmpty = listLIST_IS_EMPTY( pxOverflowTimerList );
				}

				vQueueWaitForMessageRestricted( xTimerQueue, ( xNextExpireTime - xTimeNow ), xListWasEmpty );

				if( xTaskResumeAll() == pdFALSE )
				{
					taskYIELD_WITHIN_API();
				}
				else
				{
					mtCOVERAGE_TEST_MARKER();
				}
			}
		}
		else
		{
			( void ) xTaskResumeAll();
		}
	}
}

static TickType_t prvGetNextExpireTime( BaseType_t * const pxListWasEmpty )
{
	TickType_t xNextExpireTime;

	*pxListWasEmpty = listLIST_IS_EMPTY( pxCurrentTimerList );

	if( *pxListWasEmpty == pdFALSE )
	{
		xNextExpireTime = listGET_ITEM_VALUE_OF_HEAD_ENTRY( pxCurrentTimerList );
	}
	else
	{
		xNextExpireTime = ( TickType_t ) 0U;
	}

	return xNextExpireTime;
}

static TickType_t prvSampleTimeNow( BaseType_t * const pxTimerListsWereSwitched )
{
	TickType_t xTimeNow;
	PRIVILEGED_DATA static TickType_t xLastTime = ( TickType_t ) 0U;

	xTimeNow = xTaskGetTickCount();

	if( xTimeNow < xLastTime )
	{
		prvSwitchTimerLists();
		*pxTimerListsWereSwitched = pdTRUE;
	}
	else
	{
		*pxTimerListsWereSwitched = pdFALSE;
	}

	xLastTime = xTimeNow;

	return xTimeNow;
}

static BaseType_t prvInsertTimerInActiveList( Timer_t * const pxTimer, const TickType_t xNextExpiryTime, const TickType_t xTimeNow, const TickType_t xCommandTime )
{
	BaseType_t xProcessTimerNow = pdFALSE;

	listSET_LIST_ITEM_VALUE( &( pxTimer->xTimerListItem ), xNextExpiryTime );
	listSET_LIST_ITEM_OWNER( &( pxTimer->xTimerListItem ), pxTimer );

	if( xNextExpiryTime <= xTimeNow )
	{
		if( ( ( TickType_t ) ( xTimeNow - xCommandTime ) ) >= pxTimer->xTimerPeriodInTicks )
		{
			xProcessTimerNow = pdTRUE;
		}
		else
		{
			vListInsert( pxOverflowTimerList, &( pxTimer->xTimerListItem ) );
		}
	}
	else
	{
		if( ( xTimeNow < xCommandTime ) && ( xNextExpiryTime >= xCommandTime ) )
		{
			xProcessTimerNow = pdTRUE;
		}
		else
		{
			vListInsert( pxCurrentTimerList, &( pxTimer->xTimerListItem ) );
		}
	}

	return xProcessTimerNow;
}

static void prvProcessReceivedCommands( void )
{
	DaemonTaskMessage_t xMessage = { 0 };
	Timer_t * pxTimer;
	BaseType_t xTimerListsWereSwitched;
	TickType_t xTimeNow;

	while( xQueueReceive( xTimerQueue, &xMessage, tmrNO_DELAY ) != pdFAIL )
	{
		#if ( INCLUDE_xTimerPendFunctionCall == 1 )
		{
			if( xMessage.xMessageID < ( BaseType_t ) 0 )
			{
				const CallbackParameters_t * const pxCallback = &( xMessage.u.xCallbackParameters );

				configASSERT( pxCallback );

				pxCallback->pxCallbackFunction( pxCallback->pvParameter1, pxCallback->ulParameter2 );
			}
			else
			{
				mtCOVERAGE_TEST_MARKER();
			}
		}
		#endif

		if( xMessage.xMessageID >= ( BaseType_t ) 0 )
		{
			pxTimer = xMessage.u.xTimerParameters.pxTimer;

			if( listIS_CONTAINED_WITHIN( NULL, &( pxTimer->xTimerListItem ) ) == pdFALSE )
			{
				( void ) uxListRemove( &( pxTimer->xTimerListItem ) );
			}
			else
			{
				mtCOVERAGE_TEST_MARKER();
			}

			traceTIMER_COMMAND_RECEIVED( pxTimer, xMessage.xMessageID, xMessage.u.xTimerParameters.xMessageValue );

			xTimeNow = prvSampleTimeNow( &xTimerListsWereSwitched );

			switch( xMessage.xMessageID )
			{
				case tmrCOMMAND_START:
				case tmrCOMMAND_START_FROM_ISR:
				case tmrCOMMAND_RESET:
				case tmrCOMMAND_RESET_FROM_ISR:

					pxTimer->ucStatus |= ( uint8_t ) tmrSTATUS_IS_ACTIVE;

					if( prvInsertTimerInActiveList( pxTimer, xMessage.u.xTimerParameters.xMessageValue + pxTimer->xTimerPeriodInTicks, xTimeNow, xMessage.u.xTimerParameters.xMessageValue ) != pdFALSE )
					{
						if( ( pxTimer->ucStatus & tmrSTATUS_IS_AUTORELOAD ) != 0U )
						{
							prvReloadTimer( pxTimer, xMessage.u.xTimerParameters.xMessageValue + pxTimer->xTimerPeriodInTicks, xTimeNow );
						}
						else
						{
							pxTimer->ucStatus &= ( ( uint8_t ) ~tmrSTATUS_IS_ACTIVE );
						}

						traceTIMER_EXPIRED( pxTimer );
						pxTimer->pxCallbackFunction( ( TimerHandle_t ) pxTimer );
					}
					else
					{
						mtCOVERAGE_TEST_MARKER();
					}

					break;

				case tmrCOMMAND_STOP:
				case tmrCOMMAND_STOP_FROM_ISR:

					pxTimer->ucStatus &= ( ( uint8_t ) ~tmrSTATUS_IS_ACTIVE );
					break;

				case tmrCOMMAND_CHANGE_PERIOD:
				case tmrCOMMAND_CHANGE_PERIOD_FROM_ISR:
					pxTimer->ucStatus |= ( uint8_t ) tmrSTATUS_IS_ACTIVE;
					pxTimer->xTimerPeriodInTicks = xMessage.u.xTimerParameters.xMessageValue;
					configASSERT( ( pxTimer->xTimerPeriodInTicks > 0 ) );

					( void ) prvInsertTimerInActiveList( pxTimer, ( xTimeNow + pxTimer->xTimerPeriodInTicks ), xTimeNow, xTimeNow );
					break;

				case tmrCOMMAND_DELETE:
					#if ( configSUPPORT_DYNAMIC_ALLOCATION == 1 )
					{
						if( ( pxTimer->ucStatus & tmrSTATUS_IS_STATICALLY_ALLOCATED ) == ( uint8_t ) 0 )
						{
							vPortFree( pxTimer );
						}
						else
						{
							pxTimer->ucStatus &= ( ( uint8_t ) ~tmrSTATUS_IS_ACTIVE );
						}
					}
					#else
					{
						pxTimer->ucStatus &= ( ( uint8_t ) ~tmrSTATUS_IS_ACTIVE );
					}
					#endif
					break;

				default:

					break;
			}
		}
	}
}

static void prvSwitchTimerLists( void )
{
	TickType_t xNextExpireTime;
	List_t * pxTemp;

	while( listLIST_IS_EMPTY( pxCurrentTimerList ) == pdFALSE )
	{
		xNextExpireTime = listGET_ITEM_VALUE_OF_HEAD_ENTRY( pxCurrentTimerList );

		prvProcessExpiredTimer( xNextExpireTime, tmrMAX_TIME_BEFORE_OVERFLOW );
	}

	pxTemp = pxCurrentTimerList;
	pxCurrentTimerList = pxOverflowTimerList;
	pxOverflowTimerList = pxTemp;
}

static void prvCheckForValidListAndQueue( void )
{
	taskENTER_CRITICAL();
	{
		if( xTimerQueue == NULL )
		{
			vListInitialise( &xActiveTimerList1 );
			vListInitialise( &xActiveTimerList2 );
			pxCurrentTimerList = &xActiveTimerList1;
			pxOverflowTimerList = &xActiveTimerList2;

			#if ( configSUPPORT_STATIC_ALLOCATION == 1 )
			{
				PRIVILEGED_DATA static StaticQueue_t xStaticTimerQueue;
				PRIVILEGED_DATA static uint8_t ucStaticTimerQueueStorage[ ( size_t ) configTIMER_QUEUE_LENGTH * sizeof( DaemonTaskMessage_t ) ];

				xTimerQueue = xQueueCreateStatic( ( UBaseType_t ) configTIMER_QUEUE_LENGTH, ( UBaseType_t ) sizeof( DaemonTaskMessage_t ), &( ucStaticTimerQueueStorage[ 0 ] ), &xStaticTimerQueue );
			}
			#else
			{
				xTimerQueue = xQueueCreate( ( UBaseType_t ) configTIMER_QUEUE_LENGTH, ( UBaseType_t ) sizeof( DaemonTaskMessage_t ) );
			}
			#endif

			#if ( configQUEUE_REGISTRY_SIZE > 0 )
			{
				if( xTimerQueue != NULL )
				{
					vQueueAddToRegistry( xTimerQueue, "TmrQ" );
				}
				else
				{
					mtCOVERAGE_TEST_MARKER();
				}
			}
			#endif
		}
		else
		{
			mtCOVERAGE_TEST_MARKER();
		}
	}
	taskEXIT_CRITICAL();
}

BaseType_t xTimerIsTimerActive( TimerHandle_t xTimer )
{
	BaseType_t xReturn;
	Timer_t * pxTimer = xTimer;

	traceENTER_xTimerIsTimerActive( xTimer );

	configASSERT( xTimer );

	taskENTER_CRITICAL();
	{
		if( ( pxTimer->ucStatus & tmrSTATUS_IS_ACTIVE ) == 0U )
		{
			xReturn = pdFALSE;
		}
		else
		{
			xReturn = pdTRUE;
		}
	}
	taskEXIT_CRITICAL();

	traceRETURN_xTimerIsTimerActive( xReturn );

	return xReturn;
}

void * pvTimerGetTimerID( const TimerHandle_t xTimer )
{
	Timer_t * const pxTimer = xTimer;
	void * pvReturn;

	traceENTER_pvTimerGetTimerID( xTimer );

	configASSERT( xTimer );

	taskENTER_CRITICAL();
	{
		pvReturn = pxTimer->pvTimerID;
	}
	taskEXIT_CRITICAL();

	traceRETURN_pvTimerGetTimerID( pvReturn );

	return pvReturn;
}

void vTimerSetTimerID( TimerHandle_t xTimer, void * pvNewID )
{
	Timer_t * const pxTimer = xTimer;

	traceENTER_vTimerSetTimerID( xTimer, pvNewID );

	configASSERT( xTimer );

	taskENTER_CRITICAL();
	{
		pxTimer->pvTimerID = pvNewID;
	}
	taskEXIT_CRITICAL();

	traceRETURN_vTimerSetTimerID();
}

#if ( INCLUDE_xTimerPendFunctionCall == 1 )

BaseType_t xTimerPendFunctionCallFromISR( PendedFunction_t xFunctionToPend, void * pvParameter1, uint32_t ulParameter2, BaseType_t * pxHigherPriorityTaskWoken )
{
	DaemonTaskMessage_t xMessage;
	BaseType_t xReturn;

	traceENTER_xTimerPendFunctionCallFromISR( xFunctionToPend, pvParameter1, ulParameter2, pxHigherPriorityTaskWoken );

	xMessage.xMessageID = tmrCOMMAND_EXECUTE_CALLBACK_FROM_ISR;
	xMessage.u.xCallbackParameters.pxCallbackFunction = xFunctionToPend;
	xMessage.u.xCallbackParameters.pvParameter1 = pvParameter1;
	xMessage.u.xCallbackParameters.ulParameter2 = ulParameter2;

	xReturn = xQueueSendFromISR( xTimerQueue, &xMessage, pxHigherPriorityTaskWoken );

	tracePEND_FUNC_CALL_FROM_ISR( xFunctionToPend, pvParameter1, ulParameter2, xReturn );
	traceRETURN_xTimerPendFunctionCallFromISR( xReturn );

	return xReturn;
}

#endif

#if ( INCLUDE_xTimerPendFunctionCall == 1 )

BaseType_t xTimerPendFunctionCall( PendedFunction_t xFunctionToPend, void * pvParameter1, uint32_t ulParameter2, TickType_t xTicksToWait )
{
	DaemonTaskMessage_t xMessage;
	BaseType_t xReturn;

	traceENTER_xTimerPendFunctionCall( xFunctionToPend, pvParameter1, ulParameter2, xTicksToWait );

	configASSERT( xTimerQueue );

	xMessage.xMessageID = tmrCOMMAND_EXECUTE_CALLBACK;
	xMessage.u.xCallbackParameters.pxCallbackFunction = xFunctionToPend;
	xMessage.u.xCallbackParameters.pvParameter1 = pvParameter1;
	xMessage.u.xCallbackParameters.ulParameter2 = ulParameter2;

	xReturn = xQueueSendToBack( xTimerQueue, &xMessage, xTicksToWait );

	tracePEND_FUNC_CALL( xFunctionToPend, pvParameter1, ulParameter2, xReturn );
	traceRETURN_xTimerPendFunctionCall( xReturn );

	return xReturn;
}

#endif

#if ( configUSE_TRACE_FACILITY == 1 )

UBaseType_t uxTimerGetTimerNumber( TimerHandle_t xTimer )
{
	traceENTER_uxTimerGetTimerNumber( xTimer );

	traceRETURN_uxTimerGetTimerNumber( ( ( Timer_t * ) xTimer )->uxTimerNumber );

	return ( ( Timer_t * ) xTimer )->uxTimerNumber;
}

#endif

#if ( configUSE_TRACE_FACILITY == 1 )

void vTimerSetTimerNumber( TimerHandle_t xTimer, UBaseType_t uxTimerNumber )
{
	traceENTER_vTimerSetTimerNumber( xTimer, uxTimerNumber );

	( ( Timer_t * ) xTimer )->uxTimerNumber = uxTimerNumber;

	traceRETURN_vTimerSetTimerNumber();
}

#endif

void vTimerResetState( void )
{
	xTimerQueue = NULL;
	xTimerTaskHandle = NULL;
}

#endif
