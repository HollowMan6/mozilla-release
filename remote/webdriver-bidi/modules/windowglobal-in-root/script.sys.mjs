/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

import { Module } from "chrome://remote/content/shared/messagehandler/Module.sys.mjs";

const lazy = {};

ChromeUtils.defineESModuleGetters(lazy, {
  processExtraData:
    "chrome://remote/content/webdriver-bidi/modules/Intercept.sys.mjs",
  TabManager: "chrome://remote/content/shared/TabManager.sys.mjs",
});

class ScriptModule extends Module {
  destroy() {}

  interceptEvent(name, payload) {
    if (name == "script.message") {
      // Resolve browsing context to a Navigable id.
      payload.source.context = lazy.TabManager.getIdForBrowsingContext(
        payload.source.context
      );

      payload = lazy.processExtraData(this.messageHandler.sessionId, payload);
    }

    return payload;
  }
}

export const script = ScriptModule;
