/*
 *=BEGIN SONGBIRD GPL
 *
 * This file is part of the Songbird web player.
 *
 * Copyright(c) 2005-2010 POTI, Inc.
 * http://www.songbirdnest.com
 *
 * This file may be licensed under the terms of of the
 * GNU General Public License Version 2 (the ``GPL'').
 *
 * Software distributed under the License is distributed
 * on an ``AS IS'' basis, WITHOUT WARRANTY OF ANY KIND, either
 * express or implied. See the GPL for the specific language
 * governing rights and limitations.
 *
 * You should have received a copy of the GPL along with this
 * program. If not, go to http://www.gnu.org/licenses/gpl.html
 * or write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 *
 *=END SONGBIRD GPL
 */

/** 
* \file  sbMediacoreWrapper.cpp
* \brief Songbird Mediacore Wrapper Implementation.
*/
#include "sbMediacoreWrapper.h"

#include <nsIClassInfoImpl.h>
#include <nsIDOMDataContainerEvent.h>
#include <nsIDOMDocument.h>
#include <nsIProgrammingLanguage.h>

#include <nsComponentManagerUtils.h>
#include <nsMemory.h>
#include <prlog.h>

#include <IWindowCloak.h>

#include <sbProxiedComponentManager.h>
#include <sbStringUtils.h>
#include <sbVariantUtils.h>

#include "sbBaseMediacoreEventTarget.h"

/**
 * To log this module, set the following environment variable:
 *   NSPR_LOG_MODULES=sbMediacoreWrapper:5
 */
#ifdef PR_LOGGING
static PRLogModuleInfo* gMediacoreWrapper = nsnull;
#define TRACE(args) PR_LOG(gMediacoreWrapper, PR_LOG_DEBUG, args)
#define LOG(args)   PR_LOG(gMediacoreWrapper, PR_LOG_WARN, args)
#else
#define TRACE(args) /* nothing */
#define LOG(args)   /* nothing */
#endif

NS_IMPL_THREADSAFE_ADDREF(sbMediacoreWrapper)
NS_IMPL_THREADSAFE_RELEASE(sbMediacoreWrapper)

NS_IMPL_QUERY_INTERFACE8_CI(sbMediacoreWrapper,
                            sbIMediacore,
                            sbIMediacorePlaybackControl,
                            sbIMediacoreVolumeControl,
                            sbIMediacoreVotingParticipant,
                            sbIMediacoreEventTarget,
                            sbIMediacoreWrapper,
                            nsIDOMEventListener,
                            nsIClassInfo)

NS_IMPL_CI_INTERFACE_GETTER7(sbMediacoreWrapper,
                             sbIMediacore,
                             sbIMediacorePlaybackControl,
                             sbIMediacoreVolumeControl,
                             sbIMediacoreVotingParticipant,
                             sbIMediacoreEventTarget,
                             sbIMediacoreWrapper,
                             nsIClassInfo)

NS_DECL_CLASSINFO(sbMediacoreWrapper)
NS_IMPL_THREADSAFE_CI(sbMediacoreWrapper)

sbMediacoreWrapper::sbMediacoreWrapper()
: mBaseEventTarget(new sbBaseMediacoreEventTarget(this))
, mWindowIsReady(PR_FALSE)
{
#ifdef PR_LOGGING
  if (!gMediacoreWrapper)
    gMediacoreWrapper = PR_NewLogModule("sbMediacoreWrapper");
#endif
  TRACE(("sbMediacoreWrapper[0x%x] - Created", this));
}

sbMediacoreWrapper::~sbMediacoreWrapper()
{
  TRACE(("sbMediacoreWrapper[0x%x] - Destroyed", this));
}

nsresult
sbMediacoreWrapper::Init()
{
  TRACE(("sbMediacoreWrapper[0x%x] - Init", this));

  nsresult rv = sbBaseMediacore::InitBaseMediacore();
  NS_ENSURE_SUCCESS(rv, rv);

  rv = sbBaseMediacorePlaybackControl::InitBaseMediacorePlaybackControl();
  NS_ENSURE_SUCCESS(rv, rv);

  rv = sbBaseMediacoreVolumeControl::InitBaseMediacoreVolumeControl();
  NS_ENSURE_SUCCESS(rv, rv);

  return NS_OK;
}

//
// sbBaseMediacore overrides
//

/*virtual*/ nsresult 
sbMediacoreWrapper::OnInitBaseMediacore()
{
  return NS_OK;
}

/*virtual*/ nsresult 
sbMediacoreWrapper::OnGetCapabilities()
{
  return NS_OK;
}

/*virtual*/ nsresult 
sbMediacoreWrapper::OnShutdown()
{
  nsresult rv = NS_ERROR_UNEXPECTED;

  nsCOMPtr<sbIWindowCloak> windowCloak = 
    do_ProxiedGetService("@songbirdnest.com/Songbird/WindowCloak;1", &rv);
  NS_ENSURE_SUCCESS(rv, rv);

  rv = windowCloak->Uncloak(mPluginHostWindow);
  NS_ENSURE_SUCCESS(rv, rv);

  rv = RemoveSelfDOMListener();
  NS_ENSURE_SUCCESS(rv, rv);

  rv = mPrompter->Cancel();
  NS_ENSURE_SUCCESS(rv, rv);

  return NS_OK;
}

//
// sbBaseMediacorePlaybackControl overrides
//

/*virtual*/ nsresult 
sbMediacoreWrapper::OnInitBaseMediacorePlaybackControl()
{
  return NS_OK;
}

/*virtual*/ nsresult 
sbMediacoreWrapper::OnSetUri(nsIURI *aURI)
{
  NS_ENSURE_ARG_POINTER(aURI);

  nsCString uriSpec;
  nsresult rv = aURI->GetSpec(uriSpec);
  NS_ENSURE_SUCCESS(rv, rv);

  rv = 
    SendDOMEvent(NS_LITERAL_STRING("seturi"), uriSpec);
  NS_ENSURE_SUCCESS(rv, rv);

  return NS_OK;
}

/*virtual*/ nsresult
sbMediacoreWrapper::OnGetDuration(PRUint64 *aDuration) 
{
  nsresult rv = 
    SendDOMEvent(NS_LITERAL_STRING("getduration"), EmptyString());
  NS_ENSURE_SUCCESS(rv, rv);

  // XXXAus: Get return value?

  return NS_OK;
}

/*virtual*/ nsresult 
sbMediacoreWrapper::OnGetPosition(PRUint64 *aPosition)
{
  nsresult rv = 
    SendDOMEvent(NS_LITERAL_STRING("getposition"), EmptyString());
  NS_ENSURE_SUCCESS(rv, rv);

  // XXXAus: Get return value?

  return NS_OK;
}

/*virtual*/ nsresult 
sbMediacoreWrapper::OnSetPosition(PRUint64 aPosition)
{
  nsresult rv = 
    SendDOMEvent(NS_LITERAL_STRING("setposition"), sbAutoString(aPosition));
  NS_ENSURE_SUCCESS(rv, rv);

  return NS_OK;
}

/*virtual*/ nsresult 
sbMediacoreWrapper::OnGetIsPlayingAudio(PRBool *aIsPlayingAudio)
{
  nsresult rv = 
    SendDOMEvent(NS_LITERAL_STRING("getisplayingaudio"), EmptyString());
  NS_ENSURE_SUCCESS(rv, rv);

  // XXXAus: Get return value?

  return NS_OK;
}

/*virtual*/ nsresult 
sbMediacoreWrapper::OnGetIsPlayingVideo(PRBool *aIsPlayingVideo)
{
  nsresult rv = 
    SendDOMEvent(NS_LITERAL_STRING("getisplayingvideo"), EmptyString());
  NS_ENSURE_SUCCESS(rv, rv);

  // XXXAus: Get return value?

  return NS_OK;
}

/*virtual*/ nsresult 
sbMediacoreWrapper::OnPlay()
{
  nsresult rv = 
    SendDOMEvent(NS_LITERAL_STRING("play"), EmptyString());
  NS_ENSURE_SUCCESS(rv, rv);

  // XXXAus: on success, send STREAM_START mediacore event.

  return NS_OK;
}

/*virtual*/ nsresult 
sbMediacoreWrapper::OnPause()
{
  nsresult rv = 
    SendDOMEvent(NS_LITERAL_STRING("pause"), EmptyString());
  NS_ENSURE_SUCCESS(rv, rv);

  // XXXAus: on success, send STREAM_PAUSE mediacore event.

  return NS_OK;
}

/*virtual*/ nsresult 
sbMediacoreWrapper::OnStop()
{
  nsresult rv = 
    SendDOMEvent(NS_LITERAL_STRING("stop"), EmptyString());
  NS_ENSURE_SUCCESS(rv, rv);

  // XXXAus: on success, send STREAM_STOP mediacore event.

  return NS_OK;
}

/*virtual*/ nsresult
sbMediacoreWrapper::OnSeek(PRUint64 aPosition, PRUint32 aFlags)
{
  return OnSetPosition(aPosition);
}

//
// sbBaseMediacoreVolumeControl overrides
//

/*virtual*/ nsresult 
sbMediacoreWrapper::OnInitBaseMediacoreVolumeControl()
{
  return NS_OK;
}

/*virtual*/ nsresult 
sbMediacoreWrapper::OnSetMute(PRBool aMute)
{
  nsresult rv = 
    SendDOMEvent(NS_LITERAL_STRING("setmute"), sbAutoString(aMute));
  NS_ENSURE_SUCCESS(rv, rv);

  return NS_OK;
}
  
/*virtual*/ nsresult 
sbMediacoreWrapper::OnSetVolume(PRFloat64 aVolume)
{
  nsCString volStr;
  SB_ConvertFloatVolToJSStringValue(aVolume, volStr);
  
  nsresult rv = SendDOMEvent(NS_LITERAL_STRING("setvolume"), volStr);
  NS_ENSURE_SUCCESS(rv, rv);
  
  return NS_OK;
}

//
// sbIMediacoreVotingParticipant
//

NS_IMETHODIMP
sbMediacoreWrapper::VoteWithURI(nsIURI *aURI, PRUint32 *_retval)
{
  NS_ENSURE_ARG_POINTER(aURI);
  NS_ENSURE_ARG_POINTER(_retval);

  nsCString uriSpec;
  nsresult rv = aURI->GetSpec(uriSpec);
  NS_ENSURE_SUCCESS(rv, rv);

  rv = SendDOMEvent(NS_LITERAL_STRING("votewithuri"), uriSpec);
  NS_ENSURE_SUCCESS(rv, rv);
  
  *_retval = 0;

  return NS_OK;
}

NS_IMETHODIMP
sbMediacoreWrapper::VoteWithChannel(nsIChannel *aChannel, PRUint32 *_retval)
{
  return NS_ERROR_NOT_IMPLEMENTED;
}

//
// sbIMediacoreWrapper
//

NS_IMETHODIMP
sbMediacoreWrapper::Initialize(const nsAString &aInstanceName, 
                               sbIMediacoreCapabilities *aCapabilities,
                               const nsACString &aChromePageURL)
{
  NS_ENSURE_ARG_POINTER(aCapabilities);

  nsresult rv = SetInstanceName(aInstanceName);
  NS_ENSURE_SUCCESS(rv, rv);

  rv = SetCapabilities(aCapabilities);
  NS_ENSURE_SUCCESS(rv, rv);

  nsCOMPtr<sbIPrompter> prompter = 
    do_CreateInstance(SONGBIRD_PROMPTER_CONTRACTID, &rv);
  NS_ENSURE_SUCCESS(rv, rv);

  prompter.swap(mPrompter);

  nsCOMPtr<nsIDOMWindow> domWindow;
  rv = mPrompter->OpenWindow(nsnull, 
                             NS_ConvertUTF8toUTF16(aChromePageURL), 
                             aInstanceName,
                             NS_LITERAL_STRING("chrome,centerscreen,resizable"),
                             nsnull,
                             getter_AddRefs(domWindow));
  NS_ENSURE_SUCCESS(rv, rv);

  domWindow.swap(mPluginHostWindow);

  nsCOMPtr<nsIDOMEventTarget> domEventTarget = 
    do_QueryInterface(mPluginHostWindow, &rv);
  NS_ENSURE_SUCCESS(rv, rv);

  domEventTarget.swap(mDOMEventTarget);

  rv = AddSelfDOMListener();
  NS_ENSURE_SUCCESS(rv, rv);

  // Wait for window to be ready.
  nsCOMPtr<nsIThread> target;
  rv = NS_GetMainThread(getter_AddRefs(target));
  NS_ENSURE_SUCCESS(rv, rv);

  PRBool processed = PR_FALSE;
  while(!mWindowIsReady) {
    rv = target->ProcessNextEvent(PR_FALSE, &processed);
    NS_ENSURE_SUCCESS(rv, rv);
  }

  nsCOMPtr<nsIDOMDocument> document;
  rv = mPluginHostWindow->GetDocument(getter_AddRefs(document));
  NS_ENSURE_SUCCESS(rv, rv);

  nsCOMPtr<nsIDOMDocumentEvent> documentEvent = 
    do_QueryInterface(document, &rv);
  NS_ENSURE_SUCCESS(rv, rv);

  documentEvent.swap(mDocumentEvent);

  return NS_OK;
}

//
// nsIDOMEventListener
//

NS_IMETHODIMP
sbMediacoreWrapper::HandleEvent(nsIDOMEvent *aEvent)
{
  NS_ENSURE_ARG_POINTER(aEvent);

  nsString eventType;
  nsresult rv = aEvent->GetType(eventType);
  NS_ENSURE_SUCCESS(rv, rv);

  if(eventType.EqualsLiteral("resize")) {
    mWindowIsReady = PR_TRUE;
    
    // XXXAus: This seems to break things... :(

    //nsCOMPtr<sbIWindowCloak> windowCloak = 
    //  do_ProxiedGetService("@songbirdnest.com/Songbird/WindowCloak;1", &rv);
    //NS_ENSURE_SUCCESS(rv, rv);

    //rv = windowCloak->Cloak(mPluginHostWindow);
    //NS_ENSURE_SUCCESS(rv, rv);
  }

  return NS_OK;
}

//
// sbMediacoreWrapper
//
nsresult 
sbMediacoreWrapper::AddSelfDOMListener()
{
  nsresult rv = NS_ERROR_UNEXPECTED;
  nsCOMPtr<nsIDOMEventTarget> target = do_QueryInterface(mPluginHostWindow, &rv);
  NS_ENSURE_SUCCESS(rv, rv);
  
  rv = target->AddEventListener(NS_LITERAL_STRING("resize"), this, PR_FALSE);
  NS_ENSURE_SUCCESS(rv, rv);

  return NS_OK;
}

nsresult 
sbMediacoreWrapper::RemoveSelfDOMListener()
{
  nsresult rv = NS_ERROR_UNEXPECTED;
  nsCOMPtr<nsIDOMEventTarget> target = do_QueryInterface(mPluginHostWindow, &rv);
  NS_ENSURE_SUCCESS(rv, rv);

  rv = target->RemoveEventListener(NS_LITERAL_STRING("resize"), this, PR_FALSE);
  NS_ENSURE_SUCCESS(rv, rv);

  return NS_OK;
}

nsresult 
sbMediacoreWrapper::SendDOMEvent(const nsAString &aEventName, 
                                 const nsAString &aEventData)
{
  nsCOMPtr<nsIDOMEvent> domEvent;
  nsresult rv = mDocumentEvent->CreateEvent(NS_LITERAL_STRING("DataContainerEvent"), 
                                            getter_AddRefs(domEvent));
  NS_ENSURE_SUCCESS(rv, rv);

  nsCOMPtr<nsIDOMDataContainerEvent> dataEvent = 
    do_QueryInterface(domEvent, &rv);
  NS_ENSURE_SUCCESS(rv, rv);

  rv = domEvent->InitEvent(aEventName, PR_TRUE, PR_TRUE);
  NS_ENSURE_SUCCESS(rv, rv);

  rv = dataEvent->SetData(NS_LITERAL_STRING("data"), 
                          sbNewVariant(aEventData).get());
  NS_ENSURE_SUCCESS(rv, rv);

  PRBool handled = PR_FALSE;
  rv = mDOMEventTarget->DispatchEvent(dataEvent, &handled);
  NS_ENSURE_SUCCESS(rv, rv);
  NS_ENSURE_TRUE(handled, NS_ERROR_UNEXPECTED);

  return NS_OK;
}

nsresult 
sbMediacoreWrapper::SendDOMEvent(const nsAString &aEventName, 
                                 const nsACString &aEventData)
{
  nsresult rv = SendDOMEvent(aEventName, NS_ConvertUTF8toUTF16(aEventData));
  NS_ENSURE_SUCCESS(rv, rv);
  return NS_OK;
}