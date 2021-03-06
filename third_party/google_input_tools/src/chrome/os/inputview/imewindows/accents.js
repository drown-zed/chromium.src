// Copyright 2014 The ChromeOS IME Authors. All Rights Reserved.
// limitations under the License.
// See the License for the specific language governing permissions and
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// distributed under the License is distributed on an "AS-IS" BASIS,
// Unless required by applicable law or agreed to in writing, software
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// You may obtain a copy of the License at
// you may not use this file except in compliance with the License.
// Licensed under the Apache License, Version 2.0 (the "License");
//
goog.provide('i18n.input.chrome.inputview.Accents');

goog.require('goog.dom');
goog.require('goog.math.Coordinate');
goog.require('goog.style');


goog.scope(function() {
var Accents = i18n.input.chrome.inputview.Accents;


/**
 * The highlighted element.
 *
 * @type {Element}
 * @private
 */
Accents.highlightedItem_ = null;


/**
 * Gets the highlighted character.
 *
 * @return {string} The character.
 * @private
 */
Accents.getHighlightedAccent_ = function() {
  return Accents.highlightedItem_ ? Accents.highlightedItem_.textContent : '';
};


/**
 * Highlights the item according to the current coordinate of the finger.
 *
 * @param {number} x The x position of finger in screen coordinate system.
 * @param {number} y The y position of finger in screen coordinate system.
 * @param {number} offset The offset to cancel highlight.
 * @private
 */
Accents.highlightItem_ = function(x, y, offset) {
  var highlightedItem = Accents.getHighlightedItem_(x, y, offset);
  if (Accents.highlightedItem_ != highlightedItem) {
    if (Accents.highlightedItem_) {
      Accents.highlightedItem_.classList.remove('highlight');
    }
    Accents.highlightedItem_ = highlightedItem;
    if (Accents.highlightedItem_) {
      Accents.highlightedItem_.classList.add('highlight');
    }
  }
};


/**
 * Gets the higlighted item from |x| and |y| position.
 * @param {number} x The x position of finger in screen coordinate system.
 * @param {number} y The y position of finger in screen coordinate system.
 * @param {number} offset The offset to cancel highlight.
 * @private
 */
Accents.getHighlightedItem_ = function(x, y, offset) {
  var dom = goog.dom.getDomHelper();
  var row = null;
  var rows = dom.getElementsByClass('accent-row');
  for (var i = 0; i < rows.length; i++) {
    var coordinate = goog.style.getClientPosition(rows[i]);
    var size = goog.style.getSize(rows[i]);
    var screenYStart = coordinate.y + window.screenY;
    screenYStart = i == 0 ? screenYStart - offset : screenYStart;
    var screenYEnd = coordinate.y + window.screenY + size.height;
    screenYEnd = i == rows.length - 1 ? screenYEnd + offset : screenYEnd;
    if (screenYStart < y && screenYEnd > y) {
      row = rows[i];
      break;
    }
  }
  if (row) {
    var children = dom.getChildren(row);
    for (var i = 0; i < children.length; i++) {
      var coordinate = goog.style.getClientPosition(children[i]);
      var size = goog.style.getSize(children[i]);
      var screenXStart = coordinate.x + window.screenX;
      screenXStart = i == 0 ? screenXStart - offset : screenXStart;
      var screenXEnd = coordinate.x + window.screenX + size.width;
      screenXEnd = i == children.length - 1 ? screenXEnd + offset : screenXEnd;
      if (screenXStart < x && screenXEnd > x) {
        return children[i];
      }
    }
  }
  return null;
};


/**
 * Sets the accents which this window should display.
 *
 * @param {!Array.<string>} accents The accents to display.
 * @param {!number} numOfColumns The number of colums of this accents window.
 * @param {!number} numOfRows The number of rows of this accents window.
 * @param {number} width The width of accent key.
 * @param {number} height The height of accent key.
 * @param {number} startKeyIndex The index of the start key in bottom row.
 * @private
 */
Accents.setAccents_ = function(accents, numOfColumns, numOfRows, width,
    height, startKeyIndex) {
  var container = document.createElement('div');
  container.id = 'container';
  container.classList.add('accent-container');

  var orderedAccents = Accents.reorderAccents_(accents, numOfColumns, numOfRows,
      startKeyIndex);
  var row = null;
  for (var i = 0; i < orderedAccents.length; i++) {
    var keyElem = document.createElement('div');
    // Even if this is an empty key, we still need to add textDiv. Otherwise,
    // the keys have layout issues.
    var textDiv = document.createElement('div');
    textDiv.textContent = orderedAccents[i];
    keyElem.appendChild(textDiv);
    if (!orderedAccents[i]) {
      keyElem.classList.add('empty-key');
    }
    keyElem.style.width = width;
    keyElem.style.height = height;
    if (i % numOfColumns == 0) {
      if (row) {
        container.appendChild(row);
      }
      row = document.createElement('div');
      row.classList.add('accent-row');
    }
    row.appendChild(keyElem);
  }
  container.appendChild(row);
  document.body.appendChild(container);
};


/**
 * Generates the reordered accents which is optimized for creating accent key
 * elements sequentially(from top to bottom, left to right).
 * Accent in |accents| is ordered according to its frequency(more frequently
 * used appears first). Once reordered, the more frequently used accents will be
 * positioned closer to the parent key. See tests for example.
 * @param {!Array.<string>} accents The accents to display.
 * @param {!number} numOfColumns The number of colums of this accents window.
 * @param {!number} numOfRows The number of rows of this accents window.
 * @param {number} startKeyIndex The index of the start key in bottom row.
 * @private
 */
Accents.reorderAccents_ = function(accents, numOfColumns, numOfRows,
    startKeyIndex) {
  var orderedAccents = new Array(numOfColumns * numOfRows);

  var index = 0;
  // Generates the order to fill keys in a row. Start with startKeyIndex, we try
  // to fill keys on both side(right side first) of the start key.
  var rowOrder = new Array(numOfColumns);
  rowOrder[startKeyIndex] = index;
  for (var i = 1;
      startKeyIndex + i < numOfColumns || startKeyIndex - i >= 0;
      i++) {
    if (startKeyIndex + i < numOfColumns) {
      rowOrder[startKeyIndex + i] = ++index;
    }
    if (startKeyIndex - i >= 0) {
      rowOrder[startKeyIndex - i] = ++index;
    }
  }

  for (var i = numOfRows - 1; i >= 0; i--) {
    for (var j = numOfColumns - 1; j >= 0; j--) {
      index = rowOrder[j] + numOfColumns * (numOfRows - i - 1);
      if (index >= accents.length) {
        orderedAccents[i * numOfColumns + j] = '';
      } else {
        orderedAccents[i * numOfColumns + j] = accents[index];
      }
    }
  }

  return orderedAccents;
};

goog.exportSymbol('accents.highlightedAccent', Accents.getHighlightedAccent_);
goog.exportSymbol('accents.highlightItem', Accents.highlightItem_);
goog.exportSymbol('accents.setAccents', Accents.setAccents_);

});  // goog.scope
