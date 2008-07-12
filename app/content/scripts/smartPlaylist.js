
var sbILocalDatabaseSmartMediaList =
  Components.interfaces.sbILocalDatabaseSmartMediaList;

var SB_NS = "http://songbirdnest.com/data/1.0#";
var SB_PROPERTY_UILIMITTYPE = SB_NS + "uiLimitType";

var USECS_PER_MINUTE = 60 * 1000 * 1000;
var USECS_PER_HOUR   = USECS_PER_MINUTE * 60;
var BYTES_PER_MB     = 1000 * 1000;
var BYTES_PER_GB     = BYTES_PER_MB * 1000;

var selectByList = [
  {
    value: "album",
    property: SB_NS + "albumName",
    direction: true
  },
  {
    value: "artist",
    property: SB_NS + "artistName",
    direction: true
  },
  {
    value: "genre",
    property: SB_NS + "genre",
    direction: true
  },
  {
    value: "title",
    property: SB_NS + "trackName",
    direction: true
  },
  {
    value: "high_rating",
    property: SB_NS + "rating",
    direction: false
  },
  {
    value: "low_rating",
    property: SB_NS + "rating",
    direction: true
  },
  {
    value: "most_recent",
    property: SB_NS + "lastPlayTime",
    direction: false
  },
  {
    value: "least_recent",
    property: SB_NS + "lastPlayTime",
    direction: true
  },
  {
    value: "most_often",
    property: SB_NS + "playCount",
    direction: false
  },
  {
    value: "least_often",
    property: SB_NS + "playCount",
    direction: true
  },
  {
    value: "most_added",
    property: SB_NS + "created",
    direction: false
  },
  {
    value: "least_added",
    property: SB_NS + "created",
    direction: true
  }
];

function updateOkButton() {
  var smartConditions = document.getElementById("smart_conditions");
  var dialog = document.documentElement;
  var ok = dialog.getButton("accept");
  ok.disabled = !smartConditions.isValid;
}

function updateMatchControls() {
  var check = document.getElementById("smart_match_check");
  var checkwhat = document.getElementById("smart_any_list");
  var following = document.getElementById("smart_following_label");
  
  var smartConditions = document.getElementById("smart_conditions");
  var conditions = smartConditions.conditions;
  
  if (conditions.length == 1) {
    check.setAttribute("label", check.getAttribute("labelsingle"));
    checkwhat.hidden = true;
    following.hidden = true;
  } else {
    check.setAttribute("label", check.getAttribute("labelmultiple"));
    checkwhat.hidden = false;
    following.hidden = false;
  }
}

function onAddCondition() {
  updateMatchControls();
  updateOkButton();
}

function onRemoveCondition() {
  updateMatchControls();
  updateOkButton();
}

function onUserInput() {
  updateOkButton();
}

function doLoad()
{
  setTimeout(loadConditions);

  var smartConditions = document.getElementById("smart_conditions");
  smartConditions.addEventListener("input",  onUserInput, false);
  smartConditions.addEventListener("select", onUserInput, false);
  smartConditions.addEventListener("additem", onAddCondition, false);
  smartConditions.addEventListener("removeitem", onRemoveCondition, false);
}

function doUnLoad()
{
  var smartConditions = document.getElementById("smart_conditions");
  smartConditions.removeEventListener("input",  onUserInput, false);
  smartConditions.removeEventListener("select", onUserInput, false);
  smartConditions.removeEventListener("additem", onAddCondition, false);
  smartConditions.removeEventListener("removeitem", onRemoveCondition, false);
}

function loadConditions()
{
  var list = window.arguments[0];
  
  if (list instanceof sbILocalDatabaseSmartMediaList) {

    // Set up conditions
    var smartConditions = document.getElementById("smart_conditions");
    if (list.conditionCount > 0) {
      var conditions = [];
      for (var i = 0; i < list.conditionCount; i++) {
        var condition = list.getConditionAt(i);
        conditions.push({
          metadata: condition.propertyID,
          condition: condition.operator.operator,
          value: condition.leftValue,
          value2: condition.rightValue,
          unit: condition.displayUnit
        });
      }
      smartConditions.conditions = conditions;
    }
    else {
      smartConditions.newCondition();
    }

    // Set match type
    var matchSomething = document.getElementById("smart_match_check");
    var matchAnyAll = document.getElementById("smart_any_list");
    switch(list.matchType) {
      case sbILocalDatabaseSmartMediaList.MATCH_TYPE_ANY:
        matchAnyAll.value = "any";
        matchSomething.checked = true;
      break;
      case sbILocalDatabaseSmartMediaList.MATCH_TYPE_ALL:
        matchAnyAll.value = "all";
        matchSomething.checked = true;
      break;
      case sbILocalDatabaseSmartMediaList.MATCH_TYPE_NONE:
        matchAnyAll.value = "any";
        matchSomething.checked = false;
      break;
    }

    // Set limit.  Get the "ui" limit from a list property
    var uiLimitType = list.getProperty(SB_PROPERTY_UILIMITTYPE) || "songs";

    // Set the limit based on the ui limit.  Convert the units from the smart
    // playlist to the units needed to display the ui limit

    var limit = document.getElementById("smart_songs_check");
    var count = document.getElementById("smart_songs_count");
    var limitType = document.getElementById("smart_songs_list");
    if (list.limitType == sbILocalDatabaseSmartMediaList.LIMIT_TYPE_NONE) {
      limit.checked = false;
      count.value = "0";
      limitType.value = "songs";
    }
    else {
      var mismatch = false;
      switch(uiLimitType) {
        case "songs":
          if (list.limitType != sbILocalDatabaseSmartMediaList.LIMIT_TYPE_ITEMS) {
            mismatch = true;
            break;
          }
          count.value = list.limit;
        break;
        case "minutes":
          if (list.limitType != sbILocalDatabaseSmartMediaList.LIMIT_TYPE_USECS) {
            mismatch = true;
            break;
          }
          count.value = list.limit / USECS_PER_MINUTE;
        break;
        case "hours":
          if (list.limitType != sbILocalDatabaseSmartMediaList.LIMIT_TYPE_USECS) {
            mismatch = true;
            break;
          }
          count.value = list.limit / USECS_PER_HOUR;
        break;
        case "MB":
          if (list.limitType != sbILocalDatabaseSmartMediaList.LIMIT_TYPE_BYTES) {
            mismatch = true;
            break;
          }
          count.value = list.limit / BYTES_PER_MB;
        break;
        case "GB":
          if (list.limitType != sbILocalDatabaseSmartMediaList.LIMIT_TYPE_BYTES) {
            mismatch = true;
            break;
          }
          count.value = list.limit / BYTES_PER_GB;
        break;
      }
      if (mismatch) {
        limit.checked = false;
        count.value = "0";
        limitType.value = "songs";
      }
      else {
        limit.checked = true;
        limitType.value = uiLimitType;
      }
    }

    // Set select by
    var selectBy = document.getElementById("smart_selected_list");
    if (list.randomSelection) {
      selectBy.value = "random";
    }
    else {
      var value = getValueForSelectBy(list);
      selectBy.value = value;
    }

  } else { // if (list instanceof smart)
    var smartConditions = document.getElementById("smart_conditions");

    smartConditions.newCondition();

    var matchSomething = document.getElementById("smart_match_check");
    var matchAnyAll = document.getElementById("smart_any_list");

    matchAnyAll.value = "all";
    matchSomething.checked = true;

    var limit = document.getElementById("smart_songs_check");
    var count = document.getElementById("smart_songs_count");
    var limitType = document.getElementById("smart_songs_list");

    limit.checked = false;
    count.value = "25";
    limitType.value = "songs";

    var selectBy = document.getElementById("smart_selected_list");
    selectBy.value = "random";
  }

  // immediately update the match controls, so we don't have to wait for the drawer items
  updateMatchControls();
  // immediately update ok button, in case one of the loaded setting was actually invalid
  updateOkButton();
}

function doOK()
{
  var smart_conditions = document.getElementById("smart_conditions");
  if (smart_conditions.isValid) {
    var list = window.arguments[0];
    
    if (!(list instanceof sbILocalDatabaseSmartMediaList)) {
      var paramObject = list;
      list = paramObject.newPlaylistFunction();
      paramObject.newSmartPlaylist = list;
    }
    var pm = Components.classes["@songbirdnest.com/Songbird/Properties/PropertyManager;1"]
                               .getService(Components.interfaces.sbIPropertyManager);

    // Save conditions
    var conditions = smart_conditions.conditions;
    list.clearConditions();
    conditions.forEach(function(condition) {
      var info = pm.getPropertyInfo(condition.metadata);
      var op = info.getOperator(condition.condition);
      var unit;
      var leftValue;
      var rightValue;
      if (op.operator != info.OPERATOR_ISTRUE &&
          op.operator != info.OPERATOR_ISFALSE)
        leftValue = condition.value;
      if (op.operator == info.OPERATOR_BETWEEN)
        rightValue = condition.value2;
      if (condition.useunits)
        unit = condition.unit;
      list.appendCondition(condition.metadata,
                           op,
                           leftValue,
                           rightValue,
                           unit);
    });

    // Save match
    var matchSomething = document.getElementById("smart_match_check");
    var matchAnyAll = document.getElementById("smart_any_list");
    if (matchSomething.checked) {
      if (matchAnyAll.value == "all") {
        list.matchType = sbILocalDatabaseSmartMediaList.MATCH_TYPE_ALL;
      }
      else {
        list.matchType = sbILocalDatabaseSmartMediaList.MATCH_TYPE_ANY;
      }
    }
    else {
     list.matchType = sbILocalDatabaseSmartMediaList.MATCH_TYPE_NONE;
    }

    // Save limit
    var limit = document.getElementById("smart_songs_check");
    var count = document.getElementById("smart_songs_count");
    var limitType = document.getElementById("smart_songs_list");

    list.setProperty(SB_PROPERTY_UILIMITTYPE, limitType.value);

    if (limit.checked) {
      switch(limitType.value) {
        case "songs":
          list.limitType = sbILocalDatabaseSmartMediaList.LIMIT_TYPE_ITEMS;
          list.limit = count.value;
        break;
        case "minutes":
          list.limitType = sbILocalDatabaseSmartMediaList.LIMIT_TYPE_USECS;
          list.limit = count.value * USECS_PER_MINUTE;
        break;
        case "hours":
          list.limitType = sbILocalDatabaseSmartMediaList.LIMIT_TYPE_USECS;
          list.limit = count.value * USECS_PER_HOUR;
        break;
        case "MB":
          list.limitType = sbILocalDatabaseSmartMediaList.LIMIT_TYPE_BYTES;
          list.limit = count.value * BYTES_PER_MB;
        break;
        case "GB":
          list.limitType = sbILocalDatabaseSmartMediaList.LIMIT_TYPE_BYTES;
          list.limit = count.value * BYTES_PER_GB;
        break;
      }
    }
    else {
      list.limitType = sbILocalDatabaseSmartMediaList.LIMIT_TYPE_NONE;
      list.limit = 0;
    }

    // Save select by
    var selectBy = document.getElementById("smart_selected_list");
    if (selectBy.value == "random") {
      list.randomSelection = true;
      list.selectPropertyID = "";
    }
    else {
      list.randomSelection = false;
      setSelectBy(list, selectBy.value);
    }

    list.rebuild();
  }
  
  return true;
}

function doCancel() {
  return true;
}

function getValueForSelectBy(list) {

  var value;
  selectByList.forEach(function(e) {
    if (e.property == list.selectPropertyID &&
        e.direction == list.selectDirection) {
      value = e.value;
    }
  });
  
  if (!value) value = "random";

  return value;
}

function setSelectBy(list, value) {

  selectByList.forEach(function(e) {
    if (e.value == value) {
      list.selectPropertyID = e.property;
      list.selectDirection = e.direction;
    }
  });

}
