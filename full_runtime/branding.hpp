// The project's public-facing name, in one place (owner's choice 2026-10-19: the codename
// "HangtimeRecomp"; PUBLIC_RELEASE.md "Name and branding"). The game's own title is used elsewhere
// only to identify the ROM the program needs ("NBA Hangtime (USA)"), never as the project's branding.
#pragma once

#define HANGTIME_PRODUCT_NAME "HangtimeRecomp"                     // windows, dialogs, About, logs
// Launcher title art: a small line, a large line and a subtitle.
#define HANGTIME_TITLE_SMALL "UNOFFICIAL PC PORT"
#define HANGTIME_TITLE_LARGE "HANGTIME RECOMP"
#define HANGTIME_TITLE_SUB "STATIC RECOMPILATION  \xC2\xB7  BRING YOUR OWN ROM"
// The GitHub repository ("owner/name") whose published releases the launcher's update check reads
// (update_check.hpp). While it is private or has no release, the check reports "no releases".
#define HANGTIME_UPDATE_REPO "8bitninja64/NBAHangtimeRecompiled"
