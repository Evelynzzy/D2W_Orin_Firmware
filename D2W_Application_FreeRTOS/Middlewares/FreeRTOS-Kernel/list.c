#include <stdlib.h>

#define MPU_WRAPPERS_INCLUDED_FROM_API_FILE

#include "FreeRTOS.h"
#include "list.h"

#undef MPU_WRAPPERS_INCLUDED_FROM_API_FILE


void vListInitialise( List_t * const pxList )
{
	traceENTER_vListInitialise( pxList );

	pxList->pxIndex = ( ListItem_t * ) &( pxList->xListEnd );

	listSET_FIRST_LIST_ITEM_INTEGRITY_CHECK_VALUE( &( pxList->xListEnd ) );

	pxList->xListEnd.xItemValue = portMAX_DELAY;

	pxList->xListEnd.pxNext = ( ListItem_t * ) &( pxList->xListEnd );
	pxList->xListEnd.pxPrevious = ( ListItem_t * ) &( pxList->xListEnd );

	#if ( configUSE_MINI_LIST_ITEM == 0 )
	{
		pxList->xListEnd.pvOwner = NULL;
		pxList->xListEnd.pxContainer = NULL;
		listSET_SECOND_LIST_ITEM_INTEGRITY_CHECK_VALUE( &( pxList->xListEnd ) );
	}
	#endif

	pxList->uxNumberOfItems = ( UBaseType_t ) 0U;

	listSET_LIST_INTEGRITY_CHECK_1_VALUE( pxList );
	listSET_LIST_INTEGRITY_CHECK_2_VALUE( pxList );

	traceRETURN_vListInitialise();
}


void vListInitialiseItem( ListItem_t * const pxItem )
{
	traceENTER_vListInitialiseItem( pxItem );

	pxItem->pxContainer = NULL;

	listSET_FIRST_LIST_ITEM_INTEGRITY_CHECK_VALUE( pxItem );
	listSET_SECOND_LIST_ITEM_INTEGRITY_CHECK_VALUE( pxItem );

	traceRETURN_vListInitialiseItem();
}


void vListInsertEnd( List_t * const pxList, ListItem_t * const pxNewListItem )
{
	ListItem_t * const pxIndex = pxList->pxIndex;

	traceENTER_vListInsertEnd( pxList, pxNewListItem );

	listTEST_LIST_INTEGRITY( pxList );
	listTEST_LIST_ITEM_INTEGRITY( pxNewListItem );

	pxNewListItem->pxNext = pxIndex;
	pxNewListItem->pxPrevious = pxIndex->pxPrevious;

	mtCOVERAGE_TEST_DELAY();

	pxIndex->pxPrevious->pxNext = pxNewListItem;
	pxIndex->pxPrevious = pxNewListItem;

	pxNewListItem->pxContainer = pxList;

	( pxList->uxNumberOfItems ) = ( UBaseType_t ) ( pxList->uxNumberOfItems + 1U );

	traceRETURN_vListInsertEnd();
}


void vListInsert( List_t * const pxList, ListItem_t * const pxNewListItem )
{
	ListItem_t * pxIterator;
	const TickType_t xValueOfInsertion = pxNewListItem->xItemValue;

	traceENTER_vListInsert( pxList, pxNewListItem );

	listTEST_LIST_INTEGRITY( pxList );
	listTEST_LIST_ITEM_INTEGRITY( pxNewListItem );


	if( xValueOfInsertion == portMAX_DELAY )
	{
		pxIterator = pxList->xListEnd.pxPrevious;
	}
	else
	{
		for( pxIterator = ( ListItem_t * ) &( pxList->xListEnd ); pxIterator->pxNext->xItemValue <= xValueOfInsertion; pxIterator = pxIterator->pxNext )
		{
		}
	}

	pxNewListItem->pxNext = pxIterator->pxNext;
	pxNewListItem->pxNext->pxPrevious = pxNewListItem;
	pxNewListItem->pxPrevious = pxIterator;
	pxIterator->pxNext = pxNewListItem;

	pxNewListItem->pxContainer = pxList;

	( pxList->uxNumberOfItems ) = ( UBaseType_t ) ( pxList->uxNumberOfItems + 1U );

	traceRETURN_vListInsert();
}


UBaseType_t uxListRemove( ListItem_t * const pxItemToRemove )
{
	List_t * const pxList = pxItemToRemove->pxContainer;

	traceENTER_uxListRemove( pxItemToRemove );

	pxItemToRemove->pxNext->pxPrevious = pxItemToRemove->pxPrevious;
	pxItemToRemove->pxPrevious->pxNext = pxItemToRemove->pxNext;

	mtCOVERAGE_TEST_DELAY();

	if( pxList->pxIndex == pxItemToRemove )
	{
		pxList->pxIndex = pxItemToRemove->pxPrevious;
	}
	else
	{
		mtCOVERAGE_TEST_MARKER();
	}

	pxItemToRemove->pxContainer = NULL;
	( pxList->uxNumberOfItems ) = ( UBaseType_t ) ( pxList->uxNumberOfItems - 1U );

	traceRETURN_uxListRemove( pxList->uxNumberOfItems );

	return pxList->uxNumberOfItems;
}

