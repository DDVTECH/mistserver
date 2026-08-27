#pragma once
#include <mist/http_parser.h>
#include <mist/json.h>
#include <string>

namespace Controller {
  /// Parses the request's Accept-Language header (RFC 7231, with q= weights)
  /// and returns the best available backend catalog language code, e.g.
  /// "de-DE" -- matched the same way the LSP's own navigator.language
  /// detection works: exact code first, then bare language subtag. Returns
  /// "en" (meaning: no catalog, the msgids are already correct) when the
  /// header is absent, requests English, or nothing offered is available.
  std::string resolveLang(const HTTP::Parser &H);

  /// Looks up msgid in lang's backend catalog. Returns msgid unchanged on a
  /// miss, or when lang == "en". This is the real, per-request translation;
  /// it is distinct from the identity tr(msgid) in <mist/tr.h>, which only
  /// marks capability-definition literals for extraction and cannot itself
  /// translate anything (see that header for why).
  std::string tr(const std::string &msgid, const std::string &lang);

  /// Translates the friendly/desc fields of every connectors/inputs/processes
  /// entry in a capabilities tree, in place. Must only ever be called on a
  /// per-request copy (e.g. Response["capabilities"]), never on the shared
  /// Controller::capabilities cache -- translating the cache in place would
  /// leak one request's language into every other request.
  void translateCapabilities(JSON::Value &capabilities, const std::string &lang);
}
