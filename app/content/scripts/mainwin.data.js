/*
//
// BEGIN SONGBIRD GPL
// 
// This file is part of the Songbird web player.
//
// Copyright� 2006 Pioneers of the Inevitable LLC
// http://songbirdnest.com
// 
// This file may be licensed under the terms of of the
// GNU General Public License Version 2 (the �GPL�).
// 
// Software distributed under the License is distributed 
// on an �AS IS� basis, WITHOUT WARRANTY OF ANY KIND, either 
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
//  This file binds UI objects from the mainwin.xul (currently rmp_demo.xul)
//  to data remotes that will automatically update them to reflect the proper value.
//

try
{
  function onSBMainwinDataLoad()
  {
    try
    {
      // Do the magic binding stuff here.
      //
      // SBDataBindElementProperty Param List
      //  1 - The data ID to bind upon
      //  2 - The element ID to be bound
      //  3 - The name of the property or attribute to change
      //  4 - Optionally assign the data as BOOLEAN data (true/false props, "true"/"false" attrs)
      //  5 - Optionally assign the data as a boolean NOT of the value
      //  6 - Optionally apply an eval string where `value = eval( "eval_string" );`

      // Playlist Items
      MainwinAdd( SBDataBindElementProperty ( "playlist.numitems", "library_text", "value" ) );
      MainwinAdd( SBDataBindElementAttribute( "playlist.shuffle", "control.shuf", "checked", true ) );
      
      // Metadata Items    
      MainwinAdd( SBDataBindElementProperty ( "metadata.title",  "songbird_text_title", "value" ) );
      MainwinAdd( SBDataBindElementProperty ( "metadata.artist", "songbird_text_artist", "value" ) );
      MainwinAdd( SBDataBindElementProperty ( "metadata.album", "songbird_text_album", "value" ) );
      
      MainwinAdd( SBDataBindElementAttribute( "metadata.position.str", "songbird_text_time_elapsed", "value" ) );

      // Faceplate Items    
      MainwinAdd( SBDataBindElementAttribute( "faceplate.state", "intro_box", "hidden", true ) );
      MainwinAdd( SBDataBindElementAttribute( "faceplate.state", "dashboard_box", "hidden", true, true ) );

      MainwinAdd( SBDataBindElementProperty ( "faceplate.status.text", "status_text", "value" ) );
      MainwinAdd( SBDataBindElementAttribute( "faceplate.status.style", "status_text", "style" ) );
      
      MainwinAdd( SBDataBindElementProperty ( "faceplate.search.reset", "search_widget", "reset" ) );
      
      MainwinAdd( SBDataBindElementProperty ( "faceplate.loading", "status_progress", "mode", false, false, "if ( value == '1' ) value = 'undetermined'; else value = ''; value;" ) );
      
      // Browser Items
      MainwinAdd( SBDataBindElementAttribute( "browser.cangoback", "browser_back", "disabled", true, true ) );
      MainwinAdd( SBDataBindElementAttribute( "browser.cangofwd", "browser_fwd", "disabled", true, true ) );
      MainwinAdd( SBDataBindElementAttribute( "faceplate.loading", "browser_stop", "disabled", true, true ) );
      //MainwinAdd( SBDataBindElementAttribute( "browser.canplaylist", "browser_playlist", "disabled", true, true ) );
      //MainwinAdd( SBDataBindElementAttribute( "browser.hasdownload", "browser_download", "disabled", true, true ) );
      MainwinAdd( SBDataBindElementProperty ( "browser.url.text", "browser_url", "value" ) );
      MainwinAdd( SBDataBindElementProperty ( "browser.url.image", "browser_url_image", "src" ) );
      MainwinAdd( SBDataBindElementProperty ( "browser.playlist.show", "playlist_web", "hidden", true, true ) );
      MainwinAdd( SBDataBindElementProperty ( "browser.playlist.show", "playlist_web_split", "hidden", true, true ) );
      
      // Backscan Item
      MainwinAdd( SBDataBindElementProperty ( "backscan.status", "scanning_text", "value" ) );
      MainwinAdd( SBDataBindElementAttribute( "backscan.status", "scanning_text", "hidden", true, false, "value == ''" ) );

      // Options
//      MainwinAdd( SBDataBindElementAttribute( "option.htmlbar", "file.htmlbar", "checked", true ) );
      MainwinAdd( SBDataBindElementAttribute( "option.skin", "file.skin", "checked", true ) );

      // Complex Data Items
      onSBMainwinComplexLoad();
      
      // Events
      document.addEventListener( "search", onSBMainwinSearchEvent, true ); // this didn't work when we put it directly on the widget?
      
//      SBInitPlayerLoop();
    }
    catch ( err )
    {
      alert( err );
    }
  }
      
  // The bind function returns a remote, 
  // we keep it here so there's always a ref
  // and we stay bound till we unload.
  var MainwinDataRemotes = new Array();
  function MainwinAdd( remote )
  {
    if ( remote )
    {
      MainwinDataRemotes.push( remote );
    }
  }

  function onSBMainwinDataUnload()
  {
    try
    {
      // Unbind the observers!
      for ( var i = 0; i < MainwinDataRemotes.length; i++ )
      {
        MainwinDataRemotes[ i ].unbind();
      }
    }  
    catch ( err )
    {
      alert( err );
    }
  }
  
  //
  // Complex Implementations (can't be handled by a simple eval)
  //
  
  var MainwinArtistRemote = null;
  var MainwinAlbumRemote = null;
  function onSBArtistAlbumChanged( value )
  {
    // (we get called before they're fully bound)
    if ( MainwinArtistRemote && MainwinAlbumRemote )
    {
      var artist = MainwinArtistRemote.getValue();
      var album = MainwinAlbumRemote.getValue();
      var theAASlash = document.getElementById( "songbird_text_slash" );
      var theAABox = document.getElementById( "songbird_text_artistalbum" );
      if ( album.length || artist.length )
      {
        if ( album.length && artist.length )
        {
          theAASlash.setAttribute( "hidden", "false" );    
        }
        else
        {
          theAASlash.setAttribute( "hidden", "true" );    
        }
        theAABox.setAttribute( "hidden", "false" );
      }
      else
      {
        theAASlash.setAttribute( "hidden", "true" );
        // theAABox.setAttribute( "hidden", "true" ); // use this to center the seekbar & title
        theAABox.setAttribute( "hidden", "false" ); // use this to keep the seekbar in the same place
      }
    }
  }

  function onSBMainwinComplexLoad()
  {
    try
    {
      // Title/<slash>/Album Box Complex -- two data items for one callback.
      MainwinArtistRemote = new sbIDataRemote( "metadata.title" ); // changed to title cuz we like to be odd.
      MainwinArtistRemote.bindCallbackFunction( onSBArtistAlbumChanged );
      MainwinAdd( MainwinArtistRemote );
      MainwinAlbumRemote = new sbIDataRemote( "metadata.album" );
      MainwinAlbumRemote.bindCallbackFunction( onSBArtistAlbumChanged );
      MainwinAdd( MainwinAlbumRemote );
      
    }
    catch ( err )
    {
      alert( err );
    }
  }
/*  
*/  
  var theLastSearchEventTarget = null;
  var theSearchEventInterval = null;
  function onSBMainwinSearchEvent( evt )
  {
    if ( theSearchEventInterval )
    {
      clearInterval( theSearchEventInterval );
      theSearchEventInterval = null;
    }
    var widget = null;
    if ( theLastSearchEventTarget )
    {
      widget = theLastSearchEventTarget;
    }
    else if ( evt )
    {
      widget = evt.target;
    }
    else
    {
      // ???
    }
    if ( widget )
    {
      if ( widget.is_songbird )
      {
        if ( !thePlaylistTree && !theLastSearchEventTarget )
        {
          LaunchMainPaneURL( "chrome://songbird/content/xul/main_pane.xul?library" );
        }
        
        if ( thePlaylistRef.getValue().length > 0 )
        {
          // Feed the new filter into the list.
          var source = new sbIPlaylistsource();
          // Wait until it is done executing
          if ( ! source.IsQueryExecuting( thePlaylistRef.getValue() ) )
          {
            // ...before attempting to override.
            source.FeedPlaylistFilterOverride( thePlaylistRef.getValue(), widget.list.label );
            theLastSearchEventTarget = null;
            return;
          }
        }
        
        // If we get here, we need to wait for the tree to appear
        // and finish its query.
        theLastSearchEventTarget = widget;
        theSearchEventInterval = setInterval( onSBMainwinSearchEvent, 1500 );
      }
      else
      {
        onSearchTerm( evt.target.cur_service, evt.target.list.value );
      }
    }
  }
  
  // END
}
catch ( err )
{
  alert( err );
}