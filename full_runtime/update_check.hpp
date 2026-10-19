// Updates from the project's GitHub releases (as AkiLauncher does for its ports).
// Check: one anonymous HTTPS GET to api.github.com for the latest published release; its tag is
// compared with this build's version. The launcher runs it in the background when it opens
// (setting update_check=0 turns that off; "Check now" still works).
// Install (only when the player chooses it): downloads this system's package from that release
// (HangtimeRecomp-win64-*.zip / HangtimeRecomp-linux-x86_64-*.tar.gz, never a -PERSONAL one),
// checks its SHA-256 against the digest GitHub publishes for the asset (or the release's
// SHA256SUMS.txt; no checksum, no install), unpacks it with the system's tar, and replaces the
// program files in place. Kept: hangtime.ini, saves/, the log, the ROM (never in the folder's
// packages). When the new package's first-start build kit differs from the installed one, the game
// module built from the ROM is retired, so the new version rebuilds it from the player's ROM.
// Replaced files are renamed to *.old-update first (a running program's files can be renamed on
// both systems), restored if anything fails, and removed at the next start. Then the program
// restarts into the launcher.
#pragma once
#include <cstdint>
#include <filesystem>
#include <string>

struct UpdateAsset { std::string name, url, sha256; uint64_t size = 0; };

struct UpdateStatus {
    enum State { Idle, Checking, UpToDate, Available, NoReleases, Failed } state = Idle;
    std::string latest;     // the release's tag, e.g. "v0.2.0"
    std::string error;      // Failed: what went wrong, for the player
    std::string page;       // the release's web page
    std::string notes;      // the release notes (Markdown as written)
    UpdateAsset asset;      // this system's package (name empty: the release has none)
    std::string sums_url;   // the release's SHA256SUMS.txt, if any
};

// The repository ("owner/name") whose releases are checked: HANGTIME_UPDATE_REPO (branding.hpp)
// unless the setting update_repo= names another (tests).
std::string update_repo(const std::string& setting);
// The page the launcher opens for a release that cannot be installed here.
std::string update_release_page(const std::string& repo);
// Starts a background check (ignored while one runs) / the current state.
void update_check_start(const std::string& repo);
UpdateStatus update_check_status();
// The check itself, synchronous (--check-update and the background thread).
UpdateStatus update_check_now(const std::string& repo);
// True when release tag `latest` ("v1.2.3", "1.2", "0.2.0-beta") is a newer version than `current`.
bool update_version_newer(const std::string& latest, const std::string& current);

struct UpdateInstallStatus {
    enum State { Idle, Downloading, Verifying, Extracting, Installing, Done, Failed } state = Idle;
    float fraction = 0;     // Downloading: 0..1
    std::string error;
};
// Whether `release` can be installed in place here; `why` says why not (then the launcher offers
// the release page instead).
bool update_can_install(const UpdateStatus& release, std::string& why);
// Starts installing `release` on a background thread / the current state.
void update_install_start(const std::string& repo, const UpdateStatus& release);
UpdateInstallStatus update_install_status();
// The installation itself, synchronous (--install-update and the background thread).
UpdateInstallStatus update_install_now(const std::string& repo, const UpdateStatus& release);
// After a finished installation: starts the new executable with --launcher (the caller then exits).
bool update_restart();
// At startup: removes the files the last installation replaced (those still in use stay listed).
void update_cleanup();
