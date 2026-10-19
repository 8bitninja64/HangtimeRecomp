// Update check: asks GitHub for the project's latest published release and compares its tag with
// this build's version. Read-only and anonymous: one HTTPS GET to api.github.com (no account, no
// personal data; the request carries only a User-Agent with the version). Nothing is downloaded or
// installed: an available update is shown in the launcher, which opens the release page in the
// browser. The launcher runs it in the background when it opens (setting update_check=0 turns
// that off; "Check now" still works).
#pragma once
#include <string>

struct UpdateStatus {
    enum State { Idle, Checking, UpToDate, Available, NoReleases, Failed } state = Idle;
    std::string latest;   // the release's tag, e.g. "v0.2.0"
    std::string error;    // Failed: what went wrong, for the player
};

// The repository ("owner/name") whose releases are checked: HANGTIME_UPDATE_REPO (branding.hpp)
// unless the setting update_repo= names another (tests).
std::string update_repo(const std::string& setting);
// The page the launcher opens for an available update (the latest release).
std::string update_release_page(const std::string& repo);
// Starts a background check (ignored while one runs) / the current state.
void update_check_start(const std::string& repo);
UpdateStatus update_check_status();
// The check itself, synchronous (--check-update and the background thread).
UpdateStatus update_check_now(const std::string& repo);
// True when release tag `latest` ("v1.2.3", "1.2", "0.2.0-beta") is a newer version than `current`.
bool update_version_newer(const std::string& latest, const std::string& current);
