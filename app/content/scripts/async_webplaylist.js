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

//
// Web Playlist loader using sbIAsyncForLoop
//

var WEB_PLAYLIST_CONTEXT      = "webplaylist";
var WEB_PLAYLIST_TABLE        = "webplaylist";
var WEB_PLAYLIST_TABLE_NAME   = "&device.webplaylist";
var WEB_PLAYLIST_LIBRARY_NAME = "&device.weblibrary";

try
{
  // Parse through the document to get all the urls.
  var href_loop = null;
  function CancelAsyncWebDocument()
  {
    if ( href_loop )
    {
      href_loop.cancel();
      href_loop = null;
    }
  }
  function AsyncWebDocument( theDocument, aGuid, aTable )
  {
    if (!aGuid) aGuid = WEB_PLAYLIST_CONTEXT; // null protection
    if (!aTable) aTable = WEB_PLAYLIST_TABLE; // null protection
    CancelAsyncWebDocument();
    const PlaylistManager = new Components.Constructor("@songbirdnest.com/Songbird/PlaylistManager;1", "sbIPlaylistManager");
    const MediaLibrary = new Components.Constructor("@songbirdnest.com/Songbird/MediaLibrary;1", "sbIMediaLibrary");
    href_loop = new sbIAsyncForLoop
    ( // this is an arg list:
      // aInitEval
      function()
      {
        this.installed_listener = false;
        this.i = 0;
      },
      // aWhileEval
      function()
      {
        return ( this.i < this.a_array.length ) || ( this.i < this.embed_array.length ) || ( this.i < this.object_array.length );
      },
      // aStepEval
      function()
      {
        this.i++;
      },
      // aBodyEval
      function()
      {
       try 
       {
        // Do not run while the "main" playlist is up?  This is so gross.
        //  UGH.  MUST REWRITE ENTIRE WORLD.
        if ( thePlaylistTree ) {
          this.cancel(); 
          onBrowserPlaylistHide();
        } 

        // check is clearInterval has been called (see sbIAsyncForLoop.js:66)
        if ( !this.m_Interval )
          return true;

        var loop_break = false;
        // "A" tags
        if ( 
            ( this.i < this.a_array.length ) &&
            ( this.a_array[ this.i ].href ) &&
            ( this.a_array[ this.i ].addEventListener )
           )
        {
          var url = this.a_array[ this.i ].href;
          if ( url )
            loop_break = this.HandleUrl( url );
          // Add our event listeners to anything that's a link.
          this.a_array[ this.i ].addEventListener( "contextmenu", onLinkContext, true );
          this.a_array[ this.i ].addEventListener( "mouseover", onLinkOver, true );
          this.a_array[ this.i ].addEventListener( "mouseout", onLinkOut, true );
        }
        // "Embed" tags
        if ( 
            ( this.i < this.embed_array.length )
           )
        {
          var url = this.embed_array[ this.i ].getAttribute("src");
          if ( url )
            loop_break |= this.HandleUrl( url );
        }
        // "Object" tags
        if ( 
            ( this.i < this.object_array.length )
           )
        {
          // ?
          var url = this.object_array[ this.i ].getAttribute("src");
          if ( url )
            loop_break |= this.HandleUrl( url );
        }
        
        return loop_break;
       }
       catch ( err )        
       {
        alert( "async_webplaylist - aBodyEval\n\n" + err );
       }
      return 0;
      },

      // aFinishedEval
      function()
      {
        if ( !this.m_Interval ) 
          return;
        SBDataSetBoolValue( "media_scan.open", false ); // ?  Don't let this go?
        SBDataSetIntValue( "webplaylist.total", this.a_array.length );
        SBDataSetIntValue( "webplaylist.current", this.a_array.length );
        
        // Release the global reference
        href_loop = null;
      },
      20, // 20 steps per interval
      0 // No pause per interval (each UI frame)
    );
    // End of class construction for sbIAsyncPlaylist
    
    // Attach a whole bunch of stuff to it so it can do its job in one pass.
    href_loop.guid = aGuid;
    href_loop.table = aTable;
    href_loop.doc = theDocument;
    href_loop.data = theCanAddToPlaylistData;
    href_loop.a_array = theDocument.getElementsByTagName('A');
    href_loop.embed_array = theDocument.getElementsByTagName('EMBED');
    href_loop.object_array = theDocument.getElementsByTagName('OBJECT');
    href_loop.uri_now = SBDataGetStringValue( "browser.uri" );
    href_loop.aDBQuery = new sbIDatabaseQuery();
    href_loop.aMediaLibrary = (new MediaLibrary()).QueryInterface(Components.interfaces.sbIMediaLibrary);    
    href_loop.aDBQuery.setAsyncQuery( false );
    href_loop.aDBQuery.setDatabaseGUID( aGuid );
    href_loop.aMediaLibrary.setQueryObject( href_loop.aDBQuery );
    href_loop.aMediaLibrary.createDefaultLibrary(); // Does waitForCompletion();
    href_loop.aPlaylistManager = (new PlaylistManager()).QueryInterface(Components.interfaces.sbIPlaylistManager);
    href_loop.aPlaylistManager.createDefaultPlaylistManager( href_loop.aDBQuery );
    href_loop.inserted = new Array();
    href_loop.aPlaylist = null;
    href_loop.HandleUrl = // Useful function to be called by the internal code
    function( url )     
    {
      var loop_break = false;
      var is_media = gPPS.isMediaURL( url );
      var is_playlist = gPPS.isPlaylistURL( url );
      if ( is_playlist )
      {
        this.a_array[ this.i ].addEventListener( "click", onMediaClick, true );
      }
      else if ( is_media )
      {
        SBDataSetIntValue( "webplaylist.current", this.i + 1 );
        this.a_array[ this.i ].addEventListener( "click", onMediaClick, true );
        this.installed_listener = true;
        
        // Don't insert it if we already did.
        var skip = false;
        for ( var j in this.inserted )
        {
          if ( this.inserted[ j ] == url )
          {
            skip = true;
            break;
          }
        }
        
        if ( ! skip )
        {
          if ( this.aPlaylist == null )
          {
            if ( this.guid == WEB_PLAYLIST_CONTEXT )
            {
              // When we first find media, flip the webplaylist. 
              this.aPlaylistManager.deletePlaylist( WEB_PLAYLIST_TABLE, this.aDBQuery );
              this.aPlaylist = this.aPlaylistManager.createPlaylist( WEB_PLAYLIST_TABLE + "_library", WEB_PLAYLIST_LIBRARY_NAME, "library", "library", this.aDBQuery );
              this.aPlaylist = this.aPlaylistManager.createPlaylist( WEB_PLAYLIST_TABLE, WEB_PLAYLIST_TABLE_NAME, WEB_PLAYLIST_TABLE, this.uri_now, this.aDBQuery );
              this.data.boolValue = true;
              theWebPlaylistQuery = this.aDBQuery;
              // Then pretend like we clicked on it.
              if ( !thePlaylistTree )
                onBrowserPlaylist();
            }
            else
            {
              this.aPlaylist = this.aPlaylistManager.getPlaylist( this.table, this.aDBQuery );
            }
          }
          
          if ( this.aPlaylist == null )
          {
            return; // crap!
          }
                    
          var keys = new Array( "title" );
          var values = new Array( gPPS.convertURLToDisplayName( url ) );
          var guid = this.aMediaLibrary.addMedia( url, keys.length, keys, values.length, values, true, false );
          if ( this.aPlaylist.findByGUID( guid ) == -1 )
            this.aPlaylist.addByGUID( guid, this.guid, -1, true, false );
          //dump("XXredfive - just AddedByGUID:" + guid + " this.aDBQuery: " + this.aDBQuery + "\n");
          this.inserted.push( url );
          
          //A***
          
          //this.aDBQuery.resetQuery();
          loop_break = true; // Only one synchronous database call per ui frame.
        }
      }
      else
      {
        // decrement the total (floor is 0) to keep the percentage indicator moving
        SBDataDecrementValue( "webplaylist.total", 0 );
      }
    
      return loop_break;
    }
    SBDataSetIntValue( "webplaylist.total", href_loop.a_array.length );
    SBDataSetBoolValue( "media_scan.open", true ); // ?  Don't let this go?
  }
}
catch ( err )
{
  alert( "sbIAsyncForLoop - load\r\n" + err );
}


//Removed this block from A***.
          /*var count = this.aDBQuery.getQueryCount();
          var text = "\n\n";
          for ( c = 0; c < count; c++ )
          {
            text += this.aDBQuery.getQuery( c ) + "\n";
          }*/
          
//          alert( "pause" );
          
          /*
          var rv = this.aDBQuery.execute();
          
          if ( rv != 0 )
          {
            alert( "FAILED: " + gPPS.convertURLToDisplayName( url ) + "  ----  Result value: " + rv + text );
          }
          */
          
          //this.aDBQuery.resetQuery();
