/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

/**
 * Tests that conditional breakpoint expressions survive disabled breakpoints.
 */

const TAB_URL = EXAMPLE_URL + "doc_conditional-breakpoints.html";

function test() {
  let gTab, gDebuggee, gPanel, gDebugger;
  let gSources, gBreakpoints, gLocation;

  initDebugger(TAB_URL).then(([aTab, aDebuggee, aPanel]) => {
    gTab = aTab;
    gDebuggee = aDebuggee;
    gPanel = aPanel;
    gDebugger = gPanel.panelWin;
    gSources = gDebugger.DebuggerView.Sources;
    gBreakpoints = gDebugger.DebuggerController.Breakpoints;

    gLocation = { url: gSources.selectedValue, line: 18 };

    waitForSourceAndCaretAndScopes(gPanel, ".html", 17)
      .then(addBreakpoint)
      .then(setConditional)
      .then(() => {
        let finished = waitForDebuggerEvents(gPanel, gDebugger.EVENTS.BREAKPOINT_REMOVED);
        toggleBreakpoint();
        return finished;
      })
      .then(() => {
        let finished = waitForDebuggerEvents(gPanel, gDebugger.EVENTS.BREAKPOINT_ADDED);
        toggleBreakpoint();
        return finished;
      })
      .then(testConditionalExpressionOnClient)
      .then(() => {
        let finished = waitForDebuggerEvents(gPanel, gDebugger.EVENTS.CONDITIONAL_BREAKPOINT_POPUP_SHOWING);
        openConditionalPopup();
        return finished;
      })
      .then(testConditionalExpressionInPopup)
      .then(() => resumeDebuggerThenCloseAndFinish(gPanel))
      .then(null, aError => {
        ok(false, "Got an error: " + aError.message + "\n" + aError.stack);
      });

    gDebuggee.ermahgerd();
  });

  function addBreakpoint() {
    return gPanel.addBreakpoint(gLocation);
  }

  function setConditional(aClient) {
    aClient.conditionalExpression = "hello";
  }

  function toggleBreakpoint() {
    EventUtils.sendMouseEvent({ type: "click" },
      gDebugger.document.querySelector(".dbg-breakpoint-checkbox"),
      gDebugger);
  }

  function openConditionalPopup() {
    EventUtils.sendMouseEvent({ type: "click" },
      gDebugger.document.querySelector(".dbg-breakpoint"),
      gDebugger);
  }

  function testConditionalExpressionOnClient() {
    return gBreakpoints._getAdded(gLocation).then(aClient => {
      is(aClient.conditionalExpression, "hello", "The expression is correct (1).");
    });
  }

  function testConditionalExpressionInPopup() {
    let textbox = gDebugger.document.getElementById("conditional-breakpoint-panel-textbox");
    is(textbox.value, "hello", "The expression is correct (2).")
  }
}
