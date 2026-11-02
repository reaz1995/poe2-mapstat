// Self-update from GitHub Releases (only compiled in when UPDATE_REPO is defined - Release builds with
// GITHUB_REPO set in .env). The latest release must be tagged like "v1.2.3" and carry the exe as an asset.
#pragma once
#include <cstdint>
#include <string>

struct ReleaseInfo {
    std::string tag;        // "v1.2.3"
    std::string body;       // release notes (markdown)
    std::string pageUrl;    // html_url of the release
    std::string assetUrl;   // browser_download_url of the .exe asset
    std::string digest;     // "sha256:..." when GitHub provides it, else ""
    uint64_t size = 0;
};

// GET https://api.github.com/repos/<repo>/releases/latest. false + err on failure.
bool fetchLatestRelease(const std::string& repo, ReleaseInfo& out, std::string& err);
// Downloads the asset into memory and checks it (exe header, size, sha256 digest if given).
bool downloadAsset(const ReleaseInfo& r, std::string& data, std::string& err);
// "v1.10.0" > "v1.9.3"? (numbers compared part by part, leading 'v' ignored)
bool isNewer(const std::string& tag, const std::string& current);
