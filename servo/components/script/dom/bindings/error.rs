/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

//! Utilities to throw exceptions from Rust bindings.

use dom::bindings::codegen::Bindings::DOMExceptionBinding::DOMExceptionMethods;
use dom::bindings::codegen::PrototypeList::proto_id_to_name;
use dom::bindings::conversions::root_from_object;
use dom::bindings::conversions::{FromJSValConvertible, ToJSValConvertible};
use dom::bindings::global::GlobalRef;
use dom::bindings::str::USVString;
use dom::domexception::{DOMErrorName, DOMException};
use js::error::{throw_range_error, throw_type_error};
use js::jsapi::HandleObject;
use js::jsapi::JSAutoCompartment;
use js::jsapi::JSContext;
use js::jsapi::JSObject;
use js::jsapi::JS_ClearPendingException;
use js::jsapi::JS_ErrorFromException;
use js::jsapi::JS_GetPendingException;
use js::jsapi::JS_IsExceptionPending;
use js::jsapi::JS_SetPendingException;
use js::jsval::UndefinedValue;
use libc::c_uint;
use std::slice::from_raw_parts;

/// DOM exceptions that can be thrown by a native DOM method.
#[derive(Debug, Clone, HeapSizeOf)]
pub enum Error {
    /// IndexSizeError DOMException
    IndexSize,
    /// NotFoundError DOMException
    NotFound,
    /// HierarchyRequestError DOMException
    HierarchyRequest,
    /// WrongDocumentError DOMException
    WrongDocument,
    /// InvalidCharacterError DOMException
    InvalidCharacter,
    /// NotSupportedError DOMException
    NotSupported,
    /// InUseAttributeError DOMException
    InUseAttribute,
    /// InvalidStateError DOMException
    InvalidState,
    /// SyntaxError DOMException
    Syntax,
    /// NamespaceError DOMException
    Namespace,
    /// InvalidAccessError DOMException
    InvalidAccess,
    /// SecurityError DOMException
    Security,
    /// NetworkError DOMException
    Network,
    /// AbortError DOMException
    Abort,
    /// TimeoutError DOMException
    Timeout,
    /// InvalidNodeTypeError DOMException
    InvalidNodeType,
    /// DataCloneError DOMException
    DataClone,
    /// NoModificationAllowedError DOMException
    NoModificationAllowed,
    /// QuotaExceededError DOMException
    QuotaExceeded,
    /// TypeMismatchError DOMException
    TypeMismatch,
    /// InvalidModificationError DOMException
    InvalidModification,

    /// TypeError JavaScript Error
    Type(String),
    /// RangeError JavaScript Error
    Range(String),

    /// A JavaScript exception is already pending.
    JSFailed,
}

/// The return type for IDL operations that can throw DOM exceptions.
pub type Fallible<T> = Result<T, Error>;

/// The return type for IDL operations that can throw DOM exceptions and
/// return `()`.
pub type ErrorResult = Fallible<()>;

/// Set a pending exception for the given `result` on `cx`.
pub unsafe fn throw_dom_exception(cx: *mut JSContext, global: GlobalRef, result: Error) {
    let code = match result {
        Error::IndexSize => DOMErrorName::IndexSizeError,
        Error::NotFound => DOMErrorName::NotFoundError,
        Error::HierarchyRequest => DOMErrorName::HierarchyRequestError,
        Error::WrongDocument => DOMErrorName::WrongDocumentError,
        Error::InvalidCharacter => DOMErrorName::InvalidCharacterError,
        Error::NotSupported => DOMErrorName::NotSupportedError,
        Error::InUseAttribute => DOMErrorName::InUseAttributeError,
        Error::InvalidState => DOMErrorName::InvalidStateError,
        Error::Syntax => DOMErrorName::SyntaxError,
        Error::Namespace => DOMErrorName::NamespaceError,
        Error::InvalidAccess => DOMErrorName::InvalidAccessError,
        Error::Security => DOMErrorName::SecurityError,
        Error::Network => DOMErrorName::NetworkError,
        Error::Abort => DOMErrorName::AbortError,
        Error::Timeout => DOMErrorName::TimeoutError,
        Error::InvalidNodeType => DOMErrorName::InvalidNodeTypeError,
        Error::DataClone => DOMErrorName::DataCloneError,
        Error::NoModificationAllowed => DOMErrorName::NoModificationAllowedError,
        Error::QuotaExceeded => DOMErrorName::QuotaExceededError,
        Error::TypeMismatch => DOMErrorName::TypeMismatchError,
        Error::InvalidModification => DOMErrorName::InvalidModificationError,
        Error::Type(message) => {
            assert!(!JS_IsExceptionPending(cx));
            throw_type_error(cx, &message);
            return;
        },
        Error::Range(message) => {
            assert!(!JS_IsExceptionPending(cx));
            throw_range_error(cx, &message);
            return;
        },
        Error::JSFailed => {
            assert!(JS_IsExceptionPending(cx));
            return;
        }
    };

    assert!(!JS_IsExceptionPending(cx));
    let exception = DOMException::new(global, code);
    rooted!(in(cx) let mut thrown = UndefinedValue());
    exception.to_jsval(cx, thrown.handle_mut());
    JS_SetPendingException(cx, thrown.handle());
}

struct ErrorInfo {
    filename: String,
    message: String,
    lineno: c_uint,
    column: c_uint,
}

impl ErrorInfo {
    unsafe fn from_native_error(cx: *mut JSContext, object: HandleObject)
                                -> Option<ErrorInfo> {
        let report = JS_ErrorFromException(cx, object);
        if report.is_null() {
            return None;
        }

        let filename = {
            let filename = (*report).filename as *const u8;
            if !filename.is_null() {
                let length = (0..).find(|idx| *filename.offset(*idx) == 0).unwrap();
                let filename = from_raw_parts(filename, length as usize);
                String::from_utf8_lossy(filename).into_owned()
            } else {
                "none".to_string()
            }
        };

        let lineno = (*report).lineno;
        let column = (*report).column;

        let message = {
            let message = (*report).ucmessage;
            let length = (0..).find(|idx| *message.offset(*idx) == 0).unwrap();
            let message = from_raw_parts(message, length as usize);
            String::from_utf16_lossy(message)
        };

        Some(ErrorInfo {
            filename: filename,
            message: message,
            lineno: lineno,
            column: column,
        })
    }

    fn from_dom_exception(cx: *mut JSContext, object: HandleObject) -> Option<ErrorInfo> {
        let exception = match root_from_object::<DOMException>(object.get()) {
            Ok(exception) => exception,
            Err(_) => return None,
        };

        Some(ErrorInfo {
            filename: "".to_string(),
            message: exception.Stringifier().into(),
            lineno: 0,
            column: 0,
        })
    }
}

/// Report a pending exception, thereby clearing it.
pub unsafe fn report_pending_exception(cx: *mut JSContext, obj: *mut JSObject) {
    if JS_IsExceptionPending(cx) {
        let _ac = JSAutoCompartment::new(cx, obj);
        rooted!(in(cx) let mut value = UndefinedValue());
        if !JS_GetPendingException(cx, value.handle_mut()) {
            JS_ClearPendingException(cx);
            error!("Uncaught exception: JS_GetPendingException failed");
            return;
        }

        JS_ClearPendingException(cx);
        if !value.is_object() {
            match USVString::from_jsval(cx, value.handle(), ()) {
                Ok(USVString(string)) => error!("Uncaught exception: {}", string),
                Err(_) => error!("Uncaught exception: failed to stringify primitive"),
            }
            return;
        }

        rooted!(in(cx) let object = value.to_object());
        let error_info = ErrorInfo::from_native_error(cx, object.handle())
            .or_else(|| ErrorInfo::from_dom_exception(cx, object.handle()));
        let error_info = match error_info {
            Some(error_info) => error_info,
            None => {
                error!("Uncaught exception: failed to extract information");
                return;
            }
        };

        error!("Error at {}:{}:{} {}",
               error_info.filename,
               error_info.lineno,
               error_info.column,
               error_info.message);
    }
}

/// Throw an exception to signal that a `JSVal` can not be converted to any of
/// the types in an IDL union type.
pub unsafe fn throw_not_in_union(cx: *mut JSContext, names: &'static str) {
    assert!(!JS_IsExceptionPending(cx));
    let error = format!("argument could not be converted to any of: {}", names);
    throw_type_error(cx, &error);
}

/// Throw an exception to signal that a `JSObject` can not be converted to a
/// given DOM type.
pub unsafe fn throw_invalid_this(cx: *mut JSContext, proto_id: u16) {
    debug_assert!(!JS_IsExceptionPending(cx));
    let error = format!("\"this\" object does not implement interface {}.",
                        proto_id_to_name(proto_id));
    throw_type_error(cx, &error);
}
