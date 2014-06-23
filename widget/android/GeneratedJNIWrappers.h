// GENERATED CODE
// Generated by the Java program at /build/annotationProcessors at compile time from
// annotations on Java methods. To update, change the annotations on the corresponding Java
// methods and rerun the build. Manually updating this file will cause your build to fail.

#ifndef GeneratedJNIWrappers_h__
#define GeneratedJNIWrappers_h__

#include "nsXPCOMStrings.h"
#include "AndroidJavaWrappers.h"

namespace mozilla {
namespace widget {
namespace android {
void InitStubs(JNIEnv *jEnv);

class GeckoAppShell : public AutoGlobalWrappedJavaObject {
public:
    static void InitStubs(JNIEnv *jEnv);
    static GeckoAppShell* Wrap(jobject obj);
    GeckoAppShell(jobject obj, JNIEnv* env) : AutoGlobalWrappedJavaObject(obj, env) {};
    static void AcknowledgeEvent();
    static void AddPluginViewWrapper(jobject a0, jfloat a1, jfloat a2, jfloat a3, jfloat a4, bool a5);
    static void AlertsProgressListener_OnProgress(const nsAString& a0, int64_t a1, int64_t a2, const nsAString& a3);
    static void CancelVibrate();
    static void CheckURIVisited(const nsAString& a0);
    static void ClearMessageList(int32_t a0);
    static void CloseCamera();
    static void CloseNotification(const nsAString& a0);
    static jstring ConnectionGetMimeType(jobject a0);
    static jobject CreateInputStream(jobject a0);
    static void CreateMessageListWrapper(int64_t a0, int64_t a1, jobjectArray a2, int32_t a3, int32_t a4, bool a5, int32_t a6);
    static void CreateShortcut(const nsAString& a0, const nsAString& a1, const nsAString& a2, const nsAString& a3);
    static void DeleteMessageWrapper(int32_t a0, int32_t a1);
    static void DisableBatteryNotifications();
    static void DisableNetworkNotifications();
    static void DisableScreenOrientationNotifications();
    static void DisableSensor(int32_t a0);
    static void EnableBatteryNotifications();
    static void EnableLocation(bool a0);
    static void EnableLocationHighAccuracy(bool a0);
    static void EnableNetworkNotifications();
    static void EnableScreenOrientationNotifications();
    static void EnableSensor(int32_t a0);
    static void GamepadAdded(int32_t a0, int32_t a1);
    static jobject GetConnection(const nsACString& a0);
    static jobject GetContext();
    static jdoubleArray GetCurrentBatteryInformationWrapper();
    static jdoubleArray GetCurrentNetworkInformationWrapper();
    static jfloat GetDensity();
    static int32_t GetDpiWrapper();
    static jstring GetExtensionFromMimeTypeWrapper(const nsAString& a0);
    static jobjectArray GetHandlersForMimeTypeWrapper(const nsAString& a0, const nsAString& a1);
    static jobjectArray GetHandlersForURLWrapper(const nsAString& a0, const nsAString& a1);
    static jbyteArray GetIconForExtensionWrapper(const nsAString& a0, int32_t a1);
    static void GetMessageWrapper(int32_t a0, int32_t a1);
    static jstring GetMimeTypeFromExtensionsWrapper(const nsAString& a0);
    static void GetNextMessageInListWrapper(int32_t a0, int32_t a1);
    static jstring GetProxyForURIWrapper(const nsAString& a0, const nsAString& a1, const nsAString& a2, int32_t a3);
    static int32_t GetScreenDepthWrapper();
    static int16_t GetScreenOrientationWrapper();
    static bool GetShowPasswordSetting();
    static jintArray GetSystemColoursWrapper();
    static void HandleGeckoMessageWrapper(jobject a0);
    static void HandleUncaughtException(jobject a0, jthrowable a1);
    static void HideProgressDialog();
    static jintArray InitCameraWrapper(const nsAString& a0, int32_t a1, int32_t a2, int32_t a3);
    static bool IsNetworkLinkKnown();
    static bool IsNetworkLinkUp();
    static bool IsTablet();
    static void KillAnyZombies();
    static jclass LoadPluginClass(const nsAString& a0, const nsAString& a1);
    static void LockScreenOrientation(int32_t a0);
    static void MarkURIVisited(const nsAString& a0);
    static void MoveTaskToBack();
    static int32_t NetworkLinkType();
    static void NotifyDefaultPrevented(bool a0);
    static void NotifyIME(int32_t a0);
    static void NotifyIMEChange(const nsAString& a0, int32_t a1, int32_t a2, int32_t a3);
    static void NotifyIMEContext(int32_t a0, const nsAString& a1, const nsAString& a2, const nsAString& a3);
    static void NotifyWakeLockChanged(const nsAString& a0, const nsAString& a1);
    static void NotifyXreExit();
    static bool OpenUriExternal(const nsAString& a0, const nsAString& a1, const nsAString& a2 = EmptyString(), const nsAString& a3 = EmptyString(), const nsAString& a4 = EmptyString(), const nsAString& a5 = EmptyString());
    static void PerformHapticFeedback(bool a0);
    static bool PumpMessageLoop();
    static void RegisterSurfaceTextureFrameListener(jobject a0, int32_t a1);
    static void RemovePluginView(jobject a0, bool a1);
    static void ScanMedia(const nsAString& a0, const nsAString& a1);
    static void ScheduleRestart();
    static void SendMessageWrapper(const nsAString& a0, const nsAString& a1, int32_t a2);
    static void SetFullScreen(bool a0);
    static void SetKeepScreenOn(bool a0);
    static void SetURITitle(const nsAString& a0, const nsAString& a1);
    static void ShowAlertNotificationWrapper(const nsAString& a0, const nsAString& a1, const nsAString& a2, const nsAString& a3, const nsAString& a4);
    static void ShowInputMethodPicker();
    static void StartMonitoringGamepad();
    static void StopMonitoringGamepad();
    static bool UnlockProfile();
    static void UnlockScreenOrientation();
    static void UnregisterSurfaceTextureFrameListener(jobject a0);
    static void Vibrate1(int64_t a0);
    static void VibrateA(jlongArray a0, int32_t a1);
    GeckoAppShell() : AutoGlobalWrappedJavaObject() {};
protected:
    static jclass mGeckoAppShellClass;
    static jmethodID jAcknowledgeEvent;
    static jmethodID jAddPluginViewWrapper;
    static jmethodID jAlertsProgressListener_OnProgress;
    static jmethodID jCancelVibrate;
    static jmethodID jCheckURIVisited;
    static jmethodID jClearMessageList;
    static jmethodID jCloseCamera;
    static jmethodID jCloseNotification;
    static jmethodID jConnectionGetMimeType;
    static jmethodID jCreateInputStream;
    static jmethodID jCreateMessageListWrapper;
    static jmethodID jCreateShortcut;
    static jmethodID jDeleteMessageWrapper;
    static jmethodID jDisableBatteryNotifications;
    static jmethodID jDisableNetworkNotifications;
    static jmethodID jDisableScreenOrientationNotifications;
    static jmethodID jDisableSensor;
    static jmethodID jEnableBatteryNotifications;
    static jmethodID jEnableLocation;
    static jmethodID jEnableLocationHighAccuracy;
    static jmethodID jEnableNetworkNotifications;
    static jmethodID jEnableScreenOrientationNotifications;
    static jmethodID jEnableSensor;
    static jmethodID jGamepadAdded;
    static jmethodID jGetConnection;
    static jmethodID jGetContext;
    static jmethodID jGetCurrentBatteryInformationWrapper;
    static jmethodID jGetCurrentNetworkInformationWrapper;
    static jmethodID jGetDensity;
    static jmethodID jGetDpiWrapper;
    static jmethodID jGetExtensionFromMimeTypeWrapper;
    static jmethodID jGetHandlersForMimeTypeWrapper;
    static jmethodID jGetHandlersForURLWrapper;
    static jmethodID jGetIconForExtensionWrapper;
    static jmethodID jGetMessageWrapper;
    static jmethodID jGetMimeTypeFromExtensionsWrapper;
    static jmethodID jGetNextMessageInListWrapper;
    static jmethodID jGetProxyForURIWrapper;
    static jmethodID jGetScreenDepthWrapper;
    static jmethodID jGetScreenOrientationWrapper;
    static jmethodID jGetShowPasswordSetting;
    static jmethodID jGetSystemColoursWrapper;
    static jmethodID jHandleGeckoMessageWrapper;
    static jmethodID jHandleUncaughtException;
    static jmethodID jHideProgressDialog;
    static jmethodID jInitCameraWrapper;
    static jmethodID jIsNetworkLinkKnown;
    static jmethodID jIsNetworkLinkUp;
    static jmethodID jIsTablet;
    static jmethodID jKillAnyZombies;
    static jmethodID jLoadPluginClass;
    static jmethodID jLockScreenOrientation;
    static jmethodID jMarkURIVisited;
    static jmethodID jMoveTaskToBack;
    static jmethodID jNetworkLinkType;
    static jmethodID jNotifyDefaultPrevented;
    static jmethodID jNotifyIME;
    static jmethodID jNotifyIMEChange;
    static jmethodID jNotifyIMEContext;
    static jmethodID jNotifyWakeLockChanged;
    static jmethodID jNotifyXreExit;
    static jmethodID jOpenUriExternal;
    static jmethodID jPerformHapticFeedback;
    static jmethodID jPumpMessageLoop;
    static jmethodID jRegisterSurfaceTextureFrameListener;
    static jmethodID jRemovePluginView;
    static jmethodID jScanMedia;
    static jmethodID jScheduleRestart;
    static jmethodID jSendMessageWrapper;
    static jmethodID jSetFullScreen;
    static jmethodID jSetKeepScreenOn;
    static jmethodID jSetURITitle;
    static jmethodID jShowAlertNotificationWrapper;
    static jmethodID jShowInputMethodPicker;
    static jmethodID jStartMonitoringGamepad;
    static jmethodID jStopMonitoringGamepad;
    static jmethodID jUnlockProfile;
    static jmethodID jUnlockScreenOrientation;
    static jmethodID jUnregisterSurfaceTextureFrameListener;
    static jmethodID jVibrate1;
    static jmethodID jVibrateA;
};

class JavaDomKeyLocation : public AutoGlobalWrappedJavaObject {
public:
    static void InitStubs(JNIEnv *jEnv);
    static JavaDomKeyLocation* Wrap(jobject obj);
    JavaDomKeyLocation(jobject obj, JNIEnv* env) : AutoGlobalWrappedJavaObject(obj, env) {};
    static jobject valueOf(const nsAString& a0);
    static jobjectArray values();
    static jobject getDOM_KEY_LOCATION_JOYSTICK();
    static jobject getDOM_KEY_LOCATION_LEFT();
    static jobject getDOM_KEY_LOCATION_MOBILE();
    static jobject getDOM_KEY_LOCATION_NUMPAD();
    static jobject getDOM_KEY_LOCATION_RIGHT();
    static jobject getDOM_KEY_LOCATION_STANDARD();
    int32_t getvalue();
    JavaDomKeyLocation() : AutoGlobalWrappedJavaObject() {};
protected:
    static jclass mDomKeyLocationClass;
    static jmethodID jvalueOf;
    static jmethodID jvalues;
    static jfieldID jDOM_KEY_LOCATION_JOYSTICK;
    static jfieldID jDOM_KEY_LOCATION_LEFT;
    static jfieldID jDOM_KEY_LOCATION_MOBILE;
    static jfieldID jDOM_KEY_LOCATION_NUMPAD;
    static jfieldID jDOM_KEY_LOCATION_RIGHT;
    static jfieldID jDOM_KEY_LOCATION_STANDARD;
    static jfieldID jvalue;
};

class GeckoJavaSampler : public AutoGlobalWrappedJavaObject {
public:
    static void InitStubs(JNIEnv *jEnv);
    static GeckoJavaSampler* Wrap(jobject obj);
    GeckoJavaSampler(jobject obj, JNIEnv* env) : AutoGlobalWrappedJavaObject(obj, env) {};
    static jstring GetFrameNameJavaProfilingWrapper(int32_t a0, int32_t a1, int32_t a2);
    static jdouble GetSampleTimeJavaProfiling(int32_t a0, int32_t a1);
    static jstring GetThreadNameJavaProfilingWrapper(int32_t a0);
    static void PauseJavaProfiling();
    static void StartJavaProfiling(int32_t a0, int32_t a1);
    static void StopJavaProfiling();
    static void UnpauseJavaProfiling();
    GeckoJavaSampler() : AutoGlobalWrappedJavaObject() {};
protected:
    static jclass mGeckoJavaSamplerClass;
    static jmethodID jGetFrameNameJavaProfilingWrapper;
    static jmethodID jGetSampleTimeJavaProfiling;
    static jmethodID jGetThreadNameJavaProfilingWrapper;
    static jmethodID jPauseJavaProfiling;
    static jmethodID jStartJavaProfiling;
    static jmethodID jStopJavaProfiling;
    static jmethodID jUnpauseJavaProfiling;
};

class SurfaceBits : public AutoGlobalWrappedJavaObject {
public:
    static void InitStubs(JNIEnv *jEnv);
    static SurfaceBits* Wrap(jobject obj);
    SurfaceBits(jobject obj, JNIEnv* env) : AutoGlobalWrappedJavaObject(obj, env) {};
    SurfaceBits();
    jobject getbuffer();
    void setbuffer(jobject a0);
    int32_t getformat();
    void setformat(int32_t a0);
    int32_t getheight();
    void setheight(int32_t a0);
    int32_t getwidth();
    void setwidth(int32_t a0);
protected:
    static jclass mSurfaceBitsClass;
    static jmethodID jSurfaceBits;
    static jfieldID jbuffer;
    static jfieldID jformat;
    static jfieldID jheight;
    static jfieldID jwidth;
};

class ThumbnailHelper : public AutoGlobalWrappedJavaObject {
public:
    static void InitStubs(JNIEnv *jEnv);
    static ThumbnailHelper* Wrap(jobject obj);
    ThumbnailHelper(jobject obj, JNIEnv* env) : AutoGlobalWrappedJavaObject(obj, env) {};
    static void SendThumbnail(jobject a0, int32_t a1, bool a2, bool a3);
    ThumbnailHelper() : AutoGlobalWrappedJavaObject() {};
protected:
    static jclass mThumbnailHelperClass;
    static jmethodID jSendThumbnail;
};

class DisplayPortMetrics : public AutoGlobalWrappedJavaObject {
public:
    static void InitStubs(JNIEnv *jEnv);
    static DisplayPortMetrics* Wrap(jobject obj);
    DisplayPortMetrics(jobject obj, JNIEnv* env) : AutoGlobalWrappedJavaObject(obj, env) {};
    DisplayPortMetrics(jfloat a0, jfloat a1, jfloat a2, jfloat a3, jfloat a4);
    jobject getMPosition();
    jfloat getResolution();
    DisplayPortMetrics() : AutoGlobalWrappedJavaObject() {};
protected:
    static jclass mDisplayPortMetricsClass;
    static jmethodID jDisplayPortMetrics;
    static jfieldID jMPosition;
    static jfieldID jResolution;
};

class GLController : public AutoGlobalWrappedJavaObject {
public:
    static void InitStubs(JNIEnv *jEnv);
    static GLController* Wrap(jobject obj);
    GLController(jobject obj, JNIEnv* env) : AutoGlobalWrappedJavaObject(obj, env) {};
    jobject CreateEGLSurfaceForCompositorWrapper();
    GLController() : AutoGlobalWrappedJavaObject() {};
protected:
    static jclass mGLControllerClass;
    static jmethodID jCreateEGLSurfaceForCompositorWrapper;
};

class GeckoLayerClient : public AutoGlobalWrappedJavaObject {
public:
    static void InitStubs(JNIEnv *jEnv);
    static GeckoLayerClient* Wrap(jobject obj);
    GeckoLayerClient(jobject obj, JNIEnv* env) : AutoGlobalWrappedJavaObject(obj, env) {};
    void ActivateProgram();
    void ContentDocumentChanged();
    jobject CreateFrame();
    void DeactivateProgram();
    jobject GetDisplayPort(bool a0, bool a1, int32_t a2, jobject a3);
    bool IsContentDocumentDisplayed();
    jobject ProgressiveUpdateCallback(bool a0, jfloat a1, jfloat a2, jfloat a3, jfloat a4, jfloat a5, bool a6);
    void SetFirstPaintViewport(jfloat a0, jfloat a1, jfloat a2, jfloat a3, jfloat a4, jfloat a5, jfloat a6);
    void SetPageRect(jfloat a0, jfloat a1, jfloat a2, jfloat a3);
    jobject SyncFrameMetrics(jfloat a0, jfloat a1, jfloat a2, jfloat a3, jfloat a4, jfloat a5, jfloat a6, bool a7, int32_t a8, int32_t a9, int32_t a10, int32_t a11, jfloat a12, bool a13);
    jobject SyncViewportInfo(int32_t a0, int32_t a1, int32_t a2, int32_t a3, jfloat a4, bool a5);
    GeckoLayerClient() : AutoGlobalWrappedJavaObject() {};
protected:
    static jclass mGeckoLayerClientClass;
    static jmethodID jActivateProgram;
    static jmethodID jContentDocumentChanged;
    static jmethodID jCreateFrame;
    static jmethodID jDeactivateProgram;
    static jmethodID jGetDisplayPort;
    static jmethodID jIsContentDocumentDisplayed;
    static jmethodID jProgressiveUpdateCallback;
    static jmethodID jSetFirstPaintViewport;
    static jmethodID jSetPageRect;
    static jmethodID jSyncFrameMetrics;
    static jmethodID jSyncViewportInfo;
};

class ImmutableViewportMetrics : public AutoGlobalWrappedJavaObject {
public:
    static void InitStubs(JNIEnv *jEnv);
    static ImmutableViewportMetrics* Wrap(jobject obj);
    ImmutableViewportMetrics(jobject obj, JNIEnv* env) : AutoGlobalWrappedJavaObject(obj, env) {};
    ImmutableViewportMetrics(jfloat a0, jfloat a1, jfloat a2, jfloat a3, jfloat a4, jfloat a5, jfloat a6, jfloat a7, jfloat a8, jfloat a9, jfloat a10, jfloat a11, jfloat a12);
    ImmutableViewportMetrics() : AutoGlobalWrappedJavaObject() {};
protected:
    static jclass mImmutableViewportMetricsClass;
    static jmethodID jImmutableViewportMetrics;
};

class LayerView : public AutoGlobalWrappedJavaObject {
public:
    static void InitStubs(JNIEnv *jEnv);
    static LayerView* Wrap(jobject obj);
    LayerView(jobject obj, JNIEnv* env) : AutoGlobalWrappedJavaObject(obj, env) {};
    static jobject RegisterCompositorWrapper();
    LayerView() : AutoGlobalWrappedJavaObject() {};
protected:
    static jclass mLayerViewClass;
    static jmethodID jRegisterCompositorWrapper;
};

class NativePanZoomController : public AutoGlobalWrappedJavaObject {
public:
    static void InitStubs(JNIEnv *jEnv);
    static NativePanZoomController* Wrap(jobject obj);
    NativePanZoomController(jobject obj, JNIEnv* env) : AutoGlobalWrappedJavaObject(obj, env) {};
    void PostDelayedCallbackWrapper(int64_t a0);
    void RequestContentRepaintWrapper(jfloat a0, jfloat a1, jfloat a2, jfloat a3, jfloat a4);
    NativePanZoomController() : AutoGlobalWrappedJavaObject() {};
protected:
    static jclass mNativePanZoomControllerClass;
    static jmethodID jPostDelayedCallbackWrapper;
    static jmethodID jRequestContentRepaintWrapper;
};

class ProgressiveUpdateData : public AutoGlobalWrappedJavaObject {
public:
    static void InitStubs(JNIEnv *jEnv);
    static ProgressiveUpdateData* Wrap(jobject obj);
    ProgressiveUpdateData(jobject obj, JNIEnv* env) : AutoGlobalWrappedJavaObject(obj, env) {};
    ProgressiveUpdateData();
    void setViewport(jobject a0);
    bool getabort();
    void setabort(bool a0);
    jfloat getscale();
    void setscale(jfloat a0);
    jfloat getx();
    void setx(jfloat a0);
    jfloat gety();
    void sety(jfloat a0);
protected:
    static jclass mProgressiveUpdateDataClass;
    static jmethodID jProgressiveUpdateData;
    static jmethodID jsetViewport;
    static jfieldID jabort;
    static jfieldID jscale;
    static jfieldID jx;
    static jfieldID jy;
};

class ViewTransform : public AutoGlobalWrappedJavaObject {
public:
    static void InitStubs(JNIEnv *jEnv);
    static ViewTransform* Wrap(jobject obj);
    ViewTransform(jobject obj, JNIEnv* env) : AutoGlobalWrappedJavaObject(obj, env) {};
    ViewTransform(jfloat a0, jfloat a1, jfloat a2);
    jfloat getfixedLayerMarginBottom();
    void setfixedLayerMarginBottom(jfloat a0);
    jfloat getfixedLayerMarginLeft();
    void setfixedLayerMarginLeft(jfloat a0);
    jfloat getfixedLayerMarginRight();
    void setfixedLayerMarginRight(jfloat a0);
    jfloat getfixedLayerMarginTop();
    void setfixedLayerMarginTop(jfloat a0);
    jfloat getoffsetX();
    void setoffsetX(jfloat a0);
    jfloat getoffsetY();
    void setoffsetY(jfloat a0);
    jfloat getscale();
    void setscale(jfloat a0);
    jfloat getx();
    void setx(jfloat a0);
    jfloat gety();
    void sety(jfloat a0);
    ViewTransform() : AutoGlobalWrappedJavaObject() {};
protected:
    static jclass mViewTransformClass;
    static jmethodID jViewTransform;
    static jfieldID jfixedLayerMarginBottom;
    static jfieldID jfixedLayerMarginLeft;
    static jfieldID jfixedLayerMarginRight;
    static jfieldID jfixedLayerMarginTop;
    static jfieldID joffsetX;
    static jfieldID joffsetY;
    static jfieldID jscale;
    static jfieldID jx;
    static jfieldID jy;
};

class NativeZip : public AutoGlobalWrappedJavaObject {
public:
    static void InitStubs(JNIEnv *jEnv);
    static NativeZip* Wrap(jobject obj);
    NativeZip(jobject obj, JNIEnv* env) : AutoGlobalWrappedJavaObject(obj, env) {};
    jobject CreateInputStream(jobject a0, int32_t a1);
    NativeZip() : AutoGlobalWrappedJavaObject() {};
protected:
    static jclass mNativeZipClass;
    static jmethodID jCreateInputStream;
};

class MatrixBlobCursor : public AutoGlobalWrappedJavaObject {
public:
    static void InitStubs(JNIEnv *jEnv);
    static MatrixBlobCursor* Wrap(jobject obj);
    MatrixBlobCursor(jobject obj, JNIEnv* env) : AutoGlobalWrappedJavaObject(obj, env) {};
    MatrixBlobCursor(jobjectArray a0);
    MatrixBlobCursor(jobjectArray a0, int32_t a1);
    void AddRow(jobject a0);
    void AddRow(jobject a0, int32_t a1);
    void AddRow(jobjectArray a0);
    MatrixBlobCursor() : AutoGlobalWrappedJavaObject() {};
protected:
    static jclass mMatrixBlobCursorClass;
    static jmethodID jMatrixBlobCursor;
    static jmethodID jMatrixBlobCursor0;
    static jmethodID jAddRow;
    static jmethodID jAddRow1;
    static jmethodID jAddRow2;
};

class SQLiteBridgeException : public AutoGlobalWrappedJavaObject {
public:
    static void InitStubs(JNIEnv *jEnv);
    static SQLiteBridgeException* Wrap(jobject obj);
    SQLiteBridgeException(jobject obj, JNIEnv* env) : AutoGlobalWrappedJavaObject(obj, env) {};
    SQLiteBridgeException();
    SQLiteBridgeException(const nsAString& a0);
    static int64_t getserialVersionUID();
protected:
    static jclass mSQLiteBridgeExceptionClass;
    static jmethodID jSQLiteBridgeException;
    static jmethodID jSQLiteBridgeException0;
    static jfieldID jserialVersionUID;
};

class Clipboard : public AutoGlobalWrappedJavaObject {
public:
    static void InitStubs(JNIEnv *jEnv);
    static Clipboard* Wrap(jobject obj);
    Clipboard(jobject obj, JNIEnv* env) : AutoGlobalWrappedJavaObject(obj, env) {};
    static void ClearText();
    static jstring GetClipboardTextWrapper();
    static bool HasText();
    static void SetClipboardText(const nsAString& a0);
    Clipboard() : AutoGlobalWrappedJavaObject() {};
protected:
    static jclass mClipboardClass;
    static jmethodID jClearText;
    static jmethodID jGetClipboardTextWrapper;
    static jmethodID jHasText;
    static jmethodID jSetClipboardText;
};


} /* android */
} /* widget */
} /* mozilla */
#endif
