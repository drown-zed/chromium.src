// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/**
 * @param {!Object.<string, string>} stringData String data.
 * @param {!VolumeManager} volumeManager Volume manager.
 * @constructor
 * @struct
 */
function BackgroundComponents(stringData, volumeManager) {
  /**
   * String data.
   * @type {!Object.<string, string>}
   */
  this.stringData = stringData;

  /**
   * Volume manager.
   * @type {!VolumeManager}
   */
  this.volumeManager = volumeManager;
}

/**
 * Loads background component.
 * @return {!Promise} Promise fulfilled with BackgroundComponents.
 */
BackgroundComponents.load = function() {
  var stringDataPromise = new Promise(function(fulfill) {
    chrome.fileManagerPrivate.getStrings(function(stringData) {
      loadTimeData.data = stringData;
      fulfill(stringData);
    });
  });

  // VolumeManager should be obtained after stringData initialized.
  var volumeManagerPromise = stringDataPromise.then(function() {
    return new Promise(function(fulfill) {
      VolumeManager.getInstance(fulfill);
    });
  });

  return Promise.all([stringDataPromise, volumeManagerPromise]).then(
      function(args) {
        return new BackgroundComponents(args[0], args[1]);
      });
};

/**
 * Resolves file system names and obtains entries.
 * @param {!Array.<!FileEntry>} entries Names of isolated file system.
 * @return {!Promise} Promise to be fulfilled with an entry array.
 */
function resolveEntries(entries) {
  return new Promise(function(fulfill, reject) {
    chrome.fileManagerPrivate.resolveIsolatedEntries(
        entries, function(externalEntries) {
          if (!chrome.runtime.lastError)
            fulfill(externalEntries);
          else
            reject(chrome.runtime.lastError);
        });
  });
}

/**
 * Promise to be fulfilled with singleton instance of background components.
 * @type {Promise}
 */
var backgroundComponentsPromise = null;

/**
 * Promise to be fulfilled with single application window.
 * This can be null when the window is not opened.
 * @type {Promise}
 */
var appWindowPromise = null;

/**
 * Promise to be fulfilled with entries that are used for reopening the
 * application window.
 * @type {Promise}
 */
var reopenEntriesPromise = null;

/**
 * Launches the application with entries.
 *
 * @param {!Promise} selectedEntriesPromise Promise to be fulfilled with the
 *     entries that are stored in the external file system (not in the isolated
 *     file system).
 * @return {!Promise} Promise to be fulfilled after the application is launched.
 */
function launch(selectedEntriesPromise) {
  // If there is the previous window, close the window.
  if (appWindowPromise) {
    reopenEntriesPromise = selectedEntriesPromise;
    appWindowPromise.then(function(appWindow) {
      appWindow.close();
    });
    return Promise.reject('The window has already opened.');
  }
  reopenEntriesPromise = null;

  // Create a new window.
  appWindowPromise = new Promise(function(fulfill) {
    chrome.app.window.create(
        'gallery.html',
        {
          id: 'gallery',
          innerBounds: {
            minWidth: 820,
            minHeight: 544
          },
          frame: 'none'
        },
        function(appWindow) {
          appWindow.contentWindow.addEventListener(
              'load', fulfill.bind(null, appWindow));
          appWindow.onClosed.addListener(function() {
            appWindowPromise = null;
            if (reopenEntriesPromise) {
              // TODO(hirono): This is workaround for crbug.com/442217. Remove
              // this after fixing it.
              setTimeout(function() {
                if (reopenEntriesPromise)
                  launch(reopenEntriesPromise);
              }, 500);
            }
          });
        });
  });

  // Initialize the window document.
  return Promise.all([
    appWindowPromise,
    backgroundComponentsPromise
  ]).then(function(args) {
    var appWindow = /** @type {!chrome.app.window.AppWindow} */ (args[0]);
    var galleryWindow = /** @type {!GalleryWindow} */ (appWindow.contentWindow);
    galleryWindow.initialize(args[1]);
    return selectedEntriesPromise.then(function(entries) {
      galleryWindow.loadEntries(entries);
    });
  });
}

// If the script is loaded from unit test, chrome.app.runtime is not defined.
// In this case, does not run the initialization code for the application.
if (chrome.app.runtime) {
  backgroundComponentsPromise = BackgroundComponents.load();
  chrome.app.runtime.onLaunched.addListener(function(launchData) {
    // Skip if files are not selected.
    if (!launchData || !launchData.items || launchData.items.length === 0)
      return;

    // Obtains entries in non-isolated file systems.
    // The entries in launchData are stored in the isolated file system.
    // We need to map the isolated entries to the normal entries to retrieve
    // their parent directory.
    var isolatedEntries = launchData.items.map(function(item) {
      return item.entry;
    });
    var selectedEntriesPromise = backgroundComponentsPromise.then(function() {
      return resolveEntries(isolatedEntries);
    });

    launch(selectedEntriesPromise).catch(function(error) {
      console.error(error.stack || error);
    });
  });
}

// If is is run in the browser test, wait for the test resources are installed
// as a component extension, and then load the test resources.
if (chrome.test) {
  // Sets a global flag that we are in tests, so other components are aware of
  // it.
  window.IN_TEST = true;

  /** @type {string} */
  window.testExtensionId = 'ejhcmmdhhpdhhgmifplfmjobgegbibkn';
  chrome.runtime.onMessageExternal.addListener(function(message) {
    if (message.name !== 'testResourceLoaded')
      return;
    var script = document.createElement('script');
    script.src =
        'chrome-extension://' + window.testExtensionId +
        '/gallery/test_loader.js';
    document.documentElement.appendChild(script);
  });
}
