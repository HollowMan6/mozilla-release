/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

XPCOMUtils.defineLazyModuleGetter(this, "CustomizableUI",
                                  "resource:///modules/CustomizableUI.jsm");
/**
 * Maintains the state and dispatches events for the main menu panel.
 */

const PanelUI = {
  /** Panel events that we listen for. **/
  get kEvents() ["popupshowing", "popupshown", "popuphiding", "popuphidden"],
  /**
   * Used for lazily getting and memoizing elements from the document. Lazy
   * getters are set in init, and memoizing happens after the first retrieval.
   */
  get kElements() {
    return {
      contents: "PanelUI-contents",
      mainView: "PanelUI-mainView",
      multiView: "PanelUI-multiView",
      helpView: "PanelUI-help",
      menuButton: "PanelUI-menu-button",
      panel: "PanelUI-popup",
      zoomResetButton: "PanelUI-zoomReset-btn"
    };
  },

  init: function() {
    for (let [k, v] of Iterator(this.kElements)) {
      // Need to do fresh let-bindings per iteration
      let getKey = k;
      let id = v;
      this.__defineGetter__(getKey, function() {
        delete this[getKey];
        return this[getKey] = document.getElementById(id);
      });
    }

    for (let event of this.kEvents) {
      this.panel.addEventListener(event, this);
    }

    // Register ourselves with the service so we know when the zoom prefs change.
    Services.obs.addObserver(this, "browser-fullZoom:zoomChange", false);
    Services.obs.addObserver(this, "browser-fullZoom:zoomReset", false);

    this._updateZoomResetButton();

    this.helpView.addEventListener("ViewShowing", this._onHelpViewShow, false);
    this.helpView.addEventListener("ViewHiding", this._onHelpViewHide, false);
  },

  uninit: function() {
    for (let event of this.kEvents) {
      this.panel.removeEventListener(event, this);
    }
    this.helpView.removeEventListener("ViewShowing", this._onHelpViewShow);
    this.helpView.removeEventListener("ViewHiding", this._onHelpViewHide);
    Services.obs.removeObserver(this, "browser-fullZoom:zoomChange");
    Services.obs.removeObserver(this, "browser-fullZoom:zoomReset");
  },

  /**
   * Customize mode extracts the mainView and puts it somewhere else while the
   * user customizes. Upon completion, this function can be called to put the
   * panel back to where it belongs in normal browsing mode.
   *
   * @param aMainView
   *        The mainView node to put back into place.
   */
  replaceMainView: function(aMainView) {
    this.multiView.insertBefore(aMainView, this.multiView.firstChild);
  },

  /**
   * Opens the menu panel if it's closed, or closes it if it's
   * open. If the event target has a child with the toolbarbutton-icon
   * attribute, the panel will be anchored on that child. Otherwise, the panel
   * is anchored on the event target itself.
   *
   * @param aEvent the event that triggers the toggle.
   */
  toggle: function(aEvent) {
    if (this.panel.state == "open") {
      this.hide();
    } else if (this.panel.state == "closed") {
      this.ensureRegistered();

      let anchor = aEvent.target;
      let iconAnchor =
        document.getAnonymousElementByAttribute(anchor, "class",
                                                "toolbarbutton-icon");
      this.panel.openPopup(iconAnchor || anchor, "bottomcenter topright");
    }
  },

  /**
   * If the menu panel is being shown, hide it.
   */
  hide: function() {
    this.panel.hidePopup();
  },

  handleEvent: function(aEvent) {
    switch (aEvent.type) {
      case "popupshowing":
        // Fall through
      case "popupshown":
        // Fall through
      case "popuphiding":
        // Fall through
      case "popuphidden": {
        this._updatePanelButton(aEvent.target);
        break;
      }
    }
  },

  /**
   * nsIObserver implementation, responding to zoom pref changes
   */
  observe: function (aSubject, aTopic, aData) {
    this._updateZoomResetButton();
  },

  /**
   * Registering the menu panel is done lazily for performance reasons. This
   * method is exposed so that CustomizationMode can force registration in the
   * event that customization mode is started before the panel has had a chance
   * to register itself.
   */
  ensureRegistered: function() {
    CustomizableUI.registerMenuPanel(this.contents);
  },

  /**
   * Switch the panel to the main view if it's not already
   * in that view.
   */
  showMainView: function() {
    this.multiView.showMainView();
  },

  /**
   * Switch the panel to the help view if it's not already
   * in that view.
   */
  showHelpView: function(aAnchor) {
    this.multiView.showSubView("PanelUI-help", aAnchor);
  },

  /**
   * Shows a subview in the panel with a given ID.
   *
   * @param aViewId the ID of the subview to show.
   * @param aAnchor the element that spawned the subview.
   * @param aPlacementArea the CustomizableUI area that aAnchor is in.
   */
  showSubView: function(aViewId, aAnchor, aPlacementArea) {
    let viewNode = document.getElementById(aViewId);
    if (!viewNode) {
      Cu.reportError("Could not show panel subview with id: " + aViewId);
      return;
    }

    if (!aAnchor) {
      Cu.reportError("Expected an anchor when opening subview with id: " + aViewId);
      return;
    }

    if (aPlacementArea == CustomizableUI.AREA_PANEL) {
      this.multiView.showSubView(aViewId, aAnchor);
    } else {
      // Emit the ViewShowing event so that the widget definition has a chance
      // to lazily populate the subview with things.
      let evt = document.createEvent("CustomEvent");
      evt.initCustomEvent("ViewShowing", true, true, viewNode);
      viewNode.dispatchEvent(evt);

      let tempPanel = document.createElement("panel");
      tempPanel.appendChild(viewNode);
      tempPanel.setAttribute("type", "arrow");
      tempPanel.setAttribute("id", "customizationui-widget-panel");
      document.getElementById(CustomizableUI.AREA_NAVBAR).appendChild(tempPanel);
      tempPanel.addEventListener("popuphidden", function panelRemover() {
        tempPanel.removeEventListener("popuphidden", panelRemover);
        this.multiView.appendChild(viewNode);
        tempPanel.parentElement.removeChild(tempPanel);
      }.bind(this));

      tempPanel.openPopup(aAnchor, "bottomcenter topright");
    }
  },

  /**
   * Sets the anchor node into the open or closed state, depending
   * on the state of the panel.
   */
  _updatePanelButton: function() {
    this.menuButton.open = this.panel.state == "open" ||
                           this.panel.state == "showing";
  },

  _updateZoomResetButton: function() {
    this.zoomResetButton.setAttribute("label", gNavigatorBundle
      .getFormattedString("zoomReset.label", [Math.floor(ZoomManager.zoom * 100)]));
  },

  // Button onclick handler which hides the whole PanelUI
  _onHelpViewClick: function(aEvent) {
    if (aEvent.button == 0 && !aEvent.target.hasAttribute("disabled")) {
      PanelUI.hide();
    }
  },

  _onHelpViewShow: function(aEvent) {
    // Call global menu setup function
    buildHelpMenu();

    let helpMenu = document.getElementById("menu_HelpPopup");
    let items = this.getElementsByTagName("vbox")[0];
    let attrs = ["oncommand", "onclick", "label", "key", "disabled"];
    let NSXUL = "http://www.mozilla.org/keymaster/gatekeeper/there.is.only.xul";

    // Remove all buttons from the view
    while (items.firstChild) {
      items.removeChild(items.firstChild);
    }

    // Add the current set of menuitems of the Help menu to this view
    let menuItems = Array.prototype.slice.call(helpMenu.getElementsByTagName("menuitem"));
    let fragment = document.createDocumentFragment();
    for (let node of menuItems) {
      if (node.hidden)
        continue;
      let button = document.createElementNS(NSXUL, "toolbarbutton");
      // Copy specific attributes from a menuitem of the Help menu
      for (let attrName of attrs) {
        if (!node.hasAttribute(attrName))
          continue;
        button.setAttribute(attrName, node.getAttribute(attrName));
      }
      fragment.appendChild(button);
    }
    items.appendChild(fragment);

    this.addEventListener("click", PanelUI._onHelpViewClick, false);
  },

  _onHelpViewHide: function(aEvent) {
    this.removeEventListener("click", PanelUI._onHelpViewClick);
  }
};
