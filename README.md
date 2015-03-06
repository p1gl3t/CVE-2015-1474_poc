This code is ment to be a tentative of a poc for CVE-2015-1474.
The code is based on the code of the screencap comand, but generates a rogue parcel that crashes the surfaceflinger when it is deserialized.
See more at http://forum.xda-developers.com/kindle-fire-hdx/orig-development/evaluating-cve-2015-1474-to-escalate-to-t3045163.

Clone under frameworks/base/cmds/badscreencap and compile with mmm frameworks/base/cmds/badscreencap.
To be able to compile you change the visibility of BBinder::onTransact to public (in frameworks/native/include/Binder.h). This is required for getting the vtable offset of that method.

You can pull the required .so's from your device (or OTA package) if you do not want to build the entire native Android framework.
