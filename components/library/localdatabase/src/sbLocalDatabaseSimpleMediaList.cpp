/* vim: set sw=2 :miv */
/*
//
// BEGIN SONGBIRD GPL
//
// This file is part of the Songbird web player.
//
// Copyright(c) 2005-2008 POTI, Inc.
// http://songbirdnest.com
//
// This file may be licensed under the terms of of the
// GNU General Public License Version 2 (the "GPL").
//
// Software distributed under the License is distributed
// on an "AS IS" basis, WITHOUT WARRANTY OF ANY KIND, either
// express or implied. See the GPL for the specific language
// governing rights and limitations.
//
// You should have received a copy of the GPL along with this
// program. If not, go to http://www.gnu.org/licenses/gpl.html
// or write to the Free Software Foundation, Inc.,
// 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
//
// END SONGBIRD GPL
//
*/

#include "sbLocalDatabaseSimpleMediaList.h"

#include <nsIArray.h>
#include <nsIClassInfoImpl.h>
#include <nsIMutableArray.h>
#include <nsIProgrammingLanguage.h>
#include <nsISimpleEnumerator.h>
#include <nsIURI.h>
#include <sbIDatabaseQuery.h>
#include <sbIDatabaseResult.h>
#include <sbILibrary.h>
#include <sbILocalDatabaseGUIDArray.h>
#include <sbILocalDatabasePropertyCache.h>
#include <sbIMediaListView.h>
#include <sbISQLBuilder.h>

#include "sbLocalDatabaseCID.h"
#include "sbLocalDatabaseLibrary.h"
#include "sbLocalDatabaseGUIDArray.h"
#include "sbMediaListEnumSingleItemHelper.h"

#include <DatabaseQuery.h>
#include <nsArrayUtils.h>
#include <nsAutoLock.h>
#include <nsAutoPtr.h>
#include <nsCOMPtr.h>
#include <nsComponentManagerUtils.h>
#include <nsMemory.h>
#include <nsXPCOMCID.h>
#include <pratom.h>
#include <sbLocalDatabaseMediaListView.h>
#include <sbStandardProperties.h>
#include <sbSQLBuilderCID.h>

#define DEFAULT_SORT_PROPERTY NS_LITERAL_STRING(SB_PROPERTY_ORDINAL)
#define DEFAULT_FETCH_SIZE 1000

/**
 * To log this class, set the following environment variable:
 *   NSPR_LOG_MODULES=sbLocalDatabaseSimpleMediaList:5
 */
#ifdef PR_LOGGING
static PRLogModuleInfo* gLocalDatabaseSimpleMediaListLog = nsnull;
#endif /* PR_LOGGING */

#define TRACE(args) PR_LOG(gLocalDatabaseSimpleMediaListLog, PR_LOG_DEBUG, args)
#define LOG(args)   PR_LOG(gLocalDatabaseSimpleMediaListLog, PR_LOG_WARN, args)

#ifdef DEBUG
#define ASSERT_LIST_IS_LIBRARY(_mediaList)                                     \
  PR_BEGIN_MACRO                                                               \
    nsresult rv;                                                               \
    nsCOMPtr<sbILibrary> library = do_QueryInterface(mLibrary, &rv);           \
    NS_ASSERTION(NS_SUCCEEDED(rv), "Library won't QI to sbILibrary!");         \
                                                                               \
    if (NS_SUCCEEDED(rv)) {                                                    \
      PRBool listIsLibrary;                                                    \
      rv = library->Equals(_mediaList, &listIsLibrary);                        \
      NS_ASSERTION(NS_SUCCEEDED(rv), "Equals failed!");                        \
                                                                               \
      if (NS_SUCCEEDED(rv)) {                                                  \
        NS_ASSERTION(listIsLibrary, "Watching the wrong list!");               \
      }                                                                        \
    }                                                                          \
  PR_END_MACRO
#else /* DEBUG */
#define ASSERT_LIST_IS_LIBRARY(_mediaList)                                     \
  PR_BEGIN_MACRO /* nothing */ PR_END_MACRO
#endif /* DEBUG */

#define SB_ENSURE_INDEX_BEGIN             \
  PR_BEGIN_MACRO                          \
    nsresult rv;                          \
    PRUint32 length;                      \
    rv = GetArray()->GetLength(&length);  \
    NS_ENSURE_SUCCESS(rv, rv);

#define SB_ENSURE_INDEX_END \
  PR_END_MACRO

#define SB_ENSURE_INDEX1(_index)            \
  SB_ENSURE_INDEX_BEGIN                     \
    NS_ENSURE_ARG_MAX(_index, length - 1);  \
  SB_ENSURE_INDEX_END

#define SB_ENSURE_INDEX2(_index1, _index2)  \
  SB_ENSURE_INDEX_BEGIN                     \
    NS_ENSURE_ARG_MAX(_index1, length - 1); \
    NS_ENSURE_ARG_MAX(_index2, length - 1); \
  SB_ENSURE_INDEX_END

  // This class is stack-only but needs to act like an XPCOM object.  Add dummy
// AddRef/Release methods so the ref count is always 1
NS_IMETHODIMP_(nsrefcnt)
sbSimpleMediaListInsertingEnumerationListener::AddRef(void)
{
  return (nsrefcnt)1;
}

NS_IMETHODIMP_(nsrefcnt)
sbSimpleMediaListInsertingEnumerationListener::Release(void)
{
  return (nsrefcnt)1;
}

NS_IMPL_QUERY_INTERFACE1(sbSimpleMediaListInsertingEnumerationListener,
                         sbIMediaListEnumerationListener)

/**
 * See sbIMediaListListener.idl
 */
NS_IMETHODIMP
sbSimpleMediaListInsertingEnumerationListener::OnEnumerationBegin(sbIMediaList* aMediaList,
                                                                  PRUint16* _retval)
{
  NS_ENSURE_ARG_POINTER(_retval);

  NS_ASSERTION(aMediaList != mFriendList,
               "Can't enumerate our friend media list!");

  PRBool success = mItemsToCreate.Init();
  NS_ENSURE_TRUE(success, NS_ERROR_OUT_OF_MEMORY);

  nsresult rv = mFriendList->GetLibrary(getter_AddRefs(mListLibrary));
  NS_ENSURE_SUCCESS(rv, rv);

  // All good for enumerating.
  *_retval = sbIMediaListEnumerationListener::CONTINUE;

  return NS_OK;
}

/**
 * See sbIMediaListListener.idl
 */
NS_IMETHODIMP
sbSimpleMediaListInsertingEnumerationListener::OnEnumeratedItem(sbIMediaList* aMediaList,
                                                                sbIMediaItem* aMediaItem,
                                                                PRUint16* _retval)
{
  NS_ENSURE_ARG_POINTER(aMediaItem);
  NS_ENSURE_ARG_POINTER(_retval);

  NS_ASSERTION(aMediaList != mFriendList,
               "Can't enumerate our friend media list!");

  nsCOMPtr<sbILibrary> itemLibrary;
  nsresult rv = aMediaItem->GetLibrary(getter_AddRefs(itemLibrary));
  NS_ENSURE_SUCCESS(rv, rv);

  PRBool sameLibrary;
  rv = itemLibrary->Equals(mListLibrary, &sameLibrary);
  NS_ENSURE_SUCCESS(rv, rv);

  nsString listLibGuid;
  rv = mListLibrary->GetGuid(listLibGuid);
  NS_ENSURE_SUCCESS(rv, rv);

  PRBool success;
  NS_NAMED_LITERAL_STRING(PROP_LIBRARY, SB_PROPERTY_ORIGINLIBRARYGUID);
  NS_NAMED_LITERAL_STRING(PROP_ITEM, SB_PROPERTY_ORIGINITEMGUID);
  if (!sameLibrary && !mItemsToCreate.Get(aMediaItem, nsnull)) {
    // This item comes from another library so we'll need to add it to our
    // library before it can be used.

    // but first check if we have an existing item that is close enough
    nsString originLibGuid, originItemGuid;
    rv = aMediaItem->GetProperty(PROP_LIBRARY, originLibGuid);
    NS_ENSURE_SUCCESS(rv, rv);
    if (originLibGuid.IsEmpty()) {
      rv = itemLibrary->GetGuid(originLibGuid);
      NS_ENSURE_SUCCESS(rv, rv);
    }

    // check if we have an item that originated from the same item
    rv = aMediaItem->GetProperty(PROP_ITEM, originItemGuid);
    NS_ENSURE_SUCCESS(rv, rv);
    if (originItemGuid.IsEmpty()) {
      rv = aMediaItem->GetGuid(originItemGuid);
      NS_ENSURE_SUCCESS(rv, rv);
    }

    nsCOMPtr<sbIMediaItem> foundItem;
    // if the origin library is this library, just look for the item
    if (listLibGuid.Equals(originLibGuid)) {
      // the origin item was from this library
      rv = mListLibrary->GetMediaItem(originItemGuid, getter_AddRefs(foundItem));
      if (NS_FAILED(rv)) {
        // didn't find it
        foundItem = nsnull;
      }
    } else {
      nsCOMPtr<sbIMutablePropertyArray> originGuidArray =
        do_CreateInstance(SB_MUTABLEPROPERTYARRAY_CONTRACTID, &rv);
      NS_ENSURE_SUCCESS(rv, rv);

      rv = originGuidArray->AppendProperty(PROP_LIBRARY, originLibGuid);
      NS_ENSURE_SUCCESS(rv, rv);

      rv = originGuidArray->AppendProperty(PROP_ITEM, originItemGuid);
      NS_ENSURE_SUCCESS(rv, rv);

      nsRefPtr<sbMediaListEnumSingleItemHelper> guidCheckHelper =
        sbMediaListEnumSingleItemHelper::New();
      NS_ENSURE_TRUE(guidCheckHelper, NS_ERROR_OUT_OF_MEMORY);

      rv = mListLibrary->EnumerateItemsByProperties(originGuidArray,
                                                    guidCheckHelper,
                                                    sbIMediaList::ENUMERATIONTYPE_SNAPSHOT);
      NS_ENSURE_SUCCESS(rv, rv);

      foundItem = guidCheckHelper->GetItem(); /* null if not found */
    }

    success = mItemsToCreate.Put(aMediaItem, foundItem);
    NS_ENSURE_TRUE(success, NS_ERROR_FAILURE);
  }

  // Remember this media item.
  success = mItemList.AppendObject(aMediaItem);
  NS_ENSURE_TRUE(success, NS_ERROR_OUT_OF_MEMORY);

  *_retval = sbIMediaListEnumerationListener::CONTINUE;

  return NS_OK;
}

/**
 * See sbIMediaListListener.idl
 */
NS_IMETHODIMP
sbSimpleMediaListInsertingEnumerationListener::OnEnumerationEnd(sbIMediaList* aMediaList,
                                                                nsresult aStatusCode)
{
  TRACE(("LocalDatabaseSimpleMediaList[0x%.8x] - "
         "InsertingEnumerator::OnEnumerationEnd", this));

  NS_ASSERTION(aMediaList != mFriendList,
               "Can't enumerate our friend media list!");

  PRUint32 itemCount = mItemList.Count();
  if (!itemCount) {
    NS_WARNING("OnEnumerationEnd called with no items enumerated");
    return NS_OK;
  }

  nsresult rv;

  PRUint32 itemsToCreateCount = mItemsToCreate.Count();
  if (itemsToCreateCount) {

    nsCOMPtr<nsIMutableArray> oldItems =
      do_CreateInstance("@songbirdnest.com/moz/xpcom/threadsafe-array;1", &rv);
    NS_ENSURE_SUCCESS(rv, rv);

    nsCOMPtr<nsIMutableArray> oldURIs =
      do_CreateInstance("@songbirdnest.com/moz/xpcom/threadsafe-array;1", &rv);
    NS_ENSURE_SUCCESS(rv, rv);

    // Build a list of the items to create.
    PRUint32 numItems = mItemList.Count();
    for (PRUint32 i = 0; i < numItems; i++) {
      nsCOMPtr<sbIMediaItem> existing;
      mItemsToCreate.Get(mItemList[i], getter_AddRefs(existing));
      if (!existing) {
        nsCOMPtr<nsIURI> uri;
        rv = mItemList[i]->GetContentSrc(getter_AddRefs(uri));
        NS_ENSURE_SUCCESS(rv, rv);

        rv = oldItems->AppendElement(mItemList[i], PR_FALSE);
        NS_ENSURE_SUCCESS(rv, rv);

        rv = oldURIs->AppendElement(uri, PR_FALSE);
        NS_ENSURE_SUCCESS(rv, rv);
      }
    }

    nsCOMPtr<nsIMutableArray> propertyArrayArray =
      do_CreateInstance("@songbirdnest.com/moz/xpcom/threadsafe-array;1", &rv);
    NS_ENSURE_SUCCESS(rv, rv);

    PRUint32 oldItemCount;
    rv = oldItems->GetLength(&oldItemCount);
    NS_ENSURE_SUCCESS(rv, rv);

    // Skip the following if there are no items to add
    if (oldItemCount) {

      for (PRUint32 i = 0; i < oldItemCount; i++) {
        nsCOMPtr<sbIMediaItem> item = do_QueryElementAt(oldItems, i, &rv);
        NS_ENSURE_SUCCESS(rv, rv);

        nsCOMPtr<sbIPropertyArray> properties;
        rv = item->GetProperties(nsnull, getter_AddRefs(properties));
        NS_ENSURE_SUCCESS(rv, rv);

        nsCOMPtr<sbIPropertyArray> filteredProperties;
        rv = mFriendList->GetFilteredPropertiesForNewItem(properties,
                                                          getter_AddRefs(filteredProperties));
        NS_ENSURE_SUCCESS(rv, rv);

        nsCOMPtr<sbIMutablePropertyArray> mutableProperties =
          do_QueryInterface(filteredProperties, &rv);
        NS_ENSURE_SUCCESS(rv, rv);

        // keep track of the library/item guid that we just copied from
        NS_NAMED_LITERAL_STRING(PROP_LIBRARY, SB_PROPERTY_ORIGINLIBRARYGUID);
        NS_NAMED_LITERAL_STRING(PROP_ITEM, SB_PROPERTY_ORIGINITEMGUID);
        nsString existingGuid, sourceGuid;

        rv = filteredProperties->GetPropertyValue(PROP_LIBRARY, existingGuid);
        if (rv == NS_ERROR_NOT_AVAILABLE) {
          nsCOMPtr<sbILibrary> oldLibrary;
          rv = item->GetLibrary(getter_AddRefs(oldLibrary));
          NS_ENSURE_SUCCESS(rv, rv);

          rv = oldLibrary->GetGuid(sourceGuid);
          NS_ENSURE_SUCCESS(rv, rv);

          rv = mutableProperties->AppendProperty(PROP_LIBRARY, sourceGuid);
          NS_ENSURE_SUCCESS(rv, rv);
        }

        rv = filteredProperties->GetPropertyValue(PROP_ITEM, existingGuid);
        if (rv == NS_ERROR_NOT_AVAILABLE) {
          rv = item->GetGuid(sourceGuid);
          NS_ENSURE_SUCCESS(rv, rv);

          rv = mutableProperties->AppendProperty(PROP_ITEM, sourceGuid);
          NS_ENSURE_SUCCESS(rv, rv);
        }

        rv = propertyArrayArray->AppendElement(filteredProperties, PR_FALSE);
        NS_ENSURE_SUCCESS(rv, rv);
      }

      nsCOMPtr<nsIArray> newItems;
      rv = mListLibrary->BatchCreateMediaItems(oldURIs,
                                               propertyArrayArray,
                                               PR_TRUE,
                                               getter_AddRefs(newItems));
      NS_ENSURE_SUCCESS(rv, rv);

      PRUint32 newItemCount;
      rv = newItems->GetLength(&newItemCount);
      NS_ENSURE_SUCCESS(rv, rv);

#ifdef DEBUG
      NS_ASSERTION(newItemCount == oldItemCount,
                   "BatchCreateMediaItems didn't make the right number of items!");
#endif

      for (PRUint32 index = 0; index < newItemCount; index++) {
        nsCOMPtr<sbIMediaItem> oldItem = do_QueryElementAt(oldItems, index, &rv);
        NS_ENSURE_SUCCESS(rv, rv);

        nsCOMPtr<sbIMediaItem> newItem = do_QueryElementAt(newItems, index, &rv);
        NS_ENSURE_SUCCESS(rv, rv);

        NS_ASSERTION(mItemsToCreate.Get(oldItem, nsnull),
                     "The old item should be in the hashtable!");

        PRBool success = mItemsToCreate.Put(oldItem, newItem);
        NS_ENSURE_TRUE(success, NS_ERROR_FAILURE);
      }
    }
  }

  nsCOMPtr<sbIDatabaseQuery> query;
  rv = mFriendList->MakeStandardQuery(getter_AddRefs(query));
  NS_ENSURE_SUCCESS(rv, rv);

  rv = query->AddQuery(NS_LITERAL_STRING("begin"));
  NS_ENSURE_SUCCESS(rv, rv);

  nsString ordinal = mStartingOrdinal;

  for (PRUint32 index = 0; index < itemCount; index++) {

    nsCOMPtr<sbIMediaItem> mediaItem = mItemList[index];

    if (mItemsToCreate.Get(mediaItem, nsnull)) {
      nsCOMPtr<sbIMediaItem> newMediaItem;
      PRBool success = mItemsToCreate.Get(mediaItem, getter_AddRefs(newMediaItem));
      NS_ENSURE_TRUE(success, NS_ERROR_FAILURE);

      //Call the copy listener for this media list at this time.
      //XXXAus: This could benefit from batching in the future.
      rv = mFriendList->NotifyCopyListener(mediaItem, newMediaItem);
      NS_WARN_IF_FALSE(NS_SUCCEEDED(rv), "Failed to notify copy listener!");

      success = mItemList.ReplaceObjectAt(newMediaItem, index);
      NS_ENSURE_SUCCESS(rv, rv);

      mediaItem = newMediaItem;
    }

    nsCOMPtr<sbILocalDatabaseMediaItem> ldbmi =
      do_QueryInterface(mediaItem, &rv);
    NS_ENSURE_SUCCESS(rv, rv);

    PRUint32 mediaItemId;
    rv = ldbmi->GetMediaItemId(&mediaItemId);
    NS_ENSURE_SUCCESS(rv, rv);

    rv = query->AddQuery(mFriendList->mInsertIntoListQuery);
    NS_ENSURE_SUCCESS(rv, rv);

    rv = query->BindInt32Parameter(0, mediaItemId);
    NS_ENSURE_SUCCESS(rv, rv);

    rv = query->BindStringParameter(1, ordinal);
    NS_ENSURE_SUCCESS(rv, rv);

    // Increment the ordinal
    rv = mFriendList->AddToLastPathSegment(ordinal, 1);
    NS_ENSURE_SUCCESS(rv, rv);
  }

  rv = query->AddQuery(NS_LITERAL_STRING("commit"));
  NS_ENSURE_SUCCESS(rv, rv);

  PRInt32 dbSuccess;
  rv = query->Execute(&dbSuccess);
  NS_ENSURE_SUCCESS(rv, rv);

  // Invalidate the cached list
  rv = mFriendList->GetArray()->Invalidate();
  NS_ENSURE_SUCCESS(rv, rv);

  // Notify our listeners if we have any
  if (mFriendList->ListenerCount() > 0) {
    for (PRUint32 index = 0; index < itemCount; index++) {
      mFriendList->NotifyListenersItemAdded(mFriendList,
                                            mItemList[index],
                                            mStartingIndex + index);
    }
  }

  return NS_OK;
}

// This class is stack-only but needs to act like an XPCOM object.  Add dummy
// AddRef/Release methods so the ref count is always 1
NS_IMETHODIMP_(nsrefcnt)
sbSimpleMediaListRemovingEnumerationListener::AddRef(void)
{
  return (nsrefcnt)1;
}

NS_IMETHODIMP_(nsrefcnt)
sbSimpleMediaListRemovingEnumerationListener::Release(void)
{
  return (nsrefcnt)1;
}

NS_IMPL_QUERY_INTERFACE1(sbSimpleMediaListRemovingEnumerationListener,
                         sbIMediaListEnumerationListener)

/**
 * See sbIMediaListListener.idl
 */
NS_IMETHODIMP
sbSimpleMediaListRemovingEnumerationListener::OnEnumerationBegin(sbIMediaList* aMediaList,
                                                                 PRUint16* _retval)
{
  NS_ENSURE_ARG_POINTER(_retval);

  // Prep the query
  nsresult rv = mFriendList->MakeStandardQuery(getter_AddRefs(mDBQuery));
  NS_ENSURE_SUCCESS(rv, rv);

  rv = mDBQuery->AddQuery(NS_LITERAL_STRING("begin"));
  NS_ENSURE_SUCCESS(rv, rv);

  *_retval = sbIMediaListEnumerationListener::CONTINUE;

  return NS_OK;
}

/**
 * See sbIMediaListListener.idl
 */
NS_IMETHODIMP
sbSimpleMediaListRemovingEnumerationListener::OnEnumeratedItem(sbIMediaList* aMediaList,
                                                               sbIMediaItem* aMediaItem,
                                                               PRUint16* _retval)
{
  NS_ENSURE_ARG_POINTER(aMediaItem);
  NS_ENSURE_ARG_POINTER(_retval);

  PRUint32 index;
  nsresult rv = mFriendList->IndexOf(aMediaItem, 0, &index);
  // If the item is not in the list, ignore it
  if (rv == NS_ERROR_NOT_AVAILABLE) {
    return NS_OK;
  }
  NS_ENSURE_SUCCESS(rv, rv);

  // Remember this media item for later so we can notify with it
  PRBool success = mNotificationList.AppendObject(aMediaItem);
  NS_ENSURE_TRUE(success, NS_ERROR_OUT_OF_MEMORY);

  PRUint32* added = mNotificationIndexes.AppendElement(index);
  NS_ENSURE_TRUE(added, NS_ERROR_OUT_OF_MEMORY);

  nsCOMPtr<sbILocalDatabaseMediaItem> ldbmi =
    do_QueryInterface(aMediaItem, &rv);
  NS_ENSURE_SUCCESS(rv, rv);

  rv = mDBQuery->AddQuery(mFriendList->mDeleteFirstListItemQuery);
  NS_ENSURE_SUCCESS(rv, rv);

  PRUint32 mediaItemId;
  rv = ldbmi->GetMediaItemId(&mediaItemId);
  NS_ENSURE_SUCCESS(rv, rv);

  rv = mDBQuery->BindInt32Parameter(0, mediaItemId);
  NS_ENSURE_SUCCESS(rv, rv);

  *_retval = sbIMediaListEnumerationListener::CONTINUE;

  mItemEnumerated = PR_TRUE;
  return NS_OK;
}

/**
 * See sbIMediaListListener.idl
 */
NS_IMETHODIMP
sbSimpleMediaListRemovingEnumerationListener::OnEnumerationEnd(sbIMediaList* aMediaList,
                                                               nsresult aStatusCode)
{
  nsresult rv;

  // Notify our listeners before removal if we have any
  PRUint32 count = mNotificationList.Count();
  if (mFriendList->ListenerCount() > 0) {
    for (PRUint32 i = 0; i < count; i++) {
      mFriendList->NotifyListenersBeforeItemRemoved(mFriendList,
                                                    mNotificationList[i],
                                                    mNotificationIndexes[i]);
    }
  }

  if (mItemEnumerated) {
    rv = mDBQuery->AddQuery(NS_LITERAL_STRING("commit"));
    NS_ENSURE_SUCCESS(rv, rv);

    PRInt32 dbSuccess;
    rv = mDBQuery->Execute(&dbSuccess);
    NS_ENSURE_SUCCESS(rv, rv);
  }

  // Invalidate the cached list
  rv = mFriendList->GetArray()->Invalidate();
  NS_ENSURE_SUCCESS(rv, rv);

  // Notify our listeners after removal if we have any
  if (mFriendList->ListenerCount() > 0) {
    for (PRUint32 i = 0; i < count; i++) {
      mFriendList->NotifyListenersAfterItemRemoved(mFriendList,
                                                   mNotificationList[i],
                                                   mNotificationIndexes[i]);
    }
  }
  return NS_OK;
}

NS_IMPL_ISUPPORTS_INHERITED3(sbLocalDatabaseSimpleMediaList,
                             sbLocalDatabaseMediaListBase,
                             nsIClassInfo,
                             sbIOrderableMediaList,
                             sbILocalDatabaseSimpleMediaList)

NS_IMPL_CI_INTERFACE_GETTER7(sbLocalDatabaseSimpleMediaList,
                             nsIClassInfo,
                             nsISupportsWeakReference,
                             sbILibraryResource,
                             sbIMediaItem,
                             sbIMediaList,
                             sbILocalDatabaseSimpleMediaList,
                             sbIOrderableMediaList);

sbLocalDatabaseSimpleMediaList::sbLocalDatabaseSimpleMediaList()
{
  MOZ_COUNT_CTOR(sbLocalDatabaseSimpleMediaList);
#ifdef PR_LOGGING
  if (!gLocalDatabaseSimpleMediaListLog) {
    gLocalDatabaseSimpleMediaListLog =
      PR_NewLogModule("sbLocalDatabaseSimpleMediaList");
  }
#endif
}

sbLocalDatabaseSimpleMediaList::~sbLocalDatabaseSimpleMediaList()
{
  MOZ_COUNT_DTOR(sbLocalDatabaseSimpleMediaList);
}

nsresult
sbLocalDatabaseSimpleMediaList::Init(sbLocalDatabaseLibrary* aLibrary,
                                     const nsAString& aGuid)
{
  nsresult rv = sbLocalDatabaseMediaListBase::Init(aLibrary, aGuid);
  NS_ENSURE_SUCCESS(rv, rv);

  SetArray(new sbLocalDatabaseGUIDArray());
  NS_ENSURE_TRUE(GetArray(), NS_ERROR_OUT_OF_MEMORY);

  PRUint32 mediaItemId;
  rv = GetMediaItemId(&mediaItemId);
  NS_ENSURE_SUCCESS(rv, rv);

  nsAutoString databaseGuid;
  rv = mLibrary->GetDatabaseGuid(databaseGuid);
  NS_ENSURE_SUCCESS(rv, rv);

  rv = GetArray()->SetDatabaseGUID(databaseGuid);
  NS_ENSURE_SUCCESS(rv, rv);

  nsCOMPtr<nsIURI> databaseLocation;
  rv = mLibrary->GetDatabaseLocation(getter_AddRefs(databaseLocation));
  NS_ENSURE_SUCCESS(rv, rv);

  if (databaseLocation) {
    rv = GetArray()->SetDatabaseLocation(databaseLocation);
    NS_ENSURE_SUCCESS(rv, rv);
  }

  rv = GetArray()->SetBaseTable(NS_LITERAL_STRING("simple_media_lists"));
  NS_ENSURE_SUCCESS(rv, rv);

  rv = GetArray()->SetBaseConstraintColumn(NS_LITERAL_STRING("media_item_id"));
  NS_ENSURE_SUCCESS(rv, rv);

  rv = GetArray()->SetBaseConstraintValue(mediaItemId);
  NS_ENSURE_SUCCESS(rv, rv);

  rv = GetArray()->AddSort(DEFAULT_SORT_PROPERTY, PR_TRUE);
  NS_ENSURE_SUCCESS(rv, rv);

  rv = GetArray()->SetFetchSize(DEFAULT_FETCH_SIZE);
  NS_ENSURE_SUCCESS(rv, rv);

  nsCOMPtr<sbILocalDatabasePropertyCache> propertyCache;
  rv = aLibrary->GetPropertyCache(getter_AddRefs(propertyCache));
  NS_ENSURE_SUCCESS(rv, rv);

  rv = GetArray()->SetPropertyCache(propertyCache);
  NS_ENSURE_SUCCESS(rv, rv);

  rv = CreateQueries();
  NS_ENSURE_SUCCESS(rv, rv);

  PRBool success = mShouldNotifyAfterRemove.Init();
  NS_ENSURE_TRUE(success, NS_ERROR_OUT_OF_MEMORY);

  return NS_OK;
}

NS_IMETHODIMP
sbLocalDatabaseSimpleMediaList::GetType(nsAString& aType)
{
  aType.Assign(NS_LITERAL_STRING("simple"));
  return NS_OK;
}

NS_IMETHODIMP
sbLocalDatabaseSimpleMediaList::GetItemByGuid(const nsAString& aGuid,
                                              sbIMediaItem** _retval)
{
  NS_ENSURE_ARG_POINTER(_retval);

  nsresult rv;
  nsCOMPtr<sbIMediaItem> item;
  rv = sbLocalDatabaseMediaListBase::GetItemByGuid(aGuid, getter_AddRefs(item));
  NS_ENSURE_SUCCESS(rv, rv);

  PRBool contains;
  rv = Contains(item, &contains);
  NS_ENSURE_SUCCESS(rv, rv);

  if (contains) {
    NS_ADDREF(*_retval = item);
    return NS_OK;
  }
  else {
    return NS_ERROR_NOT_AVAILABLE;
  }

}

NS_IMETHODIMP
sbLocalDatabaseSimpleMediaList::Contains(sbIMediaItem* aMediaItem,
                                         PRBool* _retval)
{
  NS_ENSURE_ARG_POINTER(aMediaItem);
  NS_ENSURE_ARG_POINTER(_retval);
  nsresult rv;
  
  SB_MEDIALIST_LOCK_FULLARRAY_AND_ENSURE_MUTABLE();

  nsString guid;
  rv = aMediaItem->GetGuid(guid);
  NS_ENSURE_SUCCESS(rv, rv);
  
  // Leverage the guid array cache
  rv = GetArray()->ContainsGuid(guid, _retval);
  NS_ENSURE_SUCCESS(rv, rv);
  
  return NS_OK;
}

NS_IMETHODIMP
sbLocalDatabaseSimpleMediaList::Add(sbIMediaItem* aMediaItem)
{
  NS_ENSURE_ARG_POINTER(aMediaItem);

  SB_MEDIALIST_LOCK_FULLARRAY_AND_ENSURE_MUTABLE();

  PRUint32 startingIndex;
  nsresult rv = GetLength(&startingIndex);
  NS_ENSURE_SUCCESS(rv, rv);

  nsString startingOrdinal;
  rv = GetNextOrdinal(startingOrdinal);
  NS_ENSURE_SUCCESS(rv, rv);

  sbSimpleMediaListInsertingEnumerationListener listener(this,
                                                         startingIndex,
                                                         startingOrdinal);

  PRUint16 stepResult;
  rv = listener.OnEnumerationBegin(nsnull, &stepResult);
  NS_ENSURE_SUCCESS(rv, rv);

  rv = listener.OnEnumeratedItem(nsnull, aMediaItem, &stepResult);
  NS_ENSURE_SUCCESS(rv, rv);

  rv = listener.OnEnumerationEnd(nsnull, NS_OK);
  NS_ENSURE_SUCCESS(rv, rv);

  return NS_OK;
}

NS_IMETHODIMP
sbLocalDatabaseSimpleMediaList::AddAll(sbIMediaList* aMediaList)
{
  NS_ENSURE_ARG_POINTER(aMediaList);

  SB_MEDIALIST_LOCK_FULLARRAY_AND_ENSURE_MUTABLE();

  sbAutoBatchHelper batchHelper(*this);

  PRUint32 startingIndex;
  nsresult rv = GetLength(&startingIndex);
  NS_ENSURE_SUCCESS(rv, rv);

  nsString startingOrdinal;
  rv = GetNextOrdinal(startingOrdinal);
  NS_ENSURE_SUCCESS(rv, rv);

  sbSimpleMediaListInsertingEnumerationListener listener(this,
                                                         startingIndex,
                                                         startingOrdinal);
  rv =
    aMediaList->EnumerateAllItems(&listener,
                                  sbIMediaList::ENUMERATIONTYPE_SNAPSHOT);
  NS_ENSURE_SUCCESS(rv, rv);

  return NS_OK;
}

NS_IMETHODIMP
sbLocalDatabaseSimpleMediaList::InsertAllBefore(PRUint32 aIndex,
                                                 sbIMediaList* aMediaList)
{
  NS_ENSURE_ARG_POINTER(aMediaList);

  SB_MEDIALIST_LOCK_FULLARRAY_AND_ENSURE_MUTABLE();

  sbAutoBatchHelper batchHelper(*this);

  PRUint32 startingIndex;
  nsresult rv = GetLength(&startingIndex);
  NS_ENSURE_SUCCESS(rv, rv);

  nsString startingOrdinal;
  rv = GetBeforeOrdinal(aIndex, startingOrdinal);
  NS_ENSURE_SUCCESS(rv, rv);

  sbSimpleMediaListInsertingEnumerationListener listener(this,
                                                         aIndex,
                                                         startingOrdinal);
  rv =
    aMediaList->EnumerateAllItems(&listener,
                                  sbIMediaList::ENUMERATIONTYPE_SNAPSHOT);
  NS_ENSURE_SUCCESS(rv, rv);

  return NS_OK;
}

NS_IMETHODIMP
sbLocalDatabaseSimpleMediaList::AddSome(nsISimpleEnumerator* aMediaItems)
{
  NS_ENSURE_ARG_POINTER(aMediaItems);

  SB_MEDIALIST_LOCK_FULLARRAY_AND_ENSURE_MUTABLE();

  PRUint32 startingIndex;
  nsresult rv = GetLength(&startingIndex);
  NS_ENSURE_SUCCESS(rv, rv);

  nsString startingOrdinal;
  rv = GetNextOrdinal(startingOrdinal);
  NS_ENSURE_SUCCESS(rv, rv);

  sbSimpleMediaListInsertingEnumerationListener listener(this,
                                                         startingIndex,
                                                         startingOrdinal);

  PRUint16 stepResult;
  rv = listener.OnEnumerationBegin(nsnull, &stepResult);
  NS_ENSURE_SUCCESS(rv, rv);

  sbAutoBatchHelper batchHelper(*this);

  PRBool hasMore;
  while (NS_SUCCEEDED(aMediaItems->HasMoreElements(&hasMore)) && hasMore) {
    nsCOMPtr<nsISupports> supports;
    rv = aMediaItems->GetNext(getter_AddRefs(supports));
    SB_CONTINUE_IF_FAILED(rv);

    nsCOMPtr<sbIMediaItem> item = do_QueryInterface(supports, &rv);
    SB_CONTINUE_IF_FAILED(rv);

    rv = listener.OnEnumeratedItem(nsnull, item, &stepResult);
    NS_WARN_IF_FALSE(NS_SUCCEEDED(rv), "OnEnumeratedItem failed!");
  }

  rv = listener.OnEnumerationEnd(nsnull, NS_OK);
  NS_ENSURE_SUCCESS(rv, rv);

  return NS_OK;
}

NS_IMETHODIMP
sbLocalDatabaseSimpleMediaList::InsertBefore(PRUint32 aIndex,
                                             sbIMediaItem* aMediaItem)
{
  NS_ENSURE_ARG_POINTER(aMediaItem);
  SB_MEDIALIST_LOCK_FULLARRAY_AND_ENSURE_MUTABLE();
  SB_ENSURE_INDEX1(aIndex);

  nsString startingOrdinal;
  nsresult rv = GetBeforeOrdinal(aIndex, startingOrdinal);
  NS_ENSURE_SUCCESS(rv, rv);

  sbSimpleMediaListInsertingEnumerationListener listener(this,
                                                         aIndex,
                                                         startingOrdinal);

  PRUint16 stepResult;
  rv = listener.OnEnumerationBegin(nsnull, &stepResult);
  NS_ENSURE_SUCCESS(rv, rv);

  rv = listener.OnEnumeratedItem(nsnull, aMediaItem, &stepResult);
  NS_ENSURE_SUCCESS(rv, rv);

  rv = listener.OnEnumerationEnd(nsnull, NS_OK);
  NS_ENSURE_SUCCESS(rv, rv);

  return NS_OK;
}

NS_IMETHODIMP
sbLocalDatabaseSimpleMediaList::MoveBefore(PRUint32 aFromIndex,
                                           PRUint32 aToIndex)
{
  // Do nothing if we don't have to move
  if (aFromIndex == aToIndex) {
    return NS_OK;
  }

  nsresult rv;

  SB_MEDIALIST_LOCK_FULLARRAY_AND_ENSURE_MUTABLE();
  SB_ENSURE_INDEX2(aFromIndex, aToIndex);

  // Get the ordinal of the space before the to index
  nsAutoString ordinal;
  rv = GetBeforeOrdinal(aToIndex, ordinal);
  NS_ENSURE_SUCCESS(rv, rv);

  rv = UpdateOrdinalByIndex(aFromIndex, ordinal);
  NS_ENSURE_SUCCESS(rv, rv);

  // Invalidate the cached list
  rv = GetArray()->Invalidate();
  NS_ENSURE_SUCCESS(rv, rv);

  nsCOMPtr<sbIMediaList> list =
    do_QueryInterface(NS_ISUPPORTS_CAST(sbILocalDatabaseSimpleMediaList*, this), &rv);
  NS_ENSURE_SUCCESS(rv, rv);

  // If the from index is smaller than the to index, then moving the item out
  // of the from index causes everything to shift once
  if (aFromIndex < aToIndex) {
    aToIndex--;
  }

  sbLocalDatabaseMediaListListener::NotifyListenersItemMoved(list,
                                                             aFromIndex,
                                                             aToIndex);

  return NS_OK;
}

NS_IMETHODIMP
sbLocalDatabaseSimpleMediaList::MoveLast(PRUint32 aIndex)
{
  nsresult rv;

  SB_MEDIALIST_LOCK_FULLARRAY_AND_ENSURE_MUTABLE();
  SB_ENSURE_INDEX1(aIndex);

  // Get the ordinal for the space after the last item in the list
  nsAutoString ordinal;
  rv = GetNextOrdinal(ordinal);
  NS_ENSURE_SUCCESS(rv, rv);

  rv = UpdateOrdinalByIndex(aIndex, ordinal);
  NS_ENSURE_SUCCESS(rv, rv);

  // Grab the length before the invalidation since it won't be changing
  PRUint32 length;
  rv = GetArray()->GetLength(&length);
  NS_ENSURE_SUCCESS(rv, rv);

  // Invalidate the cached list
  rv = GetArray()->Invalidate();
  NS_ENSURE_SUCCESS(rv, rv);

  nsCOMPtr<sbIMediaList> list =
    do_QueryInterface(NS_ISUPPORTS_CAST(sbILocalDatabaseSimpleMediaList*, this), &rv);
  NS_ENSURE_SUCCESS(rv, rv);

  sbLocalDatabaseMediaListListener::NotifyListenersItemMoved(list,
                                                             aIndex,
                                                             length - 1);

  return NS_OK;
}

NS_IMETHODIMP
sbLocalDatabaseSimpleMediaList::InsertSomeBefore(PRUint32 aIndex,
                                                 nsISimpleEnumerator* aMediaItems)
{
  NS_ENSURE_ARG_POINTER(aMediaItems);
  SB_MEDIALIST_LOCK_FULLARRAY_AND_ENSURE_MUTABLE();
  SB_ENSURE_INDEX1(aIndex);

  nsString startingOrdinal;
  nsresult rv = GetBeforeOrdinal(aIndex, startingOrdinal);
  NS_ENSURE_SUCCESS(rv, rv);

  startingOrdinal.AppendLiteral(".0");

  sbSimpleMediaListInsertingEnumerationListener listener(this,
                                                         aIndex,
                                                         startingOrdinal);

  PRUint16 stepResult;
  rv = listener.OnEnumerationBegin(nsnull, &stepResult);
  NS_ENSURE_SUCCESS(rv, rv);

  sbAutoBatchHelper batchHelper(*this);

  PRBool hasMore;
  while (NS_SUCCEEDED(aMediaItems->HasMoreElements(&hasMore)) && hasMore) {
    nsCOMPtr<nsISupports> supports;
    rv = aMediaItems->GetNext(getter_AddRefs(supports));
    SB_CONTINUE_IF_FAILED(rv);

    nsCOMPtr<sbIMediaItem> item = do_QueryInterface(supports, &rv);
    SB_CONTINUE_IF_FAILED(rv);

    rv = listener.OnEnumeratedItem(nsnull, item, &stepResult);
    NS_WARN_IF_FALSE(NS_SUCCEEDED(rv), "OnEnumeratedItem failed!");
  }

  rv = listener.OnEnumerationEnd(nsnull, NS_OK);
  NS_ENSURE_SUCCESS(rv, rv);

  return NS_OK;
}

NS_IMETHODIMP
sbLocalDatabaseSimpleMediaList::MoveSomeBefore(PRUint32* aFromIndexArray,
                                               PRUint32 aFromIndexArrayCount,
                                               PRUint32 aToIndex)
{
  NS_ENSURE_ARG_POINTER(aFromIndexArray);

  nsresult rv;

  SB_MEDIALIST_LOCK_FULLARRAY_AND_ENSURE_MUTABLE();
  SB_ENSURE_INDEX1(aToIndex);

  nsAutoString ordinal;
  rv = GetBeforeOrdinal(aToIndex, ordinal);
  NS_ENSURE_SUCCESS(rv, rv);

  ordinal.AppendLiteral(".");
  rv = MoveSomeInternal(aFromIndexArray,
                        aFromIndexArrayCount,
                        aToIndex,
                        ordinal);
  NS_ENSURE_SUCCESS(rv, rv);

  return NS_OK;
}

NS_IMETHODIMP
sbLocalDatabaseSimpleMediaList::MoveSomeLast(PRUint32* aIndexArray,
                                             PRUint32 aIndexArrayCount)
{
  SB_MEDIALIST_LOCK_FULLARRAY_AND_ENSURE_MUTABLE();

  NS_ENSURE_ARG_POINTER(aIndexArray);

  nsAutoString ordinal;
  nsresult rv = GetNextOrdinal(ordinal);
  NS_ENSURE_SUCCESS(rv, rv);

  ordinal.AppendLiteral(".");

  PRUint32 length;
  rv = GetArray()->GetLength(&length);
  NS_ENSURE_SUCCESS(rv, rv);

  rv = MoveSomeInternal(aIndexArray, aIndexArrayCount, length, ordinal);
  NS_ENSURE_SUCCESS(rv, rv);

  return NS_OK;
}

NS_IMETHODIMP
sbLocalDatabaseSimpleMediaList::Remove(sbIMediaItem* aMediaItem)
{
  NS_ENSURE_ARG_POINTER(aMediaItem);

  SB_MEDIALIST_LOCK_FULLARRAY_AND_ENSURE_MUTABLE();

  // Use our enumeration helper to make all these remove calls use the same
  // code.
  sbSimpleMediaListRemovingEnumerationListener listener(this);

  PRUint16 stepResult;
  nsresult rv = listener.OnEnumerationBegin(nsnull, &stepResult);
  NS_ENSURE_SUCCESS(rv, rv);

  rv = listener.OnEnumeratedItem(nsnull, aMediaItem, &stepResult);
  NS_ENSURE_SUCCESS(rv, rv);

  rv = listener.OnEnumerationEnd(nsnull, NS_OK);
  NS_ENSURE_SUCCESS(rv, rv);

  return NS_OK;
}

NS_IMETHODIMP
sbLocalDatabaseSimpleMediaList::RemoveByIndex(PRUint32 aIndex)
{
  SB_MEDIALIST_LOCK_FULLARRAY_AND_ENSURE_MUTABLE();

  nsresult rv;

  nsAutoString ordinal;
  rv = GetArray()->GetSortPropertyValueByIndex(aIndex, ordinal);
  NS_ENSURE_SUCCESS(rv, rv);

  nsCOMPtr<sbIMediaItem> item;
  rv = GetItemByIndex(aIndex, getter_AddRefs(item));
  NotifyListenersBeforeItemRemoved(this, item, aIndex);

  nsCOMPtr<sbIDatabaseQuery> dbQuery;
  rv = MakeStandardQuery(getter_AddRefs(dbQuery));
  NS_ENSURE_SUCCESS(rv, rv);

  rv = dbQuery->AddQuery(mDeleteListItemByOrdinalQuery);
  NS_ENSURE_SUCCESS(rv, rv);

  rv = dbQuery->BindStringParameter(0, ordinal);
  NS_ENSURE_SUCCESS(rv, rv);

  PRInt32 dbSuccess;
  rv = dbQuery->Execute(&dbSuccess);
  NS_ENSURE_SUCCESS(rv, rv);

  rv = GetArray()->RemoveByIndex(aIndex);
  NS_ENSURE_SUCCESS(rv, rv);

  nsCOMPtr<sbIMediaList> mediaList =
    do_QueryInterface(NS_ISUPPORTS_CAST(sbILocalDatabaseSimpleMediaList*, this), &rv);
  NS_ENSURE_SUCCESS(rv, rv);

  NotifyListenersAfterItemRemoved(mediaList, item, aIndex);

  return NS_OK;
}

NS_IMETHODIMP
sbLocalDatabaseSimpleMediaList::RemoveSome(nsISimpleEnumerator* aMediaItems)
{
  NS_ENSURE_ARG_POINTER(aMediaItems);

  SB_MEDIALIST_LOCK_FULLARRAY_AND_ENSURE_MUTABLE();

  sbSimpleMediaListRemovingEnumerationListener listener(this);

  PRUint16 stepResult;
  nsresult rv = listener.OnEnumerationBegin(nsnull, &stepResult);
  NS_ENSURE_SUCCESS(rv, rv);

  PRBool hasMore;
  while (NS_SUCCEEDED(aMediaItems->HasMoreElements(&hasMore)) && hasMore) {

    nsCOMPtr<nsISupports> supports;
    rv = aMediaItems->GetNext(getter_AddRefs(supports));
    SB_CONTINUE_IF_FAILED(rv);

    nsCOMPtr<sbIMediaItem> item = do_QueryInterface(supports, &rv);
    SB_CONTINUE_IF_FAILED(rv);

    rv = listener.OnEnumeratedItem(nsnull, item, &stepResult);
    NS_WARN_IF_FALSE(NS_SUCCEEDED(rv), "OnEnumeratedItem failed!");
  }

  rv = listener.OnEnumerationEnd(nsnull, NS_OK);
  NS_ENSURE_SUCCESS(rv, rv);

  return NS_OK;
}

NS_IMETHODIMP
sbLocalDatabaseSimpleMediaList::Clear()
{
  SB_MEDIALIST_LOCK_FULLARRAY_AND_ENSURE_MUTABLE();

  nsresult rv;
  PRInt32 dbOk;

  nsCOMPtr<sbIDatabaseQuery> query;
  rv = MakeStandardQuery(getter_AddRefs(query));
  NS_ENSURE_SUCCESS(rv, rv);

  rv = query->AddQuery(mDeleteAllQuery);
  NS_ENSURE_SUCCESS(rv, rv);

  rv = query->Execute(&dbOk);
  NS_ENSURE_SUCCESS(rv, rv);

  // Invalidate the cached list
  rv = GetArray()->Invalidate();
  NS_ENSURE_SUCCESS(rv, rv);

  nsCOMPtr<sbIMediaList> mediaList =
    do_QueryInterface(NS_ISUPPORTS_CAST(sbILocalDatabaseSimpleMediaList*, this), &rv);
  NS_ENSURE_SUCCESS(rv, rv);

  sbLocalDatabaseMediaListListener::NotifyListenersListCleared(mediaList);

  return NS_OK;
}

// Called when something external changed our content without going through the
// normal means (ie, sbIMediaList). This is not a routine operation, normally
// everything should go through the media list methods, but there are cases, 
// such as smart playlists, where it makes sense to just copy the content of
// one table into another. When this happens, none of the listeners will have
// been notified of anything that may have happened to the items in the list.
// For instance, this includes the medialistview, which does not refresh its
// content. This method first triggers a LISTCLEARED notification, then enters
// batch mode and sends one ITEMADDED per item in the list.
NS_IMETHODIMP
sbLocalDatabaseSimpleMediaList::NotifyContentChanged()
{
  // Invalidate the cached list
  nsresult rv = GetArray()->Invalidate();
  NS_ENSURE_SUCCESS(rv, rv);

  nsCOMPtr<sbIMediaList> mediaList =
    do_QueryInterface(NS_ISUPPORTS_CAST(sbILocalDatabaseSimpleMediaList*, this), &rv);
  NS_ENSURE_SUCCESS(rv, rv);

  // If we have listeners, they will probably want to know about this
  if (ListenerCount() > 0) {
    // Start a batch for both the LISTCLEARED and all the ITEMADDED notifications,
    // so that if a listener needs to differentiate between ContentChanged and
    // a real LISTCLEAR + N*ITEMADDED, the fact that the whole thing happened in
    // one single batch will be its clue.
    sbAutoBatchHelper batchHelper(*this);

    // First, notify listeners that the list has been cleared
    sbLocalDatabaseMediaListListener::NotifyListenersListCleared(mediaList);

    // Then send an ITEMADDED notification for each item that we now have in the list
    PRUint32 length;
    rv = GetArray()->GetLength(&length);
    NS_ENSURE_SUCCESS(rv, rv);

    for (PRUint32 index=0; index<length; index++) {
      nsCOMPtr<sbIMediaItem> item;
      rv = GetItemByIndex(index, getter_AddRefs(item));
      NotifyListenersItemAdded(this, item, index);
    } 
  }
  
  return NS_OK;
}

NS_IMETHODIMP
sbLocalDatabaseSimpleMediaList::CreateView(sbIMediaListViewState* aState,
                                           sbIMediaListView** _retval)
{
  NS_ENSURE_ARG_POINTER(_retval);

  nsresult rv;

  nsAutoString defaultSortProperty;
  rv = GetDefaultSortProperty(defaultSortProperty);
  NS_ENSURE_SUCCESS(rv, rv);

  PRUint32 mediaItemId;
  rv = GetMediaItemId(&mediaItemId);
  NS_ENSURE_SUCCESS(rv, rv);

  nsRefPtr<sbLocalDatabaseMediaListView>
    view(new sbLocalDatabaseMediaListView(mLibrary,
                                          this,
                                          defaultSortProperty,
                                          mediaItemId));
  NS_ENSURE_TRUE(view, NS_ERROR_OUT_OF_MEMORY);
  rv = view->Init(aState);
  NS_ENSURE_SUCCESS(rv, rv);

  NS_ADDREF(*_retval = view);
  return NS_OK;
}

// sbILocalDatabaseSimpleMediaList
NS_IMETHODIMP
sbLocalDatabaseSimpleMediaList::GetCopyListener(
  sbILocalDatabaseMediaListCopyListener * *aCopyListener)
{
  NS_ENSURE_ARG_POINTER(aCopyListener);

  *aCopyListener = nsnull;

  if(mCopyListener) {
    NS_ADDREF(*aCopyListener = mCopyListener);
  }

  return NS_OK;
}

NS_IMETHODIMP sbLocalDatabaseSimpleMediaList::SetCopyListener(
  sbILocalDatabaseMediaListCopyListener * aCopyListener)
{
  NS_ENSURE_ARG_POINTER(aCopyListener);

  mCopyListener = aCopyListener;

  return NS_OK;
}

NS_IMETHODIMP
sbLocalDatabaseSimpleMediaList::GetIndexByOrdinal(const nsAString& aOrdinal,
                                                  PRUint32* _retval)
{
  NS_ENSURE_ARG_POINTER(_retval);

  nsresult rv;

  // First, search the cache for this ordinal
  PRUint32 length;
  rv = GetArray()->GetLength(&length);
  NS_ENSURE_SUCCESS(rv, rv);

  for (PRUint32 i = 0; i < length; i++) {
    PRBool isCached;
    rv = GetArray()->IsIndexCached(i, &isCached);
    NS_ENSURE_SUCCESS(rv, rv);

    if (isCached) {
      nsAutoString ordinal;
      rv = GetArray()->GetOrdinalByIndex(i, ordinal);
      NS_ENSURE_SUCCESS(rv, rv);

      if (ordinal.Equals(aOrdinal)) {
        *_retval = i;
        return NS_OK;
      }
    }
  }

  // Not cached, search the database
  PRUint32 index;
  rv = GetArray()->GetFirstIndexByPrefix(aOrdinal, &index);
  if (NS_SUCCEEDED(rv)) {
    *_retval = index;
    return NS_OK;
  }

  return NS_ERROR_NOT_AVAILABLE;
}

NS_IMETHODIMP
sbLocalDatabaseSimpleMediaList::Invalidate()
{
  nsresult rv = GetArray()->Invalidate();
  NS_ENSURE_SUCCESS(rv, rv);

  nsCOMPtr<sbIMediaList> list =
    do_QueryInterface(NS_ISUPPORTS_CAST(sbILocalDatabaseSimpleMediaList*, this), &rv);
  NS_ENSURE_SUCCESS(rv, rv);

  nsCOMPtr<sbIMediaItem> item = do_QueryInterface(list, &rv);
  NS_ENSURE_SUCCESS(rv, rv);

  nsCOMPtr<sbIMutablePropertyArray> properties =
    do_CreateInstance(SB_MUTABLEPROPERTYARRAY_CONTRACTID, &rv);
  NS_ENSURE_SUCCESS(rv, rv);

  // XXXsteve What should go into the updated properties array?

  sbLocalDatabaseMediaListListener::NotifyListenersItemUpdated(list,
                                                               item,
                                                               properties);
  return NS_OK;
}

NS_IMETHODIMP
sbLocalDatabaseSimpleMediaList::NotifyListenersItemUpdated(sbIMediaItem* aItem,
                                                           PRUint32 aIndex,
                                                           sbIPropertyArray* aProperties)
{
  NS_ENSURE_ARG_POINTER(aItem);
  NS_ENSURE_ARG_POINTER(aProperties);

  nsresult rv;
  nsCOMPtr<sbIMediaList> list =
    do_QueryInterface(NS_ISUPPORTS_CAST(sbILocalDatabaseSimpleMediaList*, this), &rv);
  NS_ENSURE_SUCCESS(rv, rv);
  sbLocalDatabaseMediaListListener::NotifyListenersItemUpdated(list,
                                                               aItem,
                                                               aProperties);
  return NS_OK;
}

NS_IMETHODIMP
sbLocalDatabaseSimpleMediaList::NotifyListenersBeforeItemRemoved(sbIMediaList* aList,
                                                                 sbIMediaItem* aItem,
                                                                 PRUint32 aIndex)
{
  NS_ENSURE_ARG_POINTER(aList);
  NS_ENSURE_ARG_POINTER(aItem);

  sbLocalDatabaseMediaListListener::NotifyListenersBeforeItemRemoved(aList,
                                                                     aItem,
                                                                     aIndex);
  return NS_OK;
}

NS_IMETHODIMP
sbLocalDatabaseSimpleMediaList::NotifyListenersAfterItemRemoved(sbIMediaList* aList,
                                                                sbIMediaItem* aItem,
                                                                PRUint32 aIndex)
{
  NS_ENSURE_ARG_POINTER(aList);
  NS_ENSURE_ARG_POINTER(aItem);

  sbLocalDatabaseMediaListListener::NotifyListenersAfterItemRemoved(aList,
                                                                    aItem,
                                                                    aIndex);
  return NS_OK;
}

NS_IMETHODIMP
sbLocalDatabaseSimpleMediaList::NotifyListenersListCleared(sbIMediaList* aList)
{
  NS_ENSURE_ARG_POINTER(aList);

  sbLocalDatabaseMediaListListener::NotifyListenersListCleared(aList);
  return NS_OK;
}

NS_IMETHODIMP
sbLocalDatabaseSimpleMediaList::NotifyListenersBatchBegin(sbIMediaList* aList)
{
  NS_ENSURE_ARG_POINTER(aList);

  sbLocalDatabaseMediaListListener::NotifyListenersBatchBegin(aList);
  return NS_OK;
}

NS_IMETHODIMP
sbLocalDatabaseSimpleMediaList::NotifyListenersBatchEnd(sbIMediaList* aList)
{
  NS_ENSURE_ARG_POINTER(aList);

  sbLocalDatabaseMediaListListener::NotifyListenersBatchEnd(aList);
  return NS_OK;
}

nsresult
sbLocalDatabaseSimpleMediaList::ExecuteAggregateQuery(const nsAString& aQuery,
                                                      nsAString& aValue)
{
  nsresult rv;
  PRInt32 dbOk;

  nsCOMPtr<sbIDatabaseQuery> query;
  rv = MakeStandardQuery(getter_AddRefs(query));
  NS_ENSURE_SUCCESS(rv, rv);

  rv = query->AddQuery(aQuery);
  NS_ENSURE_SUCCESS(rv, rv);

  rv = query->Execute(&dbOk);
  NS_ENSURE_SUCCESS(rv, rv);

  nsCOMPtr<sbIDatabaseResult> result;
  rv = query->GetResultObject(getter_AddRefs(result));
  NS_ENSURE_SUCCESS(rv, rv);

  PRUint32 rowCount;
  rv = result->GetRowCount(&rowCount);
  NS_ENSURE_SUCCESS(rv, rv);

  if (rowCount == 0) {
    return NS_ERROR_UNEXPECTED;
  }

  rv = result->GetRowCell(0, 0, aValue);
  NS_ENSURE_SUCCESS(rv, rv);

  return NS_OK;
}

nsresult
sbLocalDatabaseSimpleMediaList::UpdateOrdinalByIndex(PRUint32 aIndex,
                                                     const nsAString& aOrdinal)
{
  nsresult rv;
  PRInt32 dbOk;

  // Get the media item id of the item we are moving
  PRUint32 mediaItemId;
  rv = GetArray()->GetMediaItemIdByIndex(aIndex, &mediaItemId);
  NS_ENSURE_SUCCESS(rv, rv);

  // Get the old ordinal
  nsString oldOrdinal;
  rv = GetArray()->GetOrdinalByIndex(aIndex, oldOrdinal);
  NS_ENSURE_SUCCESS(rv, rv);

  // Update the item at the from index with the new ordinal
  nsCOMPtr<sbIDatabaseQuery> query;
  rv = MakeStandardQuery(getter_AddRefs(query));
  NS_ENSURE_SUCCESS(rv, rv);

  rv = query->AddQuery(mUpdateListItemOrdinalQuery);
  NS_ENSURE_SUCCESS(rv, rv);

  rv = query->BindStringParameter(0, aOrdinal);
  NS_ENSURE_SUCCESS(rv, rv);

  rv = query->BindInt32Parameter(1, mediaItemId);
  NS_ENSURE_SUCCESS(rv, rv);

  rv = query->BindStringParameter(2, oldOrdinal);
  NS_ENSURE_SUCCESS(rv, rv);

  rv = query->Execute(&dbOk);
  NS_ENSURE_SUCCESS(rv, rv);

  return NS_OK;
}

nsresult
sbLocalDatabaseSimpleMediaList::MoveSomeInternal(PRUint32* aFromIndexArray,
                                                 PRUint32 aFromIndexArrayCount,
                                                 PRUint32 aToIndex,
                                                 const nsAString& aOrdinalRoot)
{
  NS_ASSERTION(aFromIndexArray, "aFromIndexArray is null");

  PRUint32 length;
  nsresult rv = GetArray()->GetLength(&length);
  NS_ENSURE_SUCCESS(rv, rv);

  // Make sure all the from array indexes are legal
  for (PRUint32 i = 0; i < aFromIndexArrayCount; i++) {
    NS_ENSURE_ARG_MAX(aFromIndexArray[i], length - 1);
  }

  nsCOMPtr<sbIDatabaseQuery> query;
  rv = MakeStandardQuery(getter_AddRefs(query));
  NS_ENSURE_SUCCESS(rv, rv);

  sbAutoBatchHelper batchHelper(*this);

  rv = query->AddQuery(NS_LITERAL_STRING("begin"));
  NS_ENSURE_SUCCESS(rv, rv);

  for (PRUint32 i = 0; i < aFromIndexArrayCount; i++) {
    nsAutoString ordinal(aOrdinalRoot);
    ordinal.AppendInt(i);

    PRUint32 mediaItemId;
    rv = GetArray()->GetMediaItemIdByIndex(aFromIndexArray[i], &mediaItemId);
    NS_ENSURE_SUCCESS(rv, rv);

    nsString oldOrdinal;
    rv = GetArray()->GetOrdinalByIndex(aFromIndexArray[i], oldOrdinal);
    NS_ENSURE_SUCCESS(rv, rv);

    rv = query->AddQuery(mUpdateListItemOrdinalQuery);
    NS_ENSURE_SUCCESS(rv, rv);

    rv = query->BindStringParameter(0, ordinal);
    NS_ENSURE_SUCCESS(rv, rv);

    rv = query->BindInt32Parameter(1, mediaItemId);
    NS_ENSURE_SUCCESS(rv, rv);

    rv = query->BindStringParameter(2, oldOrdinal);
    NS_ENSURE_SUCCESS(rv, rv);
  }

  rv = query->AddQuery(NS_LITERAL_STRING("commit"));
  NS_ENSURE_SUCCESS(rv, rv);

  PRInt32 dbOk;
  rv = query->Execute(&dbOk);
  NS_ENSURE_SUCCESS(rv, rv);

  // Invalidate the cached list
  rv = GetArray()->Invalidate();
  NS_ENSURE_SUCCESS(rv, rv);

  nsCOMPtr<sbIMediaList> list =
    do_QueryInterface(NS_ISUPPORTS_CAST(sbILocalDatabaseSimpleMediaList*, this), &rv);
  NS_ENSURE_SUCCESS(rv, rv);

  nsTArray<PRUint32> shiftedIndexes;
  PRUint32* success = shiftedIndexes.AppendElements(aFromIndexArray,
                                                    aFromIndexArrayCount);
  NS_ENSURE_TRUE(success, NS_ERROR_OUT_OF_MEMORY);

  for (PRUint32 i = 0; i < aFromIndexArrayCount; i++) {
    PRUint32 fromIndex = shiftedIndexes[i];

    // If this item has moved forward, the we shift the to-index back once since
    // the incoming indexes are all "move before" indicies
    PRUint32 shift = 0;
    if (fromIndex < aToIndex) {
      shift = 1;
    }

    sbLocalDatabaseMediaListListener::NotifyListenersItemMoved(list,
                                                               fromIndex,
                                                               aToIndex - shift);

    // If this moved shifted the remaining indexes to be notified, shift them
    for (PRUint32 j = i + 1; j < aFromIndexArrayCount; j++) {
      // The moved item has jumped behind this index, causing this index to
      // shift forward.  Shift it back.
      if (fromIndex < shiftedIndexes[j] && aToIndex > shiftedIndexes[j]) {
        shiftedIndexes[j]--;
      }
      else {
        // The moved item has jumped in front of this index, causing this index
        // to shift backwards.  Shift it forward.
        if (aToIndex <= shiftedIndexes[j] && fromIndex > shiftedIndexes[j]) {
          shiftedIndexes[j]++;
        }
      }
    }

    // If we're moving indexes backwards, we need to adjust the insert point
    // so the next item is put after the previous
    if (fromIndex > aToIndex) {
      aToIndex++;
    }

  }

  return NS_OK;
}

nsresult
sbLocalDatabaseSimpleMediaList::DeleteItemByMediaItemId(PRUint32 aMediaItemId)
{
  nsresult rv;
  PRInt32 dbOk;

  nsCOMPtr<sbIDatabaseQuery> query;
  rv = MakeStandardQuery(getter_AddRefs(query));
  NS_ENSURE_SUCCESS(rv, rv);

  rv = query->AddQuery(mDeleteFirstListItemQuery);
  NS_ENSURE_SUCCESS(rv, rv);

  rv = query->BindInt32Parameter(0, aMediaItemId);
  NS_ENSURE_SUCCESS(rv, rv);

  rv = query->Execute(&dbOk);
  NS_ENSURE_SUCCESS(rv, rv);

  return NS_OK;
}

nsresult
sbLocalDatabaseSimpleMediaList::GetNextOrdinal(nsAString& aValue)
{
  nsresult rv;

  PRUint32 length;
  rv = GetArray()->GetLength(&length);
  NS_ENSURE_SUCCESS(rv, rv);

  if (length == 0) {
    aValue.AssignLiteral("0");
    return NS_OK;
  }

  PRBool cached;
  rv = GetArray()->IsIndexCached(length - 1, &cached);
  NS_ENSURE_SUCCESS(rv, rv);

  if (cached) {
    rv = GetArray()->GetSortPropertyValueByIndex(length - 1, aValue);
    NS_ENSURE_SUCCESS(rv, rv);
  }
  else {
    rv = ExecuteAggregateQuery(mGetLastOrdinalQuery, aValue);
    NS_ENSURE_SUCCESS(rv, rv);
  }

  rv = AddToLastPathSegment(aValue, 1);
  NS_ENSURE_SUCCESS(rv, rv);

  return NS_OK;
}

nsresult
sbLocalDatabaseSimpleMediaList::GetBeforeOrdinal(PRUint32 aIndex,
                                                 nsAString& aValue)
{
  nsresult rv;

  // If we want to insert before the first index, get the ordinal of the
  // first index and trim off everything but the first path and subtract 1
  if (aIndex == 0) {
    PRBool cached;
    rv = GetArray()->IsIndexCached(0, &cached);
    NS_ENSURE_SUCCESS(rv, rv);

    nsAutoString ordinal;
    if (cached) {
      rv = GetArray()->GetSortPropertyValueByIndex(0, ordinal);
      NS_ENSURE_SUCCESS(rv, rv);
    }
    else {
      rv = ExecuteAggregateQuery(mGetFirstOrdinalQuery, ordinal);
      NS_ENSURE_SUCCESS(rv, rv);
    }

    // Trim off additional path segments, if any
    PRUint32 pos = ordinal.FindChar('.');
    if (pos >= 0) {
      ordinal.SetLength(pos);
    }

    PRInt32 value = ordinal.ToInteger(&rv) - 1;
    NS_ENSURE_SUCCESS(rv, rv);

    nsAutoString newOrdinal;
    newOrdinal.AppendInt(value);
    aValue = newOrdinal;

    return NS_OK;
  }

  // Find the ordinals before and after the place we want to insert
  nsAutoString aboveOrdinal;
  nsAutoString belowOrdinal;

  rv = GetArray()->GetSortPropertyValueByIndex(aIndex - 1, aboveOrdinal);
  NS_ENSURE_SUCCESS(rv, rv);

  rv = GetArray()->GetSortPropertyValueByIndex(aIndex, belowOrdinal);
  NS_ENSURE_SUCCESS(rv, rv);

  PRUint32 aboveLevels = CountLevels(aboveOrdinal);
  PRUint32 belowLevels = CountLevels(belowOrdinal);

  // If the two paths are to the same level, create a new level on the
  // below path to make a path that sorts between
  if (aboveLevels == belowLevels) {
    belowOrdinal.AppendLiteral(".0");
    aValue = belowOrdinal;
    return NS_OK;
  }

  // Otherwise if the below path is longer than the above path, decrement the
  // last path segment of the below path
  if (belowLevels > aboveLevels) {
    rv = AddToLastPathSegment(belowOrdinal, -1);
    NS_ENSURE_SUCCESS(rv, rv);
    aValue = belowOrdinal;
    return NS_OK;
  }

  // If the above path is longer than the below, increment the last path
  // segment of the above path
  rv = AddToLastPathSegment(aboveOrdinal, 1);
  NS_ENSURE_SUCCESS(rv, rv);
  aValue = aboveOrdinal;
  return NS_OK;
}

nsresult
sbLocalDatabaseSimpleMediaList::AddToLastPathSegment(nsAString& aPath,
                                                     PRInt32 aToAdd)
{

  PRUint32 startPos = aPath.RFindChar('.') + 1;
  PRUint32 length = aPath.Length() - startPos;

  nsresult rv;
  PRInt32 value = Substring(aPath, startPos, length).ToInteger(&rv);
  NS_ENSURE_SUCCESS(rv, rv);

  value += aToAdd;

  nsAutoString newValue;
  newValue.AppendInt(value);
  aPath.Replace(startPos, length, newValue);

  return NS_OK;
}

PRUint32
sbLocalDatabaseSimpleMediaList::CountLevels(const nsAString& aPath)
{
  PRUint32 count = 0;
  PRInt32 foundpos = aPath.FindChar('.');
  while(foundpos >= 0) {
    count++;
    foundpos = aPath.FindChar('.', foundpos + 1);
  }
  return count;
}

nsresult
sbLocalDatabaseSimpleMediaList::CreateQueries()
{
  nsresult rv;

  PRUint32 mediaItemId;
  rv = GetMediaItemId(&mediaItemId);
  NS_ENSURE_SUCCESS(rv, rv);

  // The following builder is shared between queries that operate on the
  // items in this list
  nsCOMPtr<sbISQLSelectBuilder> builder =
    do_CreateInstance(SB_SQLBUILDER_SELECT_CONTRACTID, &rv);
  NS_ENSURE_SUCCESS(rv, rv);

  nsCOMPtr<sbISQLBuilderCriterion> criterion;

  rv = builder->SetBaseTableName(NS_LITERAL_STRING("media_items"));
  NS_ENSURE_SUCCESS(rv, rv);

  rv = builder->SetBaseTableAlias(NS_LITERAL_STRING("_mi"));
  NS_ENSURE_SUCCESS(rv, rv);

  rv = builder->AddJoin(sbISQLSelectBuilder::JOIN_INNER,
                        NS_LITERAL_STRING("simple_media_lists"),
                        NS_LITERAL_STRING("_sml"),
                        NS_LITERAL_STRING("member_media_item_id"),
                        NS_LITERAL_STRING("_mi"),
                        NS_LITERAL_STRING("media_item_id"));
  NS_ENSURE_SUCCESS(rv, rv);

  rv = builder->CreateMatchCriterionLong(NS_LITERAL_STRING("_sml"),
                                         NS_LITERAL_STRING("media_item_id"),
                                         sbISQLSelectBuilder::MATCH_EQUALS,
                                         mediaItemId,
                                         getter_AddRefs(criterion));
  NS_ENSURE_SUCCESS(rv, rv);

  rv = builder->AddCriterion(criterion);
  NS_ENSURE_SUCCESS(rv, rv);

  // Create get last ordinal query
  rv = builder->ClearColumns();
  NS_ENSURE_SUCCESS(rv, rv);

  rv = builder->AddColumn(EmptyString(),
                          NS_LITERAL_STRING("max(ordinal)"));
  NS_ENSURE_SUCCESS(rv, rv);

  rv = builder->ToString(mGetLastOrdinalQuery);
  NS_ENSURE_SUCCESS(rv, rv);

  // Create get first ordinal query
  rv = builder->ClearColumns();
  NS_ENSURE_SUCCESS(rv, rv);

  rv = builder->AddColumn(EmptyString(),
                          NS_LITERAL_STRING("min(ordinal)"));
  NS_ENSURE_SUCCESS(rv, rv);

  rv = builder->ToString(mGetFirstOrdinalQuery);
  NS_ENSURE_SUCCESS(rv, rv);

  // Create the query used by contains to see if a guid is in this list
  rv = builder->ClearColumns();
  NS_ENSURE_SUCCESS(rv, rv);

  rv = builder->AddColumn(NS_LITERAL_STRING("_mi"),
                          NS_LITERAL_STRING("media_item_id"));
  NS_ENSURE_SUCCESS(rv, rv);

  rv = builder->CreateMatchCriterionParameter(NS_LITERAL_STRING("_mi"),
                                              NS_LITERAL_STRING("guid"),
                                              sbISQLSelectBuilder::MATCH_EQUALS,
                                              getter_AddRefs(criterion));
  NS_ENSURE_SUCCESS(rv, rv);

  rv = builder->AddCriterion(criterion);
  NS_ENSURE_SUCCESS(rv, rv);

  rv = builder->ToString(mGetMediaItemIdForGuidQuery);
  NS_ENSURE_SUCCESS(rv, rv);

  // Create insertion query
  nsCOMPtr<sbISQLInsertBuilder> insert =
    do_CreateInstance(SB_SQLBUILDER_INSERT_CONTRACTID, &rv);

  rv = insert->SetIntoTableName(NS_LITERAL_STRING("simple_media_lists"));
  NS_ENSURE_SUCCESS(rv, rv);

  rv = insert->AddColumn(NS_LITERAL_STRING("media_item_id"));
  NS_ENSURE_SUCCESS(rv, rv);

  rv = insert->AddColumn(NS_LITERAL_STRING("member_media_item_id"));
  NS_ENSURE_SUCCESS(rv, rv);

  rv = insert->AddColumn(NS_LITERAL_STRING("ordinal"));
  NS_ENSURE_SUCCESS(rv, rv);

  rv = insert->AddValueLong(mediaItemId);
  NS_ENSURE_SUCCESS(rv, rv);

  rv = insert->AddValueParameter();
  NS_ENSURE_SUCCESS(rv, rv);

  rv = insert->AddValueParameter();
  NS_ENSURE_SUCCESS(rv, rv);

  rv = insert->ToString(mInsertIntoListQuery);
  NS_ENSURE_SUCCESS(rv, rv);

  // Create ordinal update query
  nsCOMPtr<sbISQLUpdateBuilder> update =
    do_CreateInstance(SB_SQLBUILDER_UPDATE_CONTRACTID, &rv);

  rv = update->SetTableName(NS_LITERAL_STRING("simple_media_lists"));
  NS_ENSURE_SUCCESS(rv, rv);

  rv = update->AddAssignmentParameter(NS_LITERAL_STRING("ordinal"));
  NS_ENSURE_SUCCESS(rv, rv);

  rv = update->CreateMatchCriterionLong(EmptyString(),
                                        NS_LITERAL_STRING("media_item_id"),
                                        sbISQLSelectBuilder::MATCH_EQUALS,
                                        mediaItemId,
                                        getter_AddRefs(criterion));
  NS_ENSURE_SUCCESS(rv, rv);

  rv = update->AddCriterion(criterion);
  NS_ENSURE_SUCCESS(rv, rv);

  rv = update->CreateMatchCriterionParameter(EmptyString(),
                                             NS_LITERAL_STRING("member_media_item_id"),
                                             sbISQLSelectBuilder::MATCH_EQUALS,
                                             getter_AddRefs(criterion));
  NS_ENSURE_SUCCESS(rv, rv);

  rv = update->AddCriterion(criterion);
  NS_ENSURE_SUCCESS(rv, rv);

  rv = update->CreateMatchCriterionParameter(EmptyString(),
                                             NS_LITERAL_STRING("ordinal"),
                                             sbISQLSelectBuilder::MATCH_EQUALS,
                                             getter_AddRefs(criterion));
  NS_ENSURE_SUCCESS(rv, rv);

  rv = update->AddCriterion(criterion);
  NS_ENSURE_SUCCESS(rv, rv);

  rv = update->ToString(mUpdateListItemOrdinalQuery);
  NS_ENSURE_SUCCESS(rv, rv);

  // Create first item delete query
  // delete from
  //   simple_media_lists
  // where
  //   media_item_id = x and
  //   ordinal in (
  //     select
  //       ordinal
  //     from
  //       simple_media_lists
  //     where
  //       member_media_item_id = ?
  //     order by ordinal limit 1)
  nsCOMPtr<sbISQLDeleteBuilder> deleteb =
    do_CreateInstance(SB_SQLBUILDER_DELETE_CONTRACTID, &rv);

  rv = deleteb->SetTableName(NS_LITERAL_STRING("simple_media_lists"));
  NS_ENSURE_SUCCESS(rv, rv);

  rv = deleteb->CreateMatchCriterionLong(EmptyString(),
                                         NS_LITERAL_STRING("media_item_id"),
                                         sbISQLSelectBuilder::MATCH_EQUALS,
                                         mediaItemId,
                                         getter_AddRefs(criterion));
  NS_ENSURE_SUCCESS(rv, rv);

  rv = deleteb->AddCriterion(criterion);
  NS_ENSURE_SUCCESS(rv, rv);

  // Build the subquery
  nsCOMPtr<sbISQLSelectBuilder> subquery =
    do_CreateInstance(SB_SQLBUILDER_SELECT_CONTRACTID, &rv);
  NS_ENSURE_SUCCESS(rv, rv);

  rv = subquery->AddColumn(EmptyString(), NS_LITERAL_STRING("rowid"));
  NS_ENSURE_SUCCESS(rv, rv);

  rv = subquery->SetBaseTableName(NS_LITERAL_STRING("simple_media_lists"));
  NS_ENSURE_SUCCESS(rv, rv);

  rv = subquery->CreateMatchCriterionParameter(EmptyString(),
                                               NS_LITERAL_STRING("member_media_item_id"),
                                               sbISQLSelectBuilder::MATCH_EQUALS,
                                               getter_AddRefs(criterion));
  NS_ENSURE_SUCCESS(rv, rv);

  rv = subquery->AddCriterion(criterion);
  NS_ENSURE_SUCCESS(rv, rv);

  rv = subquery->CreateMatchCriterionLong(EmptyString(),
                                          NS_LITERAL_STRING("media_item_id"),
                                          sbISQLSelectBuilder::MATCH_EQUALS,
                                          mediaItemId,
                                          getter_AddRefs(criterion));
  NS_ENSURE_SUCCESS(rv, rv);

  rv = subquery->AddCriterion(criterion);
  NS_ENSURE_SUCCESS(rv, rv);

  rv = subquery->AddOrder(EmptyString(), NS_LITERAL_STRING("ordinal"), PR_TRUE);
  NS_ENSURE_SUCCESS(rv, rv);

  rv = subquery->SetLimit(1);
  NS_ENSURE_SUCCESS(rv, rv);

  nsCOMPtr<sbISQLBuilderCriterionIn> inCriterion;
  rv = deleteb->CreateMatchCriterionIn(EmptyString(),
                                       NS_LITERAL_STRING("rowid"),
                                       getter_AddRefs(inCriterion));
  NS_ENSURE_SUCCESS(rv, rv);

  rv = inCriterion->AddSubquery(subquery);
  NS_ENSURE_SUCCESS(rv, rv);

  rv = deleteb->AddCriterion(inCriterion);
  NS_ENSURE_SUCCESS(rv, rv);

  rv = deleteb->ToString(mDeleteFirstListItemQuery);
  NS_ENSURE_SUCCESS(rv, rv);

  // Create item delete by ordinal query
  rv = deleteb->Reset();
  NS_ENSURE_SUCCESS(rv, rv);

  rv = deleteb->SetTableName(NS_LITERAL_STRING("simple_media_lists"));
  NS_ENSURE_SUCCESS(rv, rv);

  rv = deleteb->CreateMatchCriterionLong(EmptyString(),
                                         NS_LITERAL_STRING("media_item_id"),
                                         sbISQLSelectBuilder::MATCH_EQUALS,
                                         mediaItemId,
                                         getter_AddRefs(criterion));
  NS_ENSURE_SUCCESS(rv, rv);

  rv = deleteb->AddCriterion(criterion);
  NS_ENSURE_SUCCESS(rv, rv);

  rv = deleteb->CreateMatchCriterionParameter(EmptyString(),
                                              NS_LITERAL_STRING("ordinal"),
                                              sbISQLSelectBuilder::MATCH_EQUALS,
                                              getter_AddRefs(criterion));
  NS_ENSURE_SUCCESS(rv, rv);

  rv = deleteb->AddCriterion(criterion);
  NS_ENSURE_SUCCESS(rv, rv);

  rv = deleteb->ToString(mDeleteListItemByOrdinalQuery);
  NS_ENSURE_SUCCESS(rv, rv);

  // Create delete all query
  rv = deleteb->Reset();
  NS_ENSURE_SUCCESS(rv, rv);

  rv = deleteb->SetTableName(NS_LITERAL_STRING("simple_media_lists"));
  NS_ENSURE_SUCCESS(rv, rv);

  rv = deleteb->CreateMatchCriterionLong(EmptyString(),
                                         NS_LITERAL_STRING("media_item_id"),
                                         sbISQLSelectBuilder::MATCH_EQUALS,
                                         mediaItemId,
                                         getter_AddRefs(criterion));
  NS_ENSURE_SUCCESS(rv, rv);

  rv = deleteb->AddCriterion(criterion);
  NS_ENSURE_SUCCESS(rv, rv);

  rv = deleteb->ToString(mDeleteAllQuery);
  NS_ENSURE_SUCCESS(rv, rv);

  return NS_OK;
}

nsresult
sbLocalDatabaseSimpleMediaList::NotifyCopyListener(sbIMediaItem *aSourceItem,
                                                   sbIMediaItem *aDestItem)
{
  NS_ENSURE_ARG_POINTER(aSourceItem);
  NS_ENSURE_ARG_POINTER(aDestItem);

  if(mCopyListener) {
    return mCopyListener->OnItemCopied(aSourceItem, aDestItem);
  }

  return NS_OK;
}


NS_IMETHODIMP
sbLocalDatabaseSimpleMediaList::GetDefaultSortProperty(nsAString& aProperty)
{
  aProperty.Assign(DEFAULT_SORT_PROPERTY);
  return NS_OK;
}

// nsIClassInfo
NS_IMETHODIMP
sbLocalDatabaseSimpleMediaList::GetInterfaces(PRUint32* count, nsIID*** array)
{
  return NS_CI_INTERFACE_GETTER_NAME(sbLocalDatabaseSimpleMediaList)(count, array);
}

NS_IMETHODIMP
sbLocalDatabaseSimpleMediaList::GetHelperForLanguage(PRUint32 language,
                                                     nsISupports** _retval)
{
  *_retval = nsnull;
  return NS_OK;
}

NS_IMETHODIMP
sbLocalDatabaseSimpleMediaList::GetContractID(char** aContractID)
{
  *aContractID = nsnull;
  return NS_OK;
}

NS_IMETHODIMP
sbLocalDatabaseSimpleMediaList::GetClassDescription(char** aClassDescription)
{
  *aClassDescription = nsnull;
  return NS_OK;
}

NS_IMETHODIMP
sbLocalDatabaseSimpleMediaList::GetClassID(nsCID** aClassID)
{
  *aClassID = nsnull;
  return NS_OK;
}

NS_IMETHODIMP
sbLocalDatabaseSimpleMediaList::GetImplementationLanguage(PRUint32* aImplementationLanguage)
{
  *aImplementationLanguage = nsIProgrammingLanguage::CPLUSPLUS;
  return NS_OK;
}

NS_IMETHODIMP
sbLocalDatabaseSimpleMediaList::GetFlags(PRUint32 *aFlags)
{
  *aFlags = nsIClassInfo::THREADSAFE;
  return NS_OK;
}

NS_IMETHODIMP
sbLocalDatabaseSimpleMediaList::GetClassIDNoAlloc(nsCID* aClassIDNoAlloc)
{
  return NS_ERROR_NOT_AVAILABLE;
}

