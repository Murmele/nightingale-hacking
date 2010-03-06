/*
 *=BEGIN SONGBIRD GPL
 *
 * This file is part of the Songbird web player.
 *
 * Copyright(c) 2005-2009 POTI, Inc.
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

#include "sbDeviceTranscoding.h"

// Mozilla includes
#include <nsArrayUtils.h>
#include <nsComponentManagerUtils.h>
#include <nsIInputStream.h>
#include <nsIIOService.h>
#include <nsIStringEnumerator.h>
#include <nsISupportsPrimitives.h>
#include <nsIWritablePropertyBag2.h>
#include <nsIVariant.h>

#include <nsServiceManagerUtils.h>

// Songbird interfaces
#include <sbIDeviceEvent.h>
#include <sbIJobCancelable.h>
#include <sbIMediacoreEventTarget.h>
#include <sbIMediaInspector.h>
#include <sbITranscodeAlbumArt.h>
#include <sbITranscodeError.h>
#include <sbITranscodeManager.h>
#include <sbITranscodeVideoJob.h>
#include <sbITranscodingConfigurator.h>

// Songbird includes
#include <sbProxiedComponentManager.h>
#include <sbStandardProperties.h>
#include <sbStringUtils.h>
#include <sbTranscodeUtils.h>
#include <sbVariantUtils.h>

// Local includes
#include "sbDeviceStatusHelper.h"
#include "sbDeviceUtils.h"
#include "sbTranscodeProgressListener.h"

/*
 * To log this module, set the following environment variable:
 *   NSPR_LOG_MODULES=sbBaseDevice:5
 */
#undef LOG
#undef TRACE
#ifdef PR_LOGGING
extern PRLogModuleInfo* gBaseDeviceLog;
#define LOG(args)   PR_LOG(gBaseDeviceLog, PR_LOG_WARN,  args)
#define TRACE(args) PR_LOG(gBaseDeviceLog, PR_LOG_DEBUG, args)
#else
#define LOG(args)  do{ } while(0)
#define TRACE(args) do { } while(0)
#endif

sbDeviceTranscoding::sbDeviceTranscoding(sbBaseDevice * aBaseDevice) :
  mBaseDevice(aBaseDevice)
{
}

nsresult
sbDeviceTranscoding::GetSupportedTranscodeProfiles(nsIArray **aSupportedProfiles)
{
  nsresult rv;
  if (!mTranscodeProfiles) {
    rv = sbDeviceUtils::GetSupportedTranscodeProfiles(
                          mBaseDevice,
                          getter_AddRefs(mTranscodeProfiles));
    NS_ENSURE_SUCCESS(rv, rv);
  }

  NS_IF_ADDREF(*aSupportedProfiles = mTranscodeProfiles);

  return NS_OK;
}

nsresult
sbDeviceTranscoding::SelectTranscodeProfile(PRUint32 transcodeType,
                                            sbITranscodeProfile **aProfile)
{
  nsresult rv;

  PRBool hasProfilePref = PR_FALSE;
  // See if we have a preference for the transcoding profile.
  nsCOMPtr<nsIVariant> profileIdVariant;
  nsString prefProfileId;
  rv = mBaseDevice->GetPreference(
                              NS_LITERAL_STRING("transcode_profile.profile_id"),
                              getter_AddRefs(profileIdVariant));
  if (NS_SUCCEEDED(rv))
  {
    hasProfilePref = PR_TRUE;
    rv = profileIdVariant->GetAsAString(prefProfileId);
    NS_ENSURE_SUCCESS(rv, rv);
    TRACE(("%s: found a profile", __FUNCTION__));
  }

  nsCOMPtr<nsIArray> supportedProfiles;
  rv = mBaseDevice->GetSupportedTranscodeProfiles(getter_AddRefs(supportedProfiles));
  NS_ENSURE_SUCCESS(rv, rv);

  PRUint32 bestPriority = 0;
  nsCOMPtr<sbITranscodeProfile> bestProfile;
  nsCOMPtr<sbITranscodeProfile> prefProfile;

  PRUint32 length;
  rv = supportedProfiles->GetLength(&length);
  NS_ENSURE_SUCCESS(rv, rv);

  for (PRUint32 index = 0; index < length; ++index) {
    nsCOMPtr<sbITranscodeProfile> profile =
        do_QueryElementAt(supportedProfiles, index, &rv);
    NS_ENSURE_SUCCESS(rv, rv);

    if (profile) {
      PRUint32 profileContentType;
      rv = profile->GetType(&profileContentType);
      NS_ENSURE_SUCCESS(rv, rv);

      // If the content types don't match, skip (we don't want to use a video
      // transcoding profile to transcode audio, for example)
      if (profileContentType == transcodeType) {
        if (hasProfilePref) {
          nsString profileId;
          rv = profile->GetId(profileId);
          NS_ENSURE_SUCCESS(rv, rv);

          if (profileId.Equals(prefProfileId))
            prefProfile = profile;
        }

        // Also track the highest-priority profile. This is our default.
        PRUint32 priority;
        rv = profile->GetPriority(&priority);
        NS_ENSURE_SUCCESS(rv, rv);

        if (!bestProfile || priority > bestPriority) {
          bestProfile = profile;
          bestPriority = priority;
        }
      }
      else {
        TRACE(("%s: skipping profile for content type %d",
                __FUNCTION__,
                profileContentType));
      }
    }
  }
  if (prefProfile) {
    // We found the profile selected in the preferences. Apply relevant
    // preferenced properties to it as well...
    nsCOMPtr<nsIArray> audioProperties;
    rv = prefProfile->GetAudioProperties(getter_AddRefs(audioProperties));
    NS_ENSURE_SUCCESS(rv, rv);
    rv = sbDeviceUtils::ApplyPropertyPreferencesToProfile(
            mBaseDevice,
            audioProperties,
            NS_LITERAL_STRING("transcode_profile.audio_properties"));
    NS_ENSURE_SUCCESS(rv, rv);

    nsCOMPtr<nsIArray> videoProperties;
    rv = prefProfile->GetVideoProperties(getter_AddRefs(videoProperties));
    NS_ENSURE_SUCCESS(rv, rv);
    rv = sbDeviceUtils::ApplyPropertyPreferencesToProfile(
            mBaseDevice,
            videoProperties,
            NS_LITERAL_STRING("transcode_profile.video_properties"));
    NS_ENSURE_SUCCESS(rv, rv);

    nsCOMPtr<nsIArray> containerProperties;
    rv = prefProfile->GetContainerProperties(
            getter_AddRefs(containerProperties));
    NS_ENSURE_SUCCESS(rv, rv);
    rv = sbDeviceUtils::ApplyPropertyPreferencesToProfile(
            mBaseDevice,
            containerProperties,
            NS_LITERAL_STRING("transcode_profile.container_properties"));
    NS_ENSURE_SUCCESS(rv, rv);

    prefProfile.forget(aProfile);
    TRACE(("%s: found pref profile", __FUNCTION__));
    return NS_OK;
  }
  else if (bestProfile) {
    TRACE(("%s: using best-match profile", __FUNCTION__));
    bestProfile.forget(aProfile);
    return NS_OK;
  }

  // Indicate no appropriate transcoding profile available
  TRACE(("%s: no supported profiles available", __FUNCTION__));
  return NS_ERROR_NOT_AVAILABLE;
}

/**
 * Process a batch in preparation for transcoding, figuring out which items
 * need transcoding.
 */

nsresult
sbDeviceTranscoding::PrepareBatchForTranscoding(Batch & aBatch)
{
  TRACE(("%s", __FUNCTION__));
  nsresult rv;

  if (aBatch.empty()) {
    return NS_OK;
  }

  nsCOMPtr<nsIArray> imageFormats;
  rv = mBaseDevice->GetSupportedAlbumArtFormats(getter_AddRefs(imageFormats));
  // No album art formats isn't fatal.
  if (rv != NS_ERROR_NOT_AVAILABLE) {
    NS_ENSURE_SUCCESS(rv, rv);
  }

  // Iterate over the batch getting the transcode profiles if needed.
  Batch::iterator end = aBatch.end();
  Batch::iterator iter = aBatch.begin();
  while (iter != end) {
    // Check for abort.
    if (mBaseDevice->IsRequestAbortedOrDeviceDisconnected()) {
      return NS_ERROR_ABORT;
    }

    // Get request and skip it if it's a write playlist track request
    TransferRequest * const request = *iter++;
    if (request->IsPlaylist())
      continue;

    // First, ensure that the item isn't DRM protected before looking for
    // transcode profiles.
    if (sbDeviceUtils::IsItemDRMProtected(request->item)) {
      PRBool isSupported = PR_FALSE;
      rv = mBaseDevice->SupportsMediaItemDRM(
          request->item,
          PR_TRUE,  // report errors
          &isSupported);
      if (NS_SUCCEEDED(rv) && isSupported) {
        request->destinationCompatibility =
          sbBaseDevice::TransferRequest::COMPAT_SUPPORTED;
      }
      else {
        request->destinationCompatibility =
          sbBaseDevice::TransferRequest::COMPAT_UNSUPPORTED;
      }
    }
    else {
      rv = FindTranscodeProfile(request->item,
                                &request->transcodeProfile,
                                &request->destinationCompatibility);
      // Treat no profiles available as not needing transcoding
      if (NS_FAILED(rv)) {
        TRACE(("%s: no transcode profile available", __FUNCTION__));
        request->destinationCompatibility =
          sbBaseDevice::TransferRequest::COMPAT_UNSUPPORTED;
      }
      if (request->transcodeProfile) {
        TRACE(("%s: transcoding needed", __FUNCTION__));
        request->destinationCompatibility =
          sbBaseDevice::TransferRequest::COMPAT_NEEDS_TRANSCODING;
      }

      request->albumArt = do_CreateInstance(
              SONGBIRD_TRANSCODEALBUMART_CONTRACTID, &rv);
      NS_ENSURE_SUCCESS(rv, rv);

      // It's ok for this to fail; album art is optional
      rv = request->albumArt->Init(request->item, imageFormats);
      if (NS_FAILED(rv)) {
        TRACE(("%s: no album art available", __FUNCTION__));
        request->albumArt = nsnull;
      }
    }
  }

  return NS_OK;
}

PRUint32
sbDeviceTranscoding::GetTranscodeType(sbIMediaItem * aMediaItem)
{
  nsresult rv;

  nsString contentType;
  rv = aMediaItem->GetContentType(contentType);
  NS_ENSURE_SUCCESS(rv, sbITranscodeProfile::TRANSCODE_TYPE_UNKNOWN);

  if (contentType.Equals(NS_LITERAL_STRING("audio"))) {
    return sbITranscodeProfile::TRANSCODE_TYPE_AUDIO;
  }
  else if (contentType.Equals(NS_LITERAL_STRING("video"))) {
    return sbITranscodeProfile::TRANSCODE_TYPE_AUDIO_VIDEO;
  }
  else if (contentType.Equals(NS_LITERAL_STRING("image"))) {
    return sbITranscodeProfile::TRANSCODE_TYPE_IMAGE;
  }
  NS_WARNING("sbDeviceUtils::GetTranscodeType: "
             "returning unknown transcoding type");
  return sbITranscodeProfile::TRANSCODE_TYPE_UNKNOWN;
}

/**
 * Helper function to dispatch a transcode error to a device
 * @param aError The transcode error to dispatch
 * @param aDevice The device to dispatch the transcode error to
 */
static nsresult
DispatchTranscodeError(sbITranscodeError* aError,
                       sbBaseDevice* aDevice)
{
  NS_ENSURE_ARG_POINTER(aError);
  NS_ENSURE_ARG_POINTER(aDevice);

  nsresult rv;

  nsCOMPtr<nsIWritablePropertyBag2> bag =
    do_CreateInstance("@songbirdnest.com/moz/xpcom/sbpropertybag;1", &rv);
  NS_ENSURE_SUCCESS(rv, rv);
  nsCOMPtr<nsISupportsString> errorString(do_QueryInterface(aError));
  NS_ENSURE_TRUE(errorString, NS_ERROR_NO_INTERFACE);
  nsString message;
  rv = errorString->GetData(message);
  if (NS_SUCCEEDED(rv)) {
    rv = bag->SetPropertyAsAString(NS_LITERAL_STRING("message"),
                                   message);
    NS_ENSURE_SUCCESS(rv, rv);
  }
  rv = bag->SetPropertyAsInterface(NS_LITERAL_STRING("transcode-error"),
                                   NS_ISUPPORTS_CAST(sbITranscodeError*, aError));
  NS_ENSURE_SUCCESS(rv, rv);

  aDevice->CreateAndDispatchEvent(
      sbIDeviceEvent::EVENT_DEVICE_TRANSCODE_ERROR,
      sbNewVariant(bag));
  /* ignore result */
  return NS_OK;
}

nsresult
sbDeviceTranscoding::FindTranscodeProfile(sbIMediaItem * aMediaItem,
                                          sbITranscodeProfile ** aProfile,
                                          CompatibilityType * aDeviceCompatibility)
{
  TRACE(("%s", __FUNCTION__));
  NS_ENSURE_ARG_POINTER(aMediaItem);
  NS_ENSURE_ARG_POINTER(aProfile);
  NS_ENSURE_ARG_POINTER(aDeviceCompatibility);

  nsresult rv;
  *aProfile = nsnull;
  *aDeviceCompatibility = TransferRequest::COMPAT_UNSUPPORTED;

  if (sbDeviceUtils::IsItemDRMProtected(aMediaItem)) {
    // Transcoding from DRM formats is not supported.
    return NS_ERROR_NOT_AVAILABLE;
  }

  PRUint32 const transcodeType = GetTranscodeType(aMediaItem);
  bool needsTranscoding = false;

  if (transcodeType == sbITranscodeProfile::TRANSCODE_TYPE_AUDIO_VIDEO) {
    nsCOMPtr<sbIMediaFormat> mediaFormat;
    rv = GetMediaFormat(aMediaItem, getter_AddRefs(mediaFormat));
    if (NS_FAILED(rv)) {
      nsString inputUri;
      nsresult rv2;
      rv2 = aMediaItem->GetProperty(NS_LITERAL_STRING(SB_PROPERTY_CONTENTURL),
                                    inputUri);
      NS_ENSURE_SUCCESS(rv2, rv2);
      nsTArray<nsString> params;
      params.AppendElement(inputUri);
      SBLocalizedString message("transcode.error.generic", params);
      nsCOMPtr<sbITranscodeError> error;
      rv2 = SB_NewTranscodeError(message,
                                 message,
                                 SBVoidString(),
                                 inputUri,
                                 aMediaItem,
                                 getter_AddRefs(error));
      NS_ENSURE_SUCCESS(rv2, rv2);
      rv2 = DispatchTranscodeError(error, mBaseDevice);
      NS_ENSURE_SUCCESS(rv2, rv2);
    }
    if (rv == NS_ERROR_NOT_AVAILABLE) {
      return rv;
    }
    NS_ENSURE_SUCCESS(rv, rv);
    rv = sbDeviceUtils::DoesItemNeedTranscoding(transcodeType,
                                                mediaFormat,
                                                mBaseDevice,
                                                needsTranscoding);
    NS_ENSURE_SUCCESS(rv, rv);

    if (!needsTranscoding) {
      *aDeviceCompatibility = TransferRequest::COMPAT_SUPPORTED;
      return NS_OK;
    }

    // XXX MOOK this needs to be fixed to be not gstreamer specific
    nsCOMPtr<nsIURI> inputUri;
    rv = aMediaItem->GetContentSrc(getter_AddRefs(inputUri));
    NS_ENSURE_SUCCESS(rv, rv);
    nsCOMPtr<sbIDeviceTranscodingConfigurator> configurator =
      do_CreateInstance("@songbirdnest.com/Songbird/Mediacore/Transcode/Configurator/Device/GStreamer;1", &rv);
    NS_ENSURE_SUCCESS(rv, rv);
    rv = configurator->SetInputUri(inputUri);
    NS_ENSURE_SUCCESS(rv, rv);
    nsCOMPtr<sbIDevice> device =
      do_QueryInterface(NS_ISUPPORTS_CAST(sbIDevice*, mBaseDevice), &rv);
    NS_ENSURE_SUCCESS(rv, rv);
    rv = configurator->SetDevice(device);
    NS_ENSURE_SUCCESS(rv, rv);

    rv = configurator->DetermineOutputType();
    if (NS_SUCCEEDED(rv)) {
      *aDeviceCompatibility = TransferRequest::COMPAT_NEEDS_TRANSCODING;
    }
    else {
      // we need transcoding, but we don't have anything available to do it with
      nsCOMPtr<sbITranscodeError> error;
      rv = configurator->GetLastError(getter_AddRefs(error));
      if (NS_SUCCEEDED(rv) && error) {
        rv = error->SetDestItem(aMediaItem);
        NS_ENSURE_SUCCESS(rv, rv);
        rv = DispatchTranscodeError(error, mBaseDevice);
        NS_ENSURE_SUCCESS(rv, rv);
      }
    }
    return NS_OK;
  }

  // TODO: GetFormatTypeForItem is depreciated. Once the media inspector
  // service is finished this should go away
  sbExtensionToContentFormatEntry_t formatType;
  PRUint32 bitRate = 0;
  PRUint32 sampleRate = 0;
  rv = sbDeviceUtils::GetFormatTypeForItem(aMediaItem,
                                           formatType,
                                           bitRate,
                                           sampleRate);
  if (NS_FAILED(rv)) {
    // we can't figure out what this audio-only file is; dispatch an error
    // message to the user before aborting.
    nsresult rv2 = rv;
    nsString inputUri;
    rv = aMediaItem->GetProperty(NS_LITERAL_STRING(SB_PROPERTY_CONTENTURL),
                                 inputUri);
    NS_ENSURE_SUCCESS(rv, rv);
    nsTArray<nsString> params;
    params.AppendElement(inputUri);
    SBLocalizedString message("transcode.error.generic", params);
    nsString detailMessage;
    detailMessage.AppendInt(rv2, 16);
    nsCOMPtr<sbITranscodeError> error;
    rv = SB_NewTranscodeError(message, message, detailMessage,
                              inputUri, aMediaItem, getter_AddRefs(error));
    NS_ENSURE_SUCCESS(rv, rv);
    rv = DispatchTranscodeError(error, mBaseDevice);
    NS_ENSURE_SUCCESS(rv, rv);
    NS_ENSURE_SUCCESS(rv2, rv2);
  }

  // Check for expected error, unable to find format type
  rv = sbDeviceUtils::DoesItemNeedTranscoding(formatType,
                                              bitRate,
                                              sampleRate,
                                              mBaseDevice,
                                              needsTranscoding);
  NS_ENSURE_SUCCESS(rv, rv);

  if (!needsTranscoding) {
    *aDeviceCompatibility = TransferRequest::COMPAT_SUPPORTED;
    return NS_OK;
  }

  rv = SelectTranscodeProfile(transcodeType, aProfile);
  if (NS_SUCCEEDED(rv)) {
    *aDeviceCompatibility = TransferRequest::COMPAT_NEEDS_TRANSCODING;
  }
  else {
    nsString detailMessage;
    detailMessage.AppendInt(rv, 16);
    nsString inputUri;
    rv = aMediaItem->GetProperty(NS_LITERAL_STRING(SB_PROPERTY_CONTENTURL),
                                 inputUri);
    NS_ENSURE_SUCCESS(rv, rv);
    nsCOMPtr<sbITranscodeError> error;
    rv = SB_NewTranscodeError(NS_LITERAL_STRING("transcode.error.no_profile.hasitem"),
                              NS_LITERAL_STRING("transcode.error.no_profile.withoutitem"),
                              detailMessage,
                              inputUri,
                              aMediaItem,
                              getter_AddRefs(error));
    NS_ENSURE_SUCCESS(rv, rv);
    rv = DispatchTranscodeError(error, mBaseDevice);
    NS_ENSURE_SUCCESS(rv, rv);
  }

  return NS_OK;
}

nsresult
sbDeviceTranscoding::GetMediaFormat(sbIMediaItem* aMediaItem,
                                    sbIMediaFormat** aMediaFormat)
{
  nsresult rv;
  if (!mMediaInspector) {
    mMediaInspector = do_CreateInstance(SB_MEDIAINSPECTOR_CONTRACTID, &rv);
    NS_ENSURE_SUCCESS(rv, rv);
  }

  nsCOMPtr<sbIMediaFormat> mediaFormat;

  rv = mMediaInspector->InspectMedia(aMediaItem, getter_AddRefs(mediaFormat));
  NS_ENSURE_SUCCESS(rv, rv);

  mediaFormat.forget(aMediaFormat);

  return NS_OK;
}

nsresult
sbDeviceTranscoding::GetMediaInspector(sbIMediaInspector** _retval)
{
  nsresult rv;
  if (!mMediaInspector) {
    mMediaInspector = do_CreateInstance(SB_MEDIAINSPECTOR_CONTRACTID, &rv);
    NS_ENSURE_SUCCESS(rv, rv);
  }
  NS_ADDREF(*_retval = mMediaInspector);
  return NS_OK;
}

nsresult
sbDeviceTranscoding::TranscodeVideoItem(
                                     sbITranscodeVideoJob * aVideoJob,
                                     sbBaseDevice::TransferRequest * aRequest,
                                     nsIURI * aDestinationURI)
{
  NS_ENSURE_ARG_POINTER(aVideoJob);
  NS_ENSURE_ARG_POINTER(aRequest);
  NS_ENSURE_ARG_POINTER(aDestinationURI);

  nsresult rv;

  nsCString destinationURISpec;
  rv = aDestinationURI->GetSpec(destinationURISpec);
  NS_ENSURE_SUCCESS(rv, rv);
  rv = aVideoJob->SetDestURI(NS_ConvertUTF8toUTF16(destinationURISpec));
  NS_ENSURE_SUCCESS(rv, rv);

  nsCOMPtr<nsIURI> sourceURI;
  rv = aRequest->item->GetContentSrc(getter_AddRefs(sourceURI));
  NS_ENSURE_SUCCESS(rv, rv);
  nsCString sourceURISpec;
  rv = sourceURI->GetSpec(sourceURISpec);
  NS_ENSURE_SUCCESS(rv, rv);
  rv = aVideoJob->SetSourceURI(NS_ConvertUTF8toUTF16(sourceURISpec));
  NS_ENSURE_SUCCESS(rv, rv);

  nsCOMPtr<sbIPropertyArray> metadata;
  rv = aRequest->item->GetProperties(nsnull, getter_AddRefs(metadata));
  NS_ENSURE_SUCCESS(rv, rv);
  rv = aVideoJob->SetMetadata(metadata);
  NS_ENSURE_SUCCESS(rv, rv);

  // Setup the configurator
  // XXX MOOK this needs to be fixed to be not gstreamer specific
  nsCOMPtr<sbIDeviceTranscodingConfigurator> configurator =
    do_CreateInstance("@songbirdnest.com/Songbird/Mediacore/Transcode/Configurator/Device/GStreamer;1", &rv);
  NS_ENSURE_SUCCESS(rv, rv);
  rv = configurator->SetInputUri(sourceURI);
  NS_ENSURE_SUCCESS(rv, rv);
  nsCOMPtr<sbIDevice> device =
    do_QueryInterface(NS_ISUPPORTS_CAST(sbIDevice*, mBaseDevice), &rv);
  NS_ENSURE_SUCCESS(rv, rv);
  rv = configurator->SetDevice(device);
  NS_ENSURE_SUCCESS(rv, rv);

  nsCOMPtr<sbITranscodingConfigurator> qiConfigurator =
    do_QueryInterface(configurator, &rv);
  NS_ENSURE_SUCCESS(rv, rv);

  rv = aVideoJob->SetConfigurator(qiConfigurator);
  NS_ENSURE_SUCCESS(rv, rv);

  return NS_OK;
}

nsresult
sbDeviceTranscoding::TranscodeAudioItem(
                                        sbITranscodeJob * aAudioJob,
                                        sbBaseDevice::TransferRequest * aRequest,
                                        nsIURI * aDestinationURI)
{
  NS_ENSURE_ARG_POINTER(aAudioJob);
  NS_ENSURE_ARG_POINTER(aRequest);
  NS_ENSURE_ARG_POINTER(aDestinationURI);

  nsresult rv;
  rv = aAudioJob->SetProfile(aRequest->transcodeProfile);
  NS_ENSURE_SUCCESS(rv, rv);

  nsCString destinationURISpec;
  rv = aDestinationURI->GetSpec(destinationURISpec);
  NS_ENSURE_SUCCESS(rv, rv);
  rv = aAudioJob->SetDestURI(NS_ConvertUTF8toUTF16(destinationURISpec));
  NS_ENSURE_SUCCESS(rv, rv);

  nsCOMPtr<nsIURI> sourceURI;
  rv = aRequest->item->GetContentSrc(getter_AddRefs(sourceURI));
  NS_ENSURE_SUCCESS(rv, rv);
  nsCString sourceURISpec;
  rv = sourceURI->GetSpec(sourceURISpec);
  NS_ENSURE_SUCCESS(rv, rv);
  rv = aAudioJob->SetSourceURI(NS_ConvertUTF8toUTF16(sourceURISpec));
  NS_ENSURE_SUCCESS(rv, rv);

  nsCOMPtr<sbIPropertyArray> metadata;
  rv = aRequest->item->GetProperties(nsnull, getter_AddRefs(metadata));
  NS_ENSURE_SUCCESS(rv, rv);
  rv = aAudioJob->SetMetadata(metadata);
  NS_ENSURE_SUCCESS(rv, rv);

  /* Just ignore failures from transcoding the album art - the device might
     not support album art, etc, and that's ok.
   */
  if (aRequest->albumArt) {
    nsCOMPtr<nsIInputStream> imageStream;
    rv = aRequest->albumArt->GetTranscodedArt(getter_AddRefs(imageStream));
    if (imageStream && NS_SUCCEEDED(rv)) {
      rv = aAudioJob->SetMetadataImage(imageStream);
      NS_ENSURE_SUCCESS(rv, rv);
    }
  }

  return NS_OK;
}

/**
 * Set the extension on aURI to the appropriate file extension for profile
 * aProfile.
 * \param aProfile the profile we're finding an extension for
 * \param aURI     the URI to set the found extension on
 */
static
nsresult SetFileExtension(sbITranscodeProfile *aProfile,
                          nsIURI *aURI)
{
  NS_ENSURE_ARG_POINTER(aProfile);

  nsCString extension;
  nsresult rv = sbDeviceUtils::GetTranscodedFileExtension(aProfile, extension);
  NS_ENSURE_SUCCESS(rv, rv);

  nsCOMPtr<nsIThread> target;
  rv = NS_GetMainThread(getter_AddRefs(target));
  NS_ENSURE_SUCCESS(rv, rv);

  nsCOMPtr<nsIURL> url;
  rv = do_GetProxyForObject(target,
                            NS_GET_IID(nsIURL),
                            aURI,
                            NS_PROXY_SYNC | NS_PROXY_ALWAYS,
                            getter_AddRefs(url));
  NS_ENSURE_SUCCESS(rv, rv);

  rv = url->SetFileExtension(extension);
  NS_ENSURE_SUCCESS(rv, rv);

  return NS_OK;
}

nsresult
sbDeviceTranscoding::TranscodeMediaItem(
                                     sbBaseDevice::TransferRequest * aRequest,
                                     sbDeviceStatusHelper * aDeviceStatusHelper,
                                     nsIURI * aDestinationURI,
                                     nsIURI ** aTranscodedDestinationURI)
{
  NS_ENSURE_ARG_POINTER(aRequest);
  NS_ENSURE_ARG_POINTER(aDeviceStatusHelper);
  NS_ENSURE_ARG_POINTER(aDestinationURI);

  // Function variables.
  nsresult rv;

  // Create a transcode job.
  nsCOMPtr<nsISupports> tcJob;
  nsCOMPtr<sbITranscodeManager> txMgr;
  rv = GetTranscodeManager(getter_AddRefs(txMgr));
  NS_ENSURE_SUCCESS(rv, rv);
  rv = txMgr->GetTranscoderForMediaItem(aRequest->item,
                                        aRequest->transcodeProfile,
                                        getter_AddRefs(tcJob));
  NS_ENSURE_SUCCESS(rv, rv);

  nsCOMPtr<nsIThread> target;
  rv = NS_GetMainThread(getter_AddRefs(target));
  NS_ENSURE_SUCCESS(rv, rv);

  nsCOMPtr<nsIIOService> ioService =
      do_ProxiedGetService("@mozilla.org/network/io-service;1", &rv);
  NS_ENSURE_SUCCESS(rv, rv);

  nsCOMPtr<nsIURI> transcodedDestinationURI;
  nsCOMPtr<nsIURI> transcodedDestinationURIProxy;
  rv = ioService->NewURI(NS_LITERAL_CSTRING(""),
                         nsnull,
                         aDestinationURI,
                         getter_AddRefs(transcodedDestinationURI));
  NS_ENSURE_SUCCESS(rv, rv);
  rv = do_GetProxyForObject(target,
                            NS_GET_IID(nsIURI),
                            transcodedDestinationURI,
                            NS_PROXY_SYNC | NS_PROXY_ALWAYS,
                            getter_AddRefs(transcodedDestinationURIProxy));
  NS_ENSURE_SUCCESS(rv, rv);
  transcodedDestinationURI = transcodedDestinationURIProxy;

  nsCOMPtr<sbITranscodeVideoJob> videoJob = do_QueryInterface(tcJob, &rv);
  nsCOMPtr<sbITranscodeJob> audioJob;
  if (NS_SUCCEEDED(rv)) {
    nsCOMPtr<sbITranscodeVideoJob> proxyVideoJob;
    rv = do_GetProxyForObject(target,
                              NS_GET_IID(sbITranscodeVideoJob),
                              tcJob,
                              NS_PROXY_SYNC | NS_PROXY_ALWAYS,
                              getter_AddRefs(proxyVideoJob));
    NS_ENSURE_SUCCESS(rv, rv);

    videoJob.swap(proxyVideoJob);

    rv = TranscodeVideoItem(videoJob,
                            aRequest,
                            transcodedDestinationURI);
    NS_ENSURE_SUCCESS(rv, rv);
  }
  else {
    // Set the transcoded file extension.  Not finding an extension is ok, we'll
    // just keep the existing one and hope for the best.
    rv = SetFileExtension(aRequest->transcodeProfile, transcodedDestinationURI);
    if (rv != NS_ERROR_NOT_AVAILABLE) {
      NS_ENSURE_SUCCESS(rv, rv);
    }

    rv = do_GetProxyForObject(target,
                              NS_GET_IID(sbITranscodeJob),
                              tcJob,
                              NS_PROXY_SYNC | NS_PROXY_ALWAYS,
                              getter_AddRefs(audioJob));
    NS_ENSURE_SUCCESS(rv, rv);

    rv = TranscodeAudioItem(audioJob,
                            aRequest,
                            transcodedDestinationURI);
    NS_ENSURE_SUCCESS(rv, rv);
  }

  nsCOMPtr<sbIJobCancelable> cancel = do_QueryInterface(tcJob);

  // Create our listener for transcode progress.
  nsRefPtr<sbTranscodeProgressListener> listener =
    sbTranscodeProgressListener::New(mBaseDevice,
                                     aDeviceStatusHelper,
                                     aRequest,
                                     mBaseDevice->mReqWaitMonitor,
                                     sbTranscodeProgressListener::StatusProperty(),
                                     cancel);
  NS_ENSURE_TRUE(listener, NS_ERROR_OUT_OF_MEMORY);

  // Setup the progress listener.
  nsCOMPtr<sbIJobProgress> progress = do_QueryInterface(tcJob, &rv);
  NS_ENSURE_SUCCESS(rv, rv);
  nsCOMPtr<sbIJobProgress> proxiedProgress;
  rv = do_GetProxyForObject(target,
                            NS_GET_IID(sbIJobProgress),
                            progress,
                            NS_PROXY_SYNC | NS_PROXY_ALWAYS,
                            getter_AddRefs(proxiedProgress));
  NS_ENSURE_SUCCESS(rv, rv);
  rv = proxiedProgress->AddJobProgressListener(listener);
  NS_ENSURE_SUCCESS(rv, rv);

  // Setup the mediacore event listener.
  nsCOMPtr<sbIMediacoreEventTarget> eventTarget = do_QueryInterface(tcJob,
                                                                    &rv);
  NS_ENSURE_SUCCESS(rv, rv);
  rv = eventTarget->AddListener(listener);
  NS_ENSURE_SUCCESS(rv, rv);

  if (videoJob) {
    // This is async.
    rv = videoJob->Transcode();
    NS_ENSURE_SUCCESS(rv, rv);
  }
  else if (audioJob) {
    rv = audioJob->Transcode();
    NS_ENSURE_SUCCESS(rv, rv);
  }
  else {
    NS_WARNING("Transcode job is neither audio or video");
    return NS_ERROR_FAILURE;
  }
  // Wait until the transcode job is complete.
  //XXXeps should check for abort.  To do this, the job will have to be
  //       canceled.
  PRBool isComplete = PR_FALSE;
  while (!isComplete) {
    // Operate within the request wait monitor.
    nsAutoMonitor monitor(mBaseDevice->mReqWaitMonitor);

    // Check if the job is complete.
    isComplete = listener->IsComplete();

    // If not complete, wait for completion.
    if (!isComplete)
      monitor.Wait();
  }

  if (listener->IsAborted())
    return NS_ERROR_ABORT;

  // Check the transcode status.
  PRUint16 status;
  rv = progress->GetStatus(&status);
  NS_ENSURE_SUCCESS(rv, rv);

  // Log any errors.
#ifdef PR_LOGGING
  if (status != sbIJobProgress::STATUS_SUCCEEDED) {
    // Get an enumerator of the error messages.
    nsCOMPtr<nsIStringEnumerator> errorMessageEnum;
    rv = progress->GetErrorMessages(getter_AddRefs(errorMessageEnum));

    // Log each error.
    if (NS_SUCCEEDED(rv)) {
      PRBool hasMore;
      rv = errorMessageEnum->HasMore(&hasMore);
      if (NS_FAILED(rv))
        hasMore = PR_FALSE;
      while (hasMore) {
        // Get the next error message.
        nsAutoString errorMessage;
        rv = errorMessageEnum->GetNext(errorMessage);
        if (NS_FAILED(rv))
          break;

        // Log the error message.
        LOG(("sbMSCDeviceBase::ReqTranscodeWrite error %s\n",
             NS_ConvertUTF16toUTF8(errorMessage).get()));

        // Check for more error messages.
        rv = errorMessageEnum->HasMore(&hasMore);
        if (NS_FAILED(rv))
          hasMore = PR_FALSE;
      }
    }
  }
#endif

  // Get the transcoded video file URI.
  if (videoJob) {
    nsAutoString destURI;
    rv = videoJob->GetDestURI(destURI);
    NS_ENSURE_SUCCESS(rv, rv);
    rv = ioService->NewURI(NS_ConvertUTF16toUTF8(destURI),
                           nsnull,
                           nsnull,
                           getter_AddRefs(transcodedDestinationURI));
    NS_ENSURE_SUCCESS(rv, rv);
    rv = do_GetProxyForObject(target,
                              NS_GET_IID(nsIURI),
                              transcodedDestinationURI,
                              NS_PROXY_SYNC | NS_PROXY_ALWAYS,
                              getter_AddRefs(transcodedDestinationURIProxy));
    NS_ENSURE_SUCCESS(rv, rv);
    transcodedDestinationURI = transcodedDestinationURIProxy;
  }

  if (aTranscodedDestinationURI)
    transcodedDestinationURI.forget(aTranscodedDestinationURI);

  // Check for transcode errors.
  // There is no need to fire a device event, because any appropriate events
  // would have been fired based on the media core event in
  // sbTranscodeProgressListener::OnMediacoreEvent
  NS_ENSURE_TRUE(status == sbIJobProgress::STATUS_SUCCEEDED, NS_ERROR_FAILURE);

  return NS_OK;
}

nsresult sbDeviceTranscoding::GetTranscodeManager(
                                       sbITranscodeManager ** aTranscodeManager)
{
  nsresult rv;
  if (!mTranscodeManager) {
    // Get the transcode manager.
    mTranscodeManager =
      do_ProxiedGetService
        ("@songbirdnest.com/Songbird/Mediacore/TranscodeManager;1", &rv);
    NS_ENSURE_SUCCESS(rv, rv);
  }

  *aTranscodeManager = mTranscodeManager;
  NS_ADDREF(*aTranscodeManager);

  return NS_OK;
}
