/* vim:st=2:sts=2:sw=2:
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

module.metadata = {
  "stability": "deprecated"
};

const memory = require('./memory');
var timer = require("../timers");
var cfxArgs = require("@test/options");

exports.findAndRunTests = function findAndRunTests(options) {
  var TestFinder = require("./unit-test-finder").TestFinder;
  var finder = new TestFinder({
    filter: options.filter,
    testInProcess: options.testInProcess,
    testOutOfProcess: options.testOutOfProcess
  });
  var runner = new TestRunner({fs: options.fs});
  finder.findTests(
    function (tests) {
      runner.startMany({tests: tests,
                        stopOnError: options.stopOnError,
                        onDone: options.onDone});
    });
};

var TestRunner = exports.TestRunner = function TestRunner(options) {
  if (options) {
    this.fs = options.fs;
  }
  this.console = (options && "console" in options) ? options.console : console;
  memory.track(this);
  this.passed = 0;
  this.failed = 0;
  this.testRunSummary = [];
  this.expectFailNesting = 0;
};

TestRunner.prototype = {
  toString: function toString() "[object TestRunner]",

  DEFAULT_PAUSE_TIMEOUT: 5*60000,
  PAUSE_DELAY: 500,

  _logTestFailed: function _logTestFailed(why) {
    if (!(why in this.test.errors))
      this.test.errors[why] = 0;
    this.test.errors[why]++;
  },

  pass: function pass(message) {
    if(!this.expectFailure) {
      if ("testMessage" in this.console)
        this.console.testMessage(true, true, this.test.name, message);
      else
        this.console.info("pass:", message);
      this.passed++;
      this.test.passed++;
    }
    else {
      this.expectFailure = false;
      this._logTestFailed("failure");
      if ("testMessage" in this.console) {
        this.console.testMessage(true, false, this.test.name, message);
      }
      else {
        this.console.error("fail:", 'Failure Expected: ' + message)
        this.console.trace();
      }
      this.failed++;
      this.test.failed++;
    }
  },

  fail: function fail(message) {
    if(!this.expectFailure) {
      this._logTestFailed("failure");
      if ("testMessage" in this.console) {
        this.console.testMessage(false, false, this.test.name, message);
      }
      else {
        this.console.error("fail:", message)
        this.console.trace();
      }
      this.failed++;
      this.test.failed++;
    }
    else {
      this.expectFailure = false;
      if ("testMessage" in this.console)
        this.console.testMessage(false, true, this.test.name, message);
      else
        this.console.info("pass:", message);
      this.passed++;
      this.test.passed++;
    }
  },

  expectFail: function(callback) {
    this.expectFailure = true;
    callback();
    this.expectFailure = false;
  },

  exception: function exception(e) {
    this._logTestFailed("exception");
    if (cfxArgs.parseable)
      this.console.print("TEST-UNEXPECTED-FAIL | " + this.test.name + " | " + e + "\n");
    this.console.exception(e);
    this.failed++;
    this.test.failed++;
  },

  assertMatches: function assertMatches(string, regexp, message) {
    if (regexp.test(string)) {
      if (!message)
        message = uneval(string) + " matches " + uneval(regexp);
      this.pass(message);
    } else {
      var no = uneval(string) + " doesn't match " + uneval(regexp);
      if (!message)
        message = no;
      else
        message = message + " (" + no + ")";
      this.fail(message);
    }
  },

  assertRaises: function assertRaises(func, predicate, message) {
    try {
      func();
      if (message)
        this.fail(message + " (no exception thrown)");
      else
        this.fail("function failed to throw exception");
    } catch (e) {
      var errorMessage;
      if (typeof(e) == "string")
        errorMessage = e;
      else
        errorMessage = e.message;
      if (typeof(predicate) == "string")
        this.assertEqual(errorMessage, predicate, message);
      else
        this.assertMatches(errorMessage, predicate, message);
    }
  },

  assert: function assert(a, message) {
    if (!a) {
      if (!message)
        message = "assertion failed, value is " + a;
      this.fail(message);
    } else
      this.pass(message || "assertion successful");
  },

  assertNotEqual: function assertNotEqual(a, b, message) {
    if (a != b) {
      if (!message)
        message = "a != b != " + uneval(a);
      this.pass(message);
    } else {
      var equality = uneval(a) + " == " + uneval(b);
      if (!message)
        message = equality;
      else
        message += " (" + equality + ")";
      this.fail(message);
    }
  },

  assertEqual: function assertEqual(a, b, message) {
    if (a == b) {
      if (!message)
        message = "a == b == " + uneval(a);
      this.pass(message);
    } else {
      var inequality = uneval(a) + " != " + uneval(b);
      if (!message)
        message = inequality;
      else
        message += " (" + inequality + ")";
      this.fail(message);
    }
  },

  assertNotStrictEqual: function assertNotStrictEqual(a, b, message) {
    if (a !== b) {
      if (!message)
        message = "a !== b !== " + uneval(a);
      this.pass(message);
    } else {
      var equality = uneval(a) + " === " + uneval(b);
      if (!message)
        message = equality;
      else
        message += " (" + equality + ")";
      this.fail(message);
    }
  },

  assertStrictEqual: function assertStrictEqual(a, b, message) {
    if (a === b) {
      if (!message)
        message = "a === b === " + uneval(a);
      this.pass(message);
    } else {
      var inequality = uneval(a) + " !== " + uneval(b);
      if (!message)
        message = inequality;
      else
        message += " (" + inequality + ")";
      this.fail(message);
    }
  },

  assertFunction: function assertFunction(a, message) {
    this.assertStrictEqual('function', typeof a, message);
  },

  assertUndefined: function(a, message) {
    this.assertStrictEqual('undefined', typeof a, message);
  },

  assertNotUndefined: function(a, message) {
    this.assertNotStrictEqual('undefined', typeof a, message);
  },

  assertNull: function(a, message) {
    this.assertStrictEqual(null, a, message);
  },

  assertNotNull: function(a, message) {
    this.assertNotStrictEqual(null, a, message);
  },

  assertObject: function(a, message) {
    this.assertStrictEqual('[object Object]', Object.prototype.toString.apply(a), message);
  },

  assertString: function(a, message) {
    this.assertStrictEqual('[object String]', Object.prototype.toString.apply(a), message);
  },

  assertArray: function(a, message) {
    this.assertStrictEqual('[object Array]', Object.prototype.toString.apply(a), message);
  },
  
  assertNumber: function(a, message) {
    this.assertStrictEqual('[object Number]', Object.prototype.toString.apply(a), message);                
  },

  done: function done() {
    if (!this.isDone) {
      this.isDone = true;
      if(this.test.teardown) {
        this.test.teardown(this);
      }
      if (this.waitTimeout !== null) {
        timer.clearTimeout(this.waitTimeout);
        this.waitTimeout = null;
      }
      // Do not leave any callback set when calling to `waitUntil`
      this.waitUntilCallback = null;
      if (this.test.passed == 0 && this.test.failed == 0) {
        this._logTestFailed("empty test");
        this.failed++;
        this.test.failed++;
      }
      
      this.testRunSummary.push({
        name: this.test.name,
        passed: this.test.passed,
        failed: this.test.failed,
        errors: [error for (error in this.test.errors)].join(", ")
      });
      
      if (this.onDone !== null) {
        var onDone = this.onDone;
        var self = this;
        this.onDone = null;
        timer.setTimeout(function() { onDone(self); }, 0);
      }
    }
  },
  
  // Set of assertion functions to wait for an assertion to become true
  // These functions take the same arguments as the TestRunner.assert* methods.
  waitUntil: function waitUntil() {
    return this._waitUntil(this.assert, arguments);
  },
  
  waitUntilNotEqual: function waitUntilNotEqual() {
    return this._waitUntil(this.assertNotEqual, arguments);
  },
  
  waitUntilEqual: function waitUntilEqual() {
    return this._waitUntil(this.assertEqual, arguments);
  },
  
  waitUntilMatches: function waitUntilMatches() {
    return this._waitUntil(this.assertMatches, arguments);
  },
  
  /**
   * Internal function that waits for an assertion to become true.
   * @param {Function} assertionMethod
   *    Reference to a TestRunner assertion method like test.assert, 
   *    test.assertEqual, ...
   * @param {Array} args
   *    List of arguments to give to the previous assertion method. 
   *    All functions in this list are going to be called to retrieve current
   *    assertion values.
   */
  _waitUntil: function waitUntil(assertionMethod, args) {
    let count = 0;
    let maxCount = this.DEFAULT_PAUSE_TIMEOUT / this.PAUSE_DELAY;

    // We need to ensure that test is asynchronous
    if (!this.waitTimeout)
      this.waitUntilDone(this.DEFAULT_PAUSE_TIMEOUT);

    let callback = null;
    let finished = false;
    
    let test = this;

    // capture a traceback before we go async.
    let traceback = require("../console/traceback");
    let stack = traceback.get();
    stack.splice(-2, 2);
    let currentWaitStack = traceback.format(stack);
    let timeout = null;

    function loop(stopIt) {
      timeout = null;

      // Build a mockup object to fake TestRunner API and intercept calls to
      // pass and fail methods, in order to retrieve nice error messages
      // and assertion result
      let mock = {
        pass: function (msg) {
          test.pass(msg);
          test.waitUntilCallback = null;
          if (callback && !stopIt)
            callback();
          finished = true;
        },
        fail: function (msg) {
          // If we are called on test timeout, we stop the loop
          // and print which test keeps failing:
          if (stopIt) {
            test.console.error("test assertion never became true:\n",
                               msg + "\n",
                               currentWaitStack);
            if (timeout)
              timer.clearTimeout(timeout);
            return;
          }
          timeout = timer.setTimeout(loop, test.PAUSE_DELAY);
        }
      };
      
      // Automatically call args closures in order to build arguments for 
      // assertion function
      let appliedArgs = [];
      for (let i = 0, l = args.length; i < l; i++) {
        let a = args[i];
        if (typeof a == "function") {
          try {
            a = a();
          }
          catch(e) {
            test.fail("Exception when calling asynchronous assertion: " + e +
                      "\n" + e.stack);
            finished = true;
            return;
          }
        }
        appliedArgs.push(a);
      }
      
      // Finally call assertion function with current assertion values
      assertionMethod.apply(mock, appliedArgs);
    }
    loop();
    this.waitUntilCallback = loop;
    
    // Return an object with `then` method, to offer a way to execute 
    // some code when the assertion passed or failed
    return {
      then: function (c) {
        callback = c;
        
        // In case of immediate positive result, we need to execute callback
        // immediately here:
        if (finished)
          callback();
      }
    };
  },
  
  waitUntilDone: function waitUntilDone(ms) {
    if (ms === undefined)
      ms = this.DEFAULT_PAUSE_TIMEOUT;

    var self = this;

    function tiredOfWaiting() {
      self._logTestFailed("timed out");
      if (self.waitUntilCallback) {
        self.waitUntilCallback(true);
        self.waitUntilCallback = null;
      }
      self.failed++;
      self.test.failed++;
      self.done();
    }

    // We may already have registered a timeout callback
    if (this.waitTimeout)
      timer.clearTimeout(this.waitTimeout);

    this.waitTimeout = timer.setTimeout(tiredOfWaiting, ms);
  },

  startMany: function startMany(options) {
    function runNextTest(self) {
      var test = options.tests.shift();
      if (options.stopOnError && self.test && self.test.failed) {
        self.console.error("aborted: test failed and --stop-on-error was specified");
        options.onDone(self);
      } else if (test) {
        self.start({test: test, onDone: runNextTest});
      } else {
        options.onDone(self);
      }
    }
    runNextTest(this);
  },

  start: function start(options) {
    this.test = options.test;
    this.test.passed = 0;
    this.test.failed = 0;
    this.test.errors = {};

    this.isDone = false;
    this.onDone = function(self) {
      if (cfxArgs.parseable)
        self.console.print("TEST-END | " + self.test.name + "\n");
      options.onDone(self);
    }
    this.waitTimeout = null;

    try {
      if (cfxArgs.parseable)
        this.console.print("TEST-START | " + this.test.name + "\n");
      else
        this.console.info("executing '" + this.test.name + "'");

      if(this.test.setup) {
        this.test.setup(this);
      }
      this.test.testFunction(this);
    } catch (e) {
      this.exception(e);
    }
    if (this.waitTimeout === null)
      this.done();
  }
};
