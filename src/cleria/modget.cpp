// Getting mods from the internet (modget.hpp).
#include "cleria/modget.hpp"

#include <cstdlib>
#include <fstream>
#include <iterator>
#include <regex>
#include <system_error>

#include "cleria/json.hpp"
#include "cleria/mods.hpp"
#include "cleria/net/socket.hpp"

namespace fs = std::filesystem;

namespace cleria::mods::get {
namespace {

std::string str(const json::Value& v, const char* k) {
    const json::Value* f = v.find(k);
    return f ? f->as_string() : std::string();
}

bool read_file(const fs::path& p, std::string& out) {
    std::ifstream f(p, std::ios::binary);
    if (!f) return false;
    out.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
    return true;
}

net::HttpResponse github_get(const std::string& url, std::size_t max_body) {
    net::HttpRequest rq;
    rq.url = url;
    rq.headers = {{"User-Agent", "CleriaCore"}, {"Accept", "application/vnd.github+json"}};
    rq.timeout_ms = 30000;
    rq.max_body = max_body;
    return net::http_request(rq);
}

std::string why(const net::HttpResponse& rs) {
    if (!rs.ok) return rs.error.empty() ? "no answer" : rs.error;
    if (rs.truncated) return "the answer was too long";
    return "HTTP " + std::to_string(rs.status);
}

}  // namespace

std::vector<Listed> parse_catalog(std::string_view text) {
    std::vector<Listed> out;
    const auto doc = json::parse(text);
    if (!doc) return out;
    if (const json::Value* list = doc->find("mods"))
        for (const json::Value& m : list->as_array()) {
            Listed l{str(m, "id"), str(m, "name"), str(m, "author"), str(m, "description"), str(m, "repo")};
            if (l.name.empty()) l.name = l.id;
            if (valid_id(l.id) && !github_repo("https://github.com/" + l.repo).empty()) out.push_back(std::move(l));
        }
    return out;
}

std::string github_repo(std::string_view website) {
    static const std::regex re(R"(^https?://(?:www\.)?github\.com/([A-Za-z0-9_.-]+)/([A-Za-z0-9_.-]+?)(?:\.git)?/?$)");
    const std::string s(website);
    std::smatch m;
    return std::regex_match(s, m, re) ? m[1].str() + "/" + m[2].str() : std::string();
}

std::optional<Release> pick_release(std::string_view json_text, std::string_view installed, std::string_view ext) {
    const auto doc = json::parse(json_text);
    if (!doc) return std::nullopt;
    std::string tag = str(*doc, "tag_name");
    if (!tag.empty() && (tag[0] == 'v' || tag[0] == 'V')) tag.erase(0, 1);
    if (tag.empty() || compare_versions(tag, installed) <= 0) return std::nullopt;
    if (const json::Value* assets = doc->find("assets"))
        for (const json::Value& a : assets->as_array()) {
            const std::string name = str(a, "name"), url = str(a, "browser_download_url");
            if (!url.empty() && name.size() > ext.size() && name.compare(name.size() - ext.size(), ext.size(), ext) == 0)
                return Release{tag, url, name};
        }
    return std::nullopt;
}

bool supported() { return net::http_supported() || std::getenv("CLERIA_RELEASES"); }

std::string catalog_text(std::string* error) {
    std::string text;
    if (const char* local = std::getenv("CLERIA_CATALOG")) {
        if (read_file(local, text)) return text;
        if (error) *error = std::string(local) + " could not be read";
        return {};
    }
    if (!net::http_supported()) {
        if (error) *error = "this build cannot download (built without CLERIA_CURL)";
        return {};
    }
    const net::HttpResponse rs = github_get(kCatalogUrl, 1u << 20);
    if (rs.ok && rs.status == 200 && !rs.truncated) return rs.body;
    if (error) *error = "GitHub could not be reached (" + why(rs) + ")";
    return {};
}

std::optional<Release> latest_release(const std::string& repo, const std::string& installed, std::string_view ext,
                                      bool* failed) {
    if (const char* dir = std::getenv("CLERIA_RELEASES")) {   // tests: <dir>/<owner>/<repo>.json
        std::string text;
        if (!read_file(fs::path(dir) / (repo + ".json"), text)) return std::nullopt;   // no release
        return pick_release(text, installed, ext);
    }
    const net::HttpResponse rs = github_get("https://api.github.com/repos/" + repo + "/releases/latest", 1u << 20);
    if (!(rs.ok && rs.status == 200)) {
        if (!(rs.ok && rs.status == 404) && failed) *failed = true;   // 404: no release yet
        return std::nullopt;
    }
    return pick_release(rs.body, installed, ext);
}

bool download(const std::string& url, const fs::path& to, std::string* error) {
    std::error_code ec;
    fs::create_directories(to.parent_path(), ec);
    if (url.rfind("http://", 0) != 0 && url.rfind("https://", 0) != 0) {   // a test's local file (CLERIA_RELEASES)
        if (!std::getenv("CLERIA_RELEASES")) {
            if (error) *error = "not a web address";
            return false;
        }
        fs::copy_file(fs::path(url), to, fs::copy_options::overwrite_existing, ec);
        if (ec && error) *error = ec.message();
        return !ec;
    }
    const net::HttpResponse rs = github_get(url, 64u << 20);
    if (!(rs.ok && rs.status == 200 && !rs.truncated)) {
        if (error) *error = why(rs);
        return false;
    }
    std::ofstream f(to, std::ios::binary | std::ios::trunc);
    f.write(rs.body.data(), std::streamsize(rs.body.size()));
    if (!f) {
        if (error) *error = "the file could not be written";
        return false;
    }
    return true;
}

fs::path download_dir() {
    std::error_code ec;
    fs::path t = fs::temp_directory_path(ec);
    if (ec) t = ".";
    return t / "CleriaCore" / "downloads";
}

}  // namespace cleria::mods::get
