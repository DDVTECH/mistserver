#include "controller_i18n.h"
#include "backend_lang_catalogs.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <unordered_map>
#include <utility>
#include <vector>

namespace Controller {

  /// code -> (msgid -> msgstr), lazily parsed once from the embedded JSON
  /// blobs in backend_lang_catalogs.h (built from src/lang/<code>.po at
  /// compile time -- see generated/meson.build).
  static const std::unordered_map<std::string, std::unordered_map<std::string, std::string> > &backendCatalogs() {
    static std::unordered_map<std::string, std::unordered_map<std::string, std::string> > catalogs;
    static bool loaded = false;
    if (!loaded) {
      loaded = true;
      for (size_t i = 0; i < backendLangCatalogCount; ++i) {
        JSON::Value parsed;
        parsed.fromString(std::string(backendLangCatalogs[i].json, backendLangCatalogs[i].len));
        std::unordered_map<std::string, std::string> &table = catalogs[backendLangCatalogs[i].code];
        jsonForEachConst(parsed, it) {
          if (it->isString()) { table[it.key()] = it->asStringRef(); }
        }
      }
    }
    return catalogs;
  }

  static std::string baseSubtag(const std::string &code) {
    size_t dash = code.find('-');
    return dash == std::string::npos ? code : code.substr(0, dash);
  }

  static std::string toLower(std::string s) {
    for (size_t i = 0; i < s.size(); ++i) { s[i] = tolower((unsigned char)s[i]); }
    return s;
  }

  std::string tr(const std::string &msgid, const std::string &lang) {
    if (lang.empty() || lang == "en") { return msgid; }
    const std::unordered_map<std::string, std::unordered_map<std::string, std::string> > &catalogs = backendCatalogs();
    std::unordered_map<std::string, std::unordered_map<std::string, std::string> >::const_iterator cat = catalogs.find(lang);
    if (cat == catalogs.end()) { return msgid; }
    std::unordered_map<std::string, std::string>::const_iterator hit = cat->second.find(msgid);
    if (hit == cat->second.end() || hit->second.empty()) { return msgid; }
    return hit->second;
  }

  std::string resolveLang(const HTTP::Parser &H) {
    std::string header = H.GetHeader("Accept-Language");
    if (!header.size()) { return "en"; }

    // Parse "de-DE,de;q=0.9,en;q=0.8" into (code, -weight) pairs; sorting on
    // the negated weight puts the highest-weight candidate first, and
    // stable_sort keeps header order between ties.
    std::vector<std::pair<double, std::string> > candidates;
    size_t pos = 0;
    while (pos < header.size()) {
      size_t comma = header.find(',', pos);
      std::string part = header.substr(pos, comma == std::string::npos ? std::string::npos : comma - pos);
      pos = (comma == std::string::npos) ? header.size() : comma + 1;

      size_t semi = part.find(';');
      std::string code = semi == std::string::npos ? part : part.substr(0, semi);
      double q = 1.0;
      if (semi != std::string::npos) {
        size_t qpos = part.find("q=", semi);
        if (qpos != std::string::npos) { q = atof(part.c_str() + qpos + 2); }
      }
      while (code.size() && isspace((unsigned char)code[0])) { code.erase(0, 1); }
      while (code.size() && isspace((unsigned char)code[code.size() - 1])) { code.erase(code.size() - 1); }
      if (code.size() && code != "*") { candidates.push_back(std::make_pair(-q, code)); }
    }
    std::stable_sort(candidates.begin(), candidates.end());

    const std::unordered_map<std::string, std::unordered_map<std::string, std::string> > &catalogs = backendCatalogs();

    for (size_t i = 0; i < candidates.size(); ++i) {
      std::string want = toLower(candidates[i].second);
      if (want == "en" || want.substr(0, 3) == "en-") { return "en"; }
      // Exact code match first (case-insensitive), then bare language
      // subtag -- same two-pass rule the LSP's own navigator.language
      // detection uses (lsp/i18n.js, MistLang.detect).
      for (std::unordered_map<std::string, std::unordered_map<std::string, std::string> >::const_iterator it = catalogs.begin(); it != catalogs.end(); ++it) {
        if (toLower(it->first) == want) { return it->first; }
      }
      std::string base = baseSubtag(want);
      for (std::unordered_map<std::string, std::unordered_map<std::string, std::string> >::const_iterator it = catalogs.begin(); it != catalogs.end(); ++it) {
        if (toLower(baseSubtag(it->first)) == base) { return it->first; }
      }
    }
    return "en";
  }

  void translateCapabilities(JSON::Value &capabilities, const std::string &lang) {
    if (lang.empty() || lang == "en") { return; }
    static const char *categories[] = {"connectors", "inputs", "processes"};
    for (size_t c = 0; c < sizeof(categories) / sizeof(categories[0]); ++c) {
      if (!capabilities.isMember(categories[c])) { continue; }
      jsonForEach(capabilities[categories[c]], entry) {
        if (entry->isMember("friendly")) { (*entry)["friendly"] = tr((*entry)["friendly"].asStringRef(), lang); }
        if (entry->isMember("desc")) { (*entry)["desc"] = tr((*entry)["desc"].asStringRef(), lang); }
      }
    }
  }

}
