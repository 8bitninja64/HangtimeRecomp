# Contributing to NBA Hangtime Recompiled

Thanks for helping. Bug reports, hardware test results and pull requests are all welcome.

## Ground rules

- **No game data, ever.** Do not attach, commit, upload or link ROMs, generated code
  (`full_generated/`, `hangtime_game.*`), memory dumps, audio captures or screenshots of the game
  in issues, pull requests or discussions. `check_public.py` runs on every pull request and
  rejects ROM data, generated code and binaries, but it cannot catch everything.
- **Run the original code.** Fixes must keep the game's own logic: no faked game state, forced
  flags or synthetic screens. Runtime replacements of N64 hardware or libultra behaviour need
  evidence that they match the console.
- **Keep upstream projects unmodified** (`external/`, `vendor/`). Fix things in our code, or
  contribute the fix upstream.
- **Generated files are build products.** Change the generators (`full_inventory.py`,
  `prepare_full.py`, `libultra_map.py`), never their output.

## Reporting a bug

Use the issue templates. Include your OS, CPU, GPU and driver version, the release or commit,
how you start the game (launcher settings, command line), what happened and what you expected,
and the relevant part of `hangtime.log` (or the terminal output on Linux). Remove any personal
paths you do not want to share.

## Pull requests

1. Fork, create a branch, keep the change focused.
2. Build with `python build.py` and run `python check_public.py`.
3. If your change affects gameplay, audio, input or rendering, run the gameplay tests in
   [docs/TESTING.md](docs/TESTING.md) that cover it (they need your own ROM) and say which ones
   passed in the pull request. If you could not run a test, say so.
4. Describe what changed and why. Small, verified steps are easier to review than large ones.

By contributing you agree that your contribution is licensed under the project's license
(see [LICENSE](LICENSE)).

## AI-assisted contributions

AI tools are used heavily in this project and are welcome in contributions, on the same terms
as hand-written code: you are responsible for what you submit, you must have read and
understood it, it must build and pass the tests, and the pull request must say which tests you
ran. Mention in the pull request that AI tools were used.

## Code of conduct

Participation is governed by [CODE_OF_CONDUCT.md](CODE_OF_CONDUCT.md).
