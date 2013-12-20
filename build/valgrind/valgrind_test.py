#!/usr/bin/python
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

from mozprofile import FirefoxProfile, Profile, Preferences
from mozprofile.permissions import ServerLocations
from mozrunner import FirefoxRunner, CLI
from mozhttpd import MozHttpd
import json
import socket
import threading
import os
import sys
import shutil
import tempfile
from datetime import datetime
from mozbuild.base import MozbuildObject

PORT = 8888

if __name__ == '__main__':
  cli = CLI()
  debug_args, interactive = cli.debugger_arguments()

  build = MozbuildObject.from_environment()
  # XXX: currently we just use the PGO inputs for Valgrind runs.  This may
  # change in the future.
  httpd = MozHttpd(port=PORT,
                   docroot=os.path.join(build.topsrcdir, "build", "pgo"))
  httpd.start(block=False)

  locations = ServerLocations()
  locations.add_host(host='127.0.0.1',
                     port=PORT,
                     options='primary,privileged')

  #TODO: mozfile.TemporaryDirectory
  profilePath = tempfile.mkdtemp()
  try:
    #TODO: refactor this into mozprofile
    prefpath = os.path.join(build.topsrcdir, "testing", "profiles", "prefs_general.js")
    prefs = {}
    prefs.update(Preferences.read_prefs(prefpath))
    interpolation = { "server": "%s:%d" % httpd.httpd.server_address,
                      "OOP": "false"}
    prefs = json.loads(json.dumps(prefs) % interpolation)
    for pref in prefs:
      prefs[pref] = Preferences.cast(prefs[pref])
    profile = FirefoxProfile(profile=profilePath,
                             preferences=prefs,
                             addons=[os.path.join(build.distdir, 'xpi-stage', 'quitter')],
                             locations=locations)

    env = os.environ.copy()
    env["MOZ_CRASHREPORTER_NO_REPORT"] = "1"
    env["XPCOM_DEBUG_BREAK"] = "warn"
    jarlog = os.getenv("JARLOG_FILE")
    if jarlog:
      env["MOZ_JAR_LOG_FILE"] = os.path.abspath(jarlog)
      print "jarlog: %s" % env["MOZ_JAR_LOG_FILE"]

    cmdargs = ["http://localhost:%d/index.html" % PORT]
    runner = FirefoxRunner(profile=profile,
                           binary=build.get_binary_path(),
                           cmdargs=cmdargs,
                           env=env)
    runner.start(debug_args=debug_args, interactive=interactive)
    status = runner.wait()
    httpd.stop()
    if status != 0:
        status = 1      # normalize status, in case it's larger than 127

    sys.exit(status)
  finally:
    shutil.rmtree(profilePath)
