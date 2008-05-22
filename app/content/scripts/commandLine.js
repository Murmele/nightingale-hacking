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

/**
 * \file commandLine.js
 * \brief Command Line listener object implementation.
 * \internal
 */

if (typeof(ExternalDropHandler) == "undefined")
  Components.utils.import("resource://app/jsmodules/DropHelper.jsm");

try 
{

  // Module specific global for auto-init/deinit support
  var commandline_module = {};
  commandline_module.init_once = 0;
  commandline_module.deinit_once = 0;

  commandline_module.onLoad = function()
  {
    if (commandline_module.init_once++) { dump("WARNING: commandline_module double init!!\n"); return; }
    commandLineItemHandler.init();
  }

  commandline_module.onUnload = function()
  {
    if (commandline_module.deinit_once++) { dump("WARNING: commandline_module double deinit!!\n"); return; }
    commandLineItemHandler.shutdown();
    window.removeEventListener("load", commandline_module.onLoad, false);
    window.removeEventListener("unload", commandline_module.onUnload, false);
  }

  var commandLineItemHandler = {
    cmdline_mgr: null,
    
    init: function() {
      var cmdline = Components.classes["@songbirdnest.com/commandlinehandler/general-startup;1?type=songbird"];
      if (cmdline) {
        var cmdline_service = cmdline.getService(Components.interfaces.nsICommandLineHandler);
        if (cmdline_service) {
          cmdline_mgr = cmdline_service.QueryInterface(Components.interfaces.sbICommandLineManager);
          if (cmdline_mgr)
            cmdline_mgr.addItemHandler(this);
        }
      }
    },
    
    shutdown: function() {
      if (cmdline_mgr) cmdline_mgr.removeItemHandler(this);
    },
    
    handleItem: function(aUriSpec, aCount, aTotal) {
      if (aUriSpec.toLowerCase().indexOf("http:") == 0 ||
          aUriSpec.toLowerCase().indexOf("https:") == 0) {
        gBrowser.loadURI(aUriSpec);
      } else {
        if (gPPS.isPlaylistURL(aUriSpec)) {
          var list = SBOpenPlaylistURI(aUriSpec);
          if (list) {
            var view = LibraryUtils.createStandardMediaListView(list);
            gPPS.playView(view, 0);
          }
        } else {
          var dropHandlerListener = {
            onDropComplete: function(aTargetList,
                                     aImportedInLibrary,
                                     aDuplicates,
                                     aInsertedInMediaList,
                                     aOtherDropsHandled) {
              // show the standard report on the status bar
              return true;
            },
            onFirstMediaItem: function(aTargetList, aFirstMediaItem) {
              var view = LibraryUtils.createStandardMediaListView(LibraryUtils.mainLibrary);

              var index = view.getIndexForItem(aFirstMediaItem);
              
              // If we have a browser, try to show the view
              if (window.gBrowser) {
                gBrowser.showIndexInView(view, index);
              }
              
              // Play the item
              gPPS.playView(view, index);
            },
          };
          ExternalDropHandler.dropUrls(window, [aUriSpec], dropHandlerListener);
        }
      }
      return true;
    },
    
    QueryInterface : function(aIID) {
      if (!aIID.equals(Components.interfaces.sbICommandLineItemHandler) &&
          !aIID.equals(Components.interfaces.nsISupports)) 
      {
        throw Components.results.NS_ERROR_NO_INTERFACE;
      }
      return this;
    }
  };

  // Auto-init/deinit registration
  window.addEventListener("load", commandline_module.onLoad, false);
  window.addEventListener("unload", commandline_module.onUnload, false);
}
catch(e)
{
  dump("commandLine.js - " + e + "\n");
}


