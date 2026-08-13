#pragma once
#include <string>

/// Marks a backend (C++) string literal as translatable, for
/// `xgettext --keyword=tr` extraction -- nothing more.
///
/// This is *not* the same thing as the request-time translation lookup
/// (Controller::tr(msgid, lang) in controller_i18n.h). Calls to this tr()
/// happen at capability-introspection time (e.g. `capa["friendly"] = tr(
/// "MP4 over HTTP");` in a plugin's init()), which runs once, long before
/// any request -- and therefore any requested language -- exists. Its
/// result is cached and reused for every future API request in every
/// language, so it can only ever be an identity function here. The actual
/// per-request translation happens later, in the controller, by looking up
/// the (still-English) msgid this function returned.
inline const char *tr(const char *msgid) { return msgid; }
inline const std::string &tr(const std::string &msgid) { return msgid; }
