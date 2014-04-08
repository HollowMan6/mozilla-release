/* vim: set ft=javascript ts=2 et sw=2 tw=80: */
/* Any copyright is dedicated to the Public Domain.
 http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

// Test computed view key bindings

let test = asyncTest(function*() {
  yield addTab("data:text/html,default styles test");

  info("Adding content to the test page");
  content.document.body.innerHTML = '<style type="text/css"> ' +
    '.matches {color: #F00;}</style>' +
    '<span class="matches">Some styled text</span>' +
    '</div>';

  let {toolbox, inspector, view} = yield openComputedView();

  info("Selecting the test node");
  yield selectNode(".matches", inspector);

  let propView = getFirstVisiblePropertyView(view);
  let rulesTable = propView.matchedSelectorsContainer;
  let matchedExpander = propView.element;

  info("Focusing the property");
  let onMatchedExpanderFocus = once(matchedExpander, "focus", true);
  EventUtils.synthesizeMouseAtCenter(matchedExpander, {}, view.styleWindow);
  yield onMatchedExpanderFocus;

  yield checkToggleKeyBinding(view.styleWindow, "VK_SPACE", rulesTable, inspector);
  yield checkToggleKeyBinding(view.styleWindow, "VK_RETURN", rulesTable, inspector);
  yield checkHelpLinkKeybinding(view);
});

function getFirstVisiblePropertyView(view) {
  let propView = null;
  view.propertyViews.some(p => {
    if (p.visible) {
      propView = p;
      return true;
    }
    return false;
  });

  return propView;
}

function* checkToggleKeyBinding(win, key, rulesTable, inspector) {
  info("Pressing " + key + " key a couple of times to check that the property gets expanded/collapsed");

  let onExpand = inspector.once("computed-view-property-expanded");
  let onCollapse = inspector.once("computed-view-property-collapsed");

  info("Expanding the property");
  EventUtils.synthesizeKey(key, {}, win);
  yield onExpand;
  isnot(rulesTable.innerHTML, "", "The property has been expanded");

  info("Collapsing the property");
  EventUtils.synthesizeKey(key, {}, win);
  yield onCollapse;
  is(rulesTable.innerHTML, "", "The property has been collapsed");
}

function checkHelpLinkKeybinding(view) {
  info("Check that MDN link is opened on \"F1\"");
  let def = promise.defer();

  let propView = getFirstVisiblePropertyView(view);
  propView.mdnLinkClick = function(aEvent) {
    ok(true, "Pressing F1 opened the MDN link");
    def.resolve();
  };

  EventUtils.synthesizeKey("VK_F1", {}, view.styleWindow);
  return def.promise;
}
