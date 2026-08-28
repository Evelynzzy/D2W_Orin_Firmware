#include <stdlib.h>

#define MPU_WRAPPERS_INCLUDED_FROM_API_FILE

#include "FreeRTOS.h"
#include "task.h"
#include "timers.h"
#include "event_groups.h"

#undef MPU_WRAPPERS_INCLUDED_FROM_API_FILE

#if ( configUSE_EVENT_GROUPS == 1 )
	typedef struct EventGroupDef_t
	{
		EventBits_t uxEventBits;
		List_t xTasksWaitingForBits; 

		#if ( configUSE_TRACE_FACILITY == 1 )
			UBaseType_t uxEventGroupNumber;
		#endif

		#if ( ( configSUPPORT_STATIC_ALLOCATION == 1 ) && ( configSUPPORT_DYNAMIC_ALLOCATION == 1 ) )
			uint8_t ucStaticallyAllocated; 
		#endif
	} EventGroup_t;


	static BaseType_t prvTestWaitCondition( const EventBits_t uxCurrentEventBits, const EventBits_t uxBitsToWaitFor, const BaseType_t xWaitForAllBits ) PRIVILEGED_FUNCTION;
	#if ( configSUPPORT_STATIC_ALLOCATION == 1 )
		EventGroupHandle_t xEventGroupCreateStatic( StaticEventGroup_t * pxEventGroupBuffer )
		{
			EventGroup_t * pxEventBits;
			traceENTER_xEventGroupCreateStatic( pxEventGroupBuffer );
			configASSERT( pxEventGroupBuffer );

			#if ( configASSERT_DEFINED == 1 )
			{
				volatile size_t xSize = sizeof( StaticEventGroup_t );
				configASSERT( xSize == sizeof( EventGroup_t ) );
			}
			#endif 

			pxEventBits = ( EventGroup_t * ) pxEventGroupBuffer;

			if( pxEventBits != NULL )
			{
				pxEventBits->uxEventBits = 0;
				vListInitialise( &( pxEventBits->xTasksWaitingForBits ) );

				#if ( configSUPPORT_DYNAMIC_ALLOCATION == 1 )
				{
						pxEventBits->ucStaticallyAllocated = pdTRUE;
				}
				#endif

				traceEVENT_GROUP_CREATE( pxEventBits );
			}
			else
			{
				traceEVENT_GROUP_CREATE_FAILED();
			}

			traceRETURN_xEventGroupCreateStatic( pxEventBits );
			return pxEventBits;
		}
	#endif 

	#if ( configSUPPORT_DYNAMIC_ALLOCATION == 1 )
		EventGroupHandle_t xEventGroupCreate( void )
		{
			EventGroup_t * pxEventBits;

			traceENTER_xEventGroupCreate();

			pxEventBits = ( EventGroup_t * ) pvPortMalloc( sizeof( EventGroup_t ) );

			if( pxEventBits != NULL )
			{
				pxEventBits->uxEventBits = 0;
				vListInitialise( &( pxEventBits->xTasksWaitingForBits ) );

				#if ( configSUPPORT_STATIC_ALLOCATION == 1 )
				{
					pxEventBits->ucStaticallyAllocated = pdFALSE;
				}
				#endif 

				traceEVENT_GROUP_CREATE( pxEventBits );
			}
			else
			{
				traceEVENT_GROUP_CREATE_FAILED();
			}

			traceRETURN_xEventGroupCreate( pxEventBits );
			return pxEventBits;
		}
	#endif 

	EventBits_t xEventGroupSync( EventGroupHandle_t xEventGroup, const EventBits_t uxBitsToSet, const EventBits_t uxBitsToWaitFor, TickType_t xTicksToWait )
	{
		EventBits_t uxOriginalBitValue, uxReturn;
		EventGroup_t * pxEventBits = xEventGroup;
		BaseType_t xAlreadyYielded;
		BaseType_t xTimeoutOccurred = pdFALSE;

		traceENTER_xEventGroupSync( xEventGroup, uxBitsToSet, uxBitsToWaitFor, xTicksToWait );

		configASSERT( ( uxBitsToWaitFor & eventEVENT_BITS_CONTROL_BYTES ) == 0 );
		configASSERT( uxBitsToWaitFor != 0 );
		#if ( ( INCLUDE_xTaskGetSchedulerState == 1 ) || ( configUSE_TIMERS == 1 ) )
		{
			configASSERT( !( ( xTaskGetSchedulerState() == taskSCHEDULER_SUSPENDED ) && ( xTicksToWait != 0 ) ) );
		}
		#endif

		vTaskSuspendAll();
		{
			uxOriginalBitValue = pxEventBits->uxEventBits;
			( void ) xEventGroupSetBits( xEventGroup, uxBitsToSet );

			if( ( ( uxOriginalBitValue | uxBitsToSet ) & uxBitsToWaitFor ) == uxBitsToWaitFor )
			{
				uxReturn = ( uxOriginalBitValue | uxBitsToSet );
				pxEventBits->uxEventBits &= ~uxBitsToWaitFor;
				xTicksToWait = 0;
			}
			else
			{
				if( xTicksToWait != ( TickType_t ) 0 )
				{
					traceEVENT_GROUP_SYNC_BLOCK( xEventGroup, uxBitsToSet, uxBitsToWaitFor );
					vTaskPlaceOnUnorderedEventList( &( pxEventBits->xTasksWaitingForBits ), ( uxBitsToWaitFor | eventCLEAR_EVENTS_ON_EXIT_BIT | eventWAIT_FOR_ALL_BITS ), xTicksToWait );
					uxReturn = 0;
				}
				else
				{
					uxReturn = pxEventBits->uxEventBits;
					xTimeoutOccurred = pdTRUE;
				}
			}
		}
		xAlreadyYielded = xTaskResumeAll();

		if( xTicksToWait != ( TickType_t ) 0 )
		{
			if( xAlreadyYielded == pdFALSE )
			{
				taskYIELD_WITHIN_API();
			}
			else
			{
				mtCOVERAGE_TEST_MARKER();
			}

			uxReturn = uxTaskResetEventItemValue();

			if( ( uxReturn & eventUNBLOCKED_DUE_TO_BIT_SET ) == ( EventBits_t ) 0 )
			{
				taskENTER_CRITICAL();
				{
					uxReturn = pxEventBits->uxEventBits;

					if( ( uxReturn & uxBitsToWaitFor ) == uxBitsToWaitFor )
					{
						pxEventBits->uxEventBits &= ~uxBitsToWaitFor;
					}
					else
					{
						mtCOVERAGE_TEST_MARKER();
					}
				}
				taskEXIT_CRITICAL();

				xTimeoutOccurred = pdTRUE;
			}
			else
			{
			}
			uxReturn &= ~eventEVENT_BITS_CONTROL_BYTES;
		}

		traceEVENT_GROUP_SYNC_END( xEventGroup, uxBitsToSet, uxBitsToWaitFor, xTimeoutOccurred );

		( void ) xTimeoutOccurred;
		traceRETURN_xEventGroupSync( uxReturn );
		return uxReturn;
	}

	EventBits_t xEventGroupWaitBits( EventGroupHandle_t xEventGroup, const EventBits_t uxBitsToWaitFor, const BaseType_t xClearOnExit, const BaseType_t xWaitForAllBits, TickType_t xTicksToWait )
	{
		EventGroup_t * pxEventBits = xEventGroup;
		EventBits_t uxReturn, uxControlBits = 0;
		BaseType_t xWaitConditionMet, xAlreadyYielded;
		BaseType_t xTimeoutOccurred = pdFALSE;

		traceENTER_xEventGroupWaitBits( xEventGroup, uxBitsToWaitFor, xClearOnExit, xWaitForAllBits, xTicksToWait );

		configASSERT( xEventGroup );
		configASSERT( ( uxBitsToWaitFor & eventEVENT_BITS_CONTROL_BYTES ) == 0 );
		configASSERT( uxBitsToWaitFor != 0 );
		#if ( ( INCLUDE_xTaskGetSchedulerState == 1 ) || ( configUSE_TIMERS == 1 ) )
		{
			configASSERT( !( ( xTaskGetSchedulerState() == taskSCHEDULER_SUSPENDED ) && ( xTicksToWait != 0 ) ) );
		}
		#endif

		vTaskSuspendAll();
		{
			const EventBits_t uxCurrentEventBits = pxEventBits->uxEventBits;
			xWaitConditionMet = prvTestWaitCondition( uxCurrentEventBits, uxBitsToWaitFor, xWaitForAllBits );

			if( xWaitConditionMet != pdFALSE )
			{
				uxReturn = uxCurrentEventBits;
				xTicksToWait = ( TickType_t ) 0;

				if( xClearOnExit != pdFALSE )
				{
						pxEventBits->uxEventBits &= ~uxBitsToWaitFor;
				}
				else
				{
						mtCOVERAGE_TEST_MARKER();
				}
			}
			else if( xTicksToWait == ( TickType_t ) 0 )
			{
				uxReturn = uxCurrentEventBits;
				xTimeoutOccurred = pdTRUE;
			}
			else
			{
				if( xClearOnExit != pdFALSE )
				{
					uxControlBits |= eventCLEAR_EVENTS_ON_EXIT_BIT;
				}
				else
				{
					mtCOVERAGE_TEST_MARKER();
				}

				if( xWaitForAllBits != pdFALSE )
				{
					uxControlBits |= eventWAIT_FOR_ALL_BITS;
				}
				else
				{
					mtCOVERAGE_TEST_MARKER();
				}

				vTaskPlaceOnUnorderedEventList( &( pxEventBits->xTasksWaitingForBits ), ( uxBitsToWaitFor | uxControlBits ), xTicksToWait );
				uxReturn = 0;
				traceEVENT_GROUP_WAIT_BITS_BLOCK( xEventGroup, uxBitsToWaitFor );
			}
		}
		xAlreadyYielded = xTaskResumeAll();

		if( xTicksToWait != ( TickType_t ) 0 )
		{
			if( xAlreadyYielded == pdFALSE )
			{
				taskYIELD_WITHIN_API();
			}
			else
			{
				mtCOVERAGE_TEST_MARKER();
			}

			uxReturn = uxTaskResetEventItemValue();

			if( ( uxReturn & eventUNBLOCKED_DUE_TO_BIT_SET ) == ( EventBits_t ) 0 )
			{
				taskENTER_CRITICAL();
				{
					uxReturn = pxEventBits->uxEventBits;

					if( prvTestWaitCondition( uxReturn, uxBitsToWaitFor, xWaitForAllBits ) != pdFALSE )
					{
						if( xClearOnExit != pdFALSE )
						{
								pxEventBits->uxEventBits &= ~uxBitsToWaitFor;
						}
						else
						{
								mtCOVERAGE_TEST_MARKER();
						}
					}
					else
					{
						mtCOVERAGE_TEST_MARKER();
					}

					xTimeoutOccurred = pdTRUE;
				}
				taskEXIT_CRITICAL();
			}
			else
			{
			}

			uxReturn &= ~eventEVENT_BITS_CONTROL_BYTES;
		}

		traceEVENT_GROUP_WAIT_BITS_END( xEventGroup, uxBitsToWaitFor, xTimeoutOccurred );
		( void ) xTimeoutOccurred;
		traceRETURN_xEventGroupWaitBits( uxReturn );
		return uxReturn;
	}

	EventBits_t xEventGroupClearBits( EventGroupHandle_t xEventGroup, const EventBits_t uxBitsToClear )
	{
		EventGroup_t * pxEventBits = xEventGroup;
		EventBits_t uxReturn;

		traceENTER_xEventGroupClearBits( xEventGroup, uxBitsToClear );

		configASSERT( xEventGroup );
		configASSERT( ( uxBitsToClear & eventEVENT_BITS_CONTROL_BYTES ) == 0 );

		taskENTER_CRITICAL();
		{
			traceEVENT_GROUP_CLEAR_BITS( xEventGroup, uxBitsToClear );

			uxReturn = pxEventBits->uxEventBits;

			pxEventBits->uxEventBits &= ~uxBitsToClear;
		}
		taskEXIT_CRITICAL();
		traceRETURN_xEventGroupClearBits( uxReturn );
		return uxReturn;
	}


	#if ( ( configUSE_TRACE_FACILITY == 1 ) && ( INCLUDE_xTimerPendFunctionCall == 1 ) && ( configUSE_TIMERS == 1 ) )

		BaseType_t xEventGroupClearBitsFromISR( EventGroupHandle_t xEventGroup, const EventBits_t uxBitsToClear )
		{
			BaseType_t xReturn;

			traceENTER_xEventGroupClearBitsFromISR( xEventGroup, uxBitsToClear );

			traceEVENT_GROUP_CLEAR_BITS_FROM_ISR( xEventGroup, uxBitsToClear );
			xReturn = xTimerPendFunctionCallFromISR( vEventGroupClearBitsCallback, ( void * ) xEventGroup, ( uint32_t ) uxBitsToClear, NULL );

			traceRETURN_xEventGroupClearBitsFromISR( xReturn );

			return xReturn;
		}

	#endif 

	EventBits_t xEventGroupGetBitsFromISR( EventGroupHandle_t xEventGroup )
	{
		UBaseType_t uxSavedInterruptStatus;
		EventGroup_t const * const pxEventBits = xEventGroup;
		EventBits_t uxReturn;

		traceENTER_xEventGroupGetBitsFromISR( xEventGroup );

		uxSavedInterruptStatus = taskENTER_CRITICAL_FROM_ISR();
		{
			uxReturn = pxEventBits->uxEventBits;
		}
		taskEXIT_CRITICAL_FROM_ISR( uxSavedInterruptStatus );
		traceRETURN_xEventGroupGetBitsFromISR( uxReturn );
		return uxReturn;
	}


	EventBits_t xEventGroupSetBits( EventGroupHandle_t xEventGroup, const EventBits_t uxBitsToSet )
	{
		ListItem_t * pxListItem;
		ListItem_t * pxNext;
		ListItem_t const * pxListEnd;
		List_t const * pxList;
		EventBits_t uxBitsToClear = 0, uxBitsWaitedFor, uxControlBits;
		EventGroup_t * pxEventBits = xEventGroup;
		BaseType_t xMatchFound = pdFALSE;

		traceENTER_xEventGroupSetBits( xEventGroup, uxBitsToSet );

		configASSERT( xEventGroup );
		configASSERT( ( uxBitsToSet & eventEVENT_BITS_CONTROL_BYTES ) == 0 );

		pxList = &( pxEventBits->xTasksWaitingForBits );
		pxListEnd = listGET_END_MARKER( pxList );
		vTaskSuspendAll();
		{
			traceEVENT_GROUP_SET_BITS( xEventGroup, uxBitsToSet );

			pxListItem = listGET_HEAD_ENTRY( pxList );

			pxEventBits->uxEventBits |= uxBitsToSet;

			while( pxListItem != pxListEnd )
			{
				pxNext = listGET_NEXT( pxListItem );
				uxBitsWaitedFor = listGET_LIST_ITEM_VALUE( pxListItem );
				xMatchFound = pdFALSE;

				uxControlBits = uxBitsWaitedFor & eventEVENT_BITS_CONTROL_BYTES;
				uxBitsWaitedFor &= ~eventEVENT_BITS_CONTROL_BYTES;

				if( ( uxControlBits & eventWAIT_FOR_ALL_BITS ) == ( EventBits_t ) 0 )
				{
					if( ( uxBitsWaitedFor & pxEventBits->uxEventBits ) != ( EventBits_t ) 0 )
					{
						xMatchFound = pdTRUE;
					}
					else
					{
						mtCOVERAGE_TEST_MARKER();
					}
				}
				else if( ( uxBitsWaitedFor & pxEventBits->uxEventBits ) == uxBitsWaitedFor )
				{
					xMatchFound = pdTRUE;
				}
				else
				{
				}

				if( xMatchFound != pdFALSE )
				{
					if( ( uxControlBits & eventCLEAR_EVENTS_ON_EXIT_BIT ) != ( EventBits_t ) 0 )
					{
						uxBitsToClear |= uxBitsWaitedFor;
					}
					else
					{
						mtCOVERAGE_TEST_MARKER();
					}

					vTaskRemoveFromUnorderedEventList( pxListItem, pxEventBits->uxEventBits | eventUNBLOCKED_DUE_TO_BIT_SET );
				}
				pxListItem = pxNext;
			}

			pxEventBits->uxEventBits &= ~uxBitsToClear;
		}
		( void ) xTaskResumeAll();
		traceRETURN_xEventGroupSetBits( pxEventBits->uxEventBits );
		return pxEventBits->uxEventBits;
	}

	void vEventGroupDelete( EventGroupHandle_t xEventGroup )
	{
		EventGroup_t * pxEventBits = xEventGroup;
		const List_t * pxTasksWaitingForBits;

		traceENTER_vEventGroupDelete( xEventGroup );

		configASSERT( pxEventBits );

		pxTasksWaitingForBits = &( pxEventBits->xTasksWaitingForBits );

		vTaskSuspendAll();
		{
			traceEVENT_GROUP_DELETE( xEventGroup );
			while( listCURRENT_LIST_LENGTH( pxTasksWaitingForBits ) > ( UBaseType_t ) 0 )
			{
				configASSERT( pxTasksWaitingForBits->xListEnd.pxNext != ( const ListItem_t * ) &( pxTasksWaitingForBits->xListEnd ) );
				vTaskRemoveFromUnorderedEventList( pxTasksWaitingForBits->xListEnd.pxNext, eventUNBLOCKED_DUE_TO_BIT_SET );
			}
		}
		( void ) xTaskResumeAll();

		#if ( ( configSUPPORT_DYNAMIC_ALLOCATION == 1 ) && ( configSUPPORT_STATIC_ALLOCATION == 0 ) )
		{
			vPortFree( pxEventBits );
		}
		#elif ( ( configSUPPORT_DYNAMIC_ALLOCATION == 1 ) && ( configSUPPORT_STATIC_ALLOCATION == 1 ) )
		{
			if( pxEventBits->ucStaticallyAllocated == ( uint8_t ) pdFALSE )
			{
					vPortFree( pxEventBits );
			}
			else
			{
					mtCOVERAGE_TEST_MARKER();
			}
		}
		#endif 
		traceRETURN_vEventGroupDelete();
	}

	#if ( configSUPPORT_STATIC_ALLOCATION == 1 )
		BaseType_t xEventGroupGetStaticBuffer( EventGroupHandle_t xEventGroup, StaticEventGroup_t ** ppxEventGroupBuffer )
		{
			BaseType_t xReturn;
			EventGroup_t * pxEventBits = xEventGroup;

			traceENTER_xEventGroupGetStaticBuffer( xEventGroup, ppxEventGroupBuffer );

			configASSERT( pxEventBits );
			configASSERT( ppxEventGroupBuffer );

			#if ( configSUPPORT_DYNAMIC_ALLOCATION == 1 )
			{
				if( pxEventBits->ucStaticallyAllocated == ( uint8_t ) pdTRUE )
				{
					*ppxEventGroupBuffer = ( StaticEventGroup_t * ) pxEventBits;
					xReturn = pdTRUE;
				}
				else
				{
					xReturn = pdFALSE;
				}
			}
			#else 
			{
				*ppxEventGroupBuffer = ( StaticEventGroup_t * ) pxEventBits;
				xReturn = pdTRUE;
			}
			#endif 

			traceRETURN_xEventGroupGetStaticBuffer( xReturn );
			return xReturn;
		}
	#endif 


	void vEventGroupSetBitsCallback( void * pvEventGroup, uint32_t ulBitsToSet )
	{
		traceENTER_vEventGroupSetBitsCallback( pvEventGroup, ulBitsToSet );
		( void ) xEventGroupSetBits( pvEventGroup, ( EventBits_t ) ulBitsToSet );
		traceRETURN_vEventGroupSetBitsCallback();
	}

	void vEventGroupClearBitsCallback( void * pvEventGroup, uint32_t ulBitsToClear )
	{
		traceENTER_vEventGroupClearBitsCallback( pvEventGroup, ulBitsToClear );
		( void ) xEventGroupClearBits( pvEventGroup, ( EventBits_t ) ulBitsToClear );
		traceRETURN_vEventGroupClearBitsCallback();
	}

	static BaseType_t prvTestWaitCondition( const EventBits_t uxCurrentEventBits, const EventBits_t uxBitsToWaitFor, const BaseType_t xWaitForAllBits )
	{
		BaseType_t xWaitConditionMet = pdFALSE;
		if( xWaitForAllBits == pdFALSE )
		{
			if( ( uxCurrentEventBits & uxBitsToWaitFor ) != ( EventBits_t ) 0 )
			{
				xWaitConditionMet = pdTRUE;
			}
			else
			{
				mtCOVERAGE_TEST_MARKER();
			}
		}
		else
		{
			if( ( uxCurrentEventBits & uxBitsToWaitFor ) == uxBitsToWaitFor )
			{
				xWaitConditionMet = pdTRUE;
			}
			else
			{
				mtCOVERAGE_TEST_MARKER();
			}
		}
		return xWaitConditionMet;
	}

	#if ( ( configUSE_TRACE_FACILITY == 1 ) && ( INCLUDE_xTimerPendFunctionCall == 1 ) && ( configUSE_TIMERS == 1 ) )
		BaseType_t xEventGroupSetBitsFromISR( EventGroupHandle_t xEventGroup, const EventBits_t uxBitsToSet, BaseType_t * pxHigherPriorityTaskWoken )
		{
			BaseType_t xReturn;
			traceENTER_xEventGroupSetBitsFromISR( xEventGroup, uxBitsToSet, pxHigherPriorityTaskWoken );

			traceEVENT_GROUP_SET_BITS_FROM_ISR( xEventGroup, uxBitsToSet );
			xReturn = xTimerPendFunctionCallFromISR( vEventGroupSetBitsCallback, ( void * ) xEventGroup, ( uint32_t ) uxBitsToSet, pxHigherPriorityTaskWoken );

			traceRETURN_xEventGroupSetBitsFromISR( xReturn );
			return xReturn;
		}
	#endif 


	#if ( configUSE_TRACE_FACILITY == 1 )
		UBaseType_t uxEventGroupGetNumber( void * xEventGroup )
		{
			UBaseType_t xReturn;
			EventGroup_t const * pxEventBits = ( EventGroup_t * ) xEventGroup;
			traceENTER_uxEventGroupGetNumber( xEventGroup );

			if( xEventGroup == NULL )
			{
				xReturn = 0;
			}
			else
			{
				xReturn = pxEventBits->uxEventGroupNumber;
			}

			traceRETURN_uxEventGroupGetNumber( xReturn );
			return xReturn;
		}
	#endif 

	#if ( configUSE_TRACE_FACILITY == 1 )
		void vEventGroupSetNumber( void * xEventGroup, UBaseType_t uxEventGroupNumber )
		{
			traceENTER_vEventGroupSetNumber( xEventGroup, uxEventGroupNumber );
			( ( EventGroup_t * ) xEventGroup )->uxEventGroupNumber = uxEventGroupNumber;
			traceRETURN_vEventGroupSetNumber();
		}

	#endif 

#endif 
