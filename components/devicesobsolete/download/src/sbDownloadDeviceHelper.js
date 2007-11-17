/*
//
// BEGIN SONGBIRD GPL
//
// This file is part of the Songbird web player.
//
// Copyright(c) 2005-2007 POTI, Inc.
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

Components.utils.import("resource://gre/modules/XPCOMUtils.jsm");
Components.utils.import("resource://app/components/sbProperties.jsm");
Components.utils.import("resource://app/components/ArrayConverter.jsm");

const Ci = Components.interfaces;
const Cc = Components.classes;
const Cr = Components.results;

const DOWNLOAD_PREF = "songbird.library.download";

function sbDownloadDeviceHelper()
{
  this._mainLibrary = Cc["@songbirdnest.com/Songbird/library/Manager;1"]
                        .getService(Ci.sbILibraryManager).mainLibrary;
}

sbDownloadDeviceHelper.prototype =
{
  classDescription: "Songbird Download Device Helper",
  classID:          Components.ID("{576b6833-15d8-483a-84d6-2fbd329c82e1}"),
  contractID:       "@songbirdnest.com/Songbird/DownloadDeviceHelper;1",
  QueryInterface:   XPCOMUtils.generateQI([Ci.sbIDownloadDeviceHelper])
}

sbDownloadDeviceHelper.prototype.downloadItem =
function sbDownloadDeviceHelper_downloadItem(aMediaItem)
{
  if (!this._ensureDestination()) {
    return;
  }
  if (!this.downloadMediaList) {
    throw Cr.NS_ERROR_FAILURE;
  }

  let uriArray           = Cc["@mozilla.org/array;1"]
                             .createInstance(Ci.nsIMutableArray);
  let propertyArrayArray = Cc["@mozilla.org/array;1"]
                             .createInstance(Ci.nsIMutableArray);

  let item;
  if (aMediaItem.library.equals(this._mainLibrary)) {
    item = aMediaItem;
  }
  else {
    this._addItemToArrays(aMediaItem, uriArray, propertyArrayArray);
    let items = this._mainLibrary.batchCreateMediaItems(uriArray,
                                                        propertyArrayArray,
                                                        true);
    item = items.queryElementAt(0, Ci.sbIMediaItem);
  }

  this.downloadMediaList.add(item);
}

sbDownloadDeviceHelper.prototype.downloadSome =
function sbDownloadDeviceHelper_downloadSome(aMediaItems)
{
  if (!this._ensureDestination()) {
    return;
  }
  if (!this.downloadMediaList) {
    throw Cr.NS_ERROR_FAILURE;
  }

  let uriArray           = Cc["@mozilla.org/array;1"]
                             .createInstance(Ci.nsIMutableArray);
  let propertyArrayArray = Cc["@mozilla.org/array;1"]
                             .createInstance(Ci.nsIMutableArray);

  var items = [];
  while (aMediaItems.hasMoreElements()) {
    let item = aMediaItems.getNext();
    if (item.library.equals(this._mainLibrary)) {
      items.push(item);
    }
    else {
      this._addItemToArrays(item, uriArray, propertyArrayArray);
    }
  }

  if (uriArray.length > 0) {
    let addedItems = this._mainLibrary.batchCreateMediaItems(uriArray,
                                                             propertyArrayArray,
                                                             true);
    for (let i = 0; i < addedItems.length; i++) {
      items.push(addedItems.queryElementAt(i, Ci.sbIMediaItem));
    }
  }

  this.downloadMediaList.addSome(ArrayConverter.enumerator(items));
}

sbDownloadDeviceHelper.prototype.downloadAll =
function sbDownloadDeviceHelper_downloadAll(aMediaList)
{
  if (!this._ensureDestination()) {
    return;
  }
  if (!this.downloadMediaList) {
    throw Cr.NS_ERROR_FAILURE;
  }

  let uriArray           = Cc["@mozilla.org/array;1"]
                             .createInstance(Ci.nsIMutableArray);
  let propertyArrayArray = Cc["@mozilla.org/array;1"]
                             .createInstance(Ci.nsIMutableArray);

  var items = [];
  var isForeign = aMediaList.library.equals(this._mainLibrary);
  for (let i = 0; i < aMediaList.length; i++) {
    var item = aMediaList.getItemByIndex(i);
    if (isForeign) {
      this._addItemToArrays(item, uriArray, propertyArrayArray);
    }
    else {
      items.push(item);
    }
  }

  if (uriArray.length > 0) {
    let addedItems = this._mainLibrary.batchCreateMediaItems(uriArray,
                                                             propertyArrayArray,
                                                             true);
    for (let i = 0; i < addedItems.length; i++) {
      items.push(addedItems.queryElementAt(i, Ci.sbIMediaItem));
    }
  }

  this.downloadMediaList.addSome(ArrayConverter.enumerator(items));
}

sbDownloadDeviceHelper.prototype.__defineGetter__("downloadMediaList",
function sbDownloadDeviceHelper_getDownloadMediaList()
{
  if (!this._downloadDevice) {
    var devMgr = Cc["@songbirdnest.com/Songbird/DeviceManager;1"]
                   .getService(Ci.sbIDeviceManager);
    if (devMgr) {
      var downloadCat = "Songbird Download Device";
      if (devMgr.hasDeviceForCategory(downloadCat)) {
        this._downloadDevice = devMgr.getDeviceByCategory(downloadCat)
                                     .QueryInterface(Ci.sbIDownloadDevice);
      }
    }
  }
  return this._downloadDevice ? this._downloadDevice.downloadMediaList : null ;
});

sbDownloadDeviceHelper.prototype._addItemToArrays =
function sbDownloadDeviceHelper__addItemToArrays(aMediaItem,
                                                 aURIArray,
                                                 aPropertyArrayArray)
{
  aURIArray.appendElement(aMediaItem.contentSrc, false);

  var dest   = SBProperties.createArray();
  var source = aMediaItem.getProperties();
  for (let i = 0; i < source.length; i++) {
    var prop = source.getPropertyAt(i);
    if (prop.id != SBProperties.contentSrc) {
      dest.appendElement(prop, false);
    }
  }

  var target = aMediaItem.library.guid + "," + aMediaItem.guid;
  dest.appendProperty(SBProperties.downloadStatusTarget, target);

  aPropertyArrayArray.appendElement(dest, false);
}

sbDownloadDeviceHelper.prototype._ensureDestination =
function sbDownloadDeviceHelper__ensureDestination()
{
  var prefs = Cc["@mozilla.org/preferences-service;1"]
                  .getService(Ci.nsIPrefBranch2);
  try {
    var always = prefs.getCharPref("songbird.download.always") == "1";
    if (always) {
      return true;
    }
  }
  catch(e) {
    // Don't care if it fails
  }

  var chromeFeatures = "modal,chrome,centerscreen,dialog=yes,resizable=no";
  var useTitlebar = false;
  try {
    useTitlebar = prefs.getCharPref("songbird.accessibility.enabled") == "1";
  }
  catch(e) {
    // Don't care if it fails
  }

  // bonus stuff to shut the mac up.
  var chromeFeatures = ",modal=yes,resizable=no";
  if (useTitlebar) chromeFeatures += ",titlebar=yes";
  else chromeFeatures += ",titlebar=no";

  var wm = Components.classes["@mozilla.org/appshell/window-mediator;1"]
                      .getService(Components.interfaces.nsIWindowMediator);
  var mainWin = wm.getMostRecentWindow("Songbird:Main");

  var paramBlock = Cc["@mozilla.org/embedcomp/dialogparam;1"]
                     .createInstance(Ci.nsIDialogParamBlock);

  var watcher = Cc["@mozilla.org/embedcomp/window-watcher;1"]
                  .getService(Ci.nsIWindowWatcher);
  watcher.openWindow(mainWin,
                     "chrome://songbird/content/xul/download.xul",
                     "_blank",
                     chromeFeatures,
                     paramBlock);

  return paramBlock.GetInt(0) == 1;
}

function NSGetModule(compMgr, fileSpec) {
  return XPCOMUtils.generateModule([sbDownloadDeviceHelper]);
}
