// cod1: Call of Duty (single player), natively.
//
//   cod1 [--game-dir DIR] [--exe FILE] [-- GAME ARGS...]
//
// DIR is the game install (the folder with Main/*.pk3). Defaults to
// $COD1_GAME_DIR. FILE is the unpacked CoDSP.exe (tools/unpack_steam.py);
// defaults to $COD1_EXE. Arguments after `--` go on the game's command
// line (Quake-style, e.g. +set r_mode -1). Set COD1_TRACE=1 to log every
// HLE call.

#include <sys/stat.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include "runtime/log.h"
#include "runtime/runtime.h"

int main(int argc, char** argv) {
  std::string game_dir, exe, game_args;
  if (const char* e = std::getenv("COD1_GAME_DIR")) game_dir = e;
  if (const char* e = std::getenv("COD1_EXE")) exe = e;
  for (int i = 1; i < argc; i++) {
    if (!std::strcmp(argv[i], "--game-dir") && i + 1 < argc) {
      game_dir = argv[++i];
    } else if (!std::strcmp(argv[i], "--exe") && i + 1 < argc) {
      exe = argv[++i];
    } else if (!std::strcmp(argv[i], "--")) {
      for (i++; i < argc; i++) {
        if (!game_args.empty()) game_args += ' ';
        game_args += argv[i];
      }
    } else {
      std::fprintf(stderr,
                   "usage: %s [--game-dir DIR] [--exe FILE] [-- GAME ARGS...]\n",
                   argv[0]);
      return 2;
    }
  }
  if (game_dir.empty() || exe.empty()) {
    std::fprintf(stderr, "cod1: pass --game-dir and --exe (or set "
                         "COD1_GAME_DIR and COD1_EXE)\n");
    return 2;
  }
  while (game_dir.size() > 1 && game_dir.back() == '/') game_dir.pop_back();
  if (const char* t = std::getenv("COD1_TRACE")) recomp::g_trace = *t && *t != '0';

  struct stat st;
  for (const std::string& f : {exe, game_dir + "/Main/pak0.pk3"})
    if (stat(f.c_str(), &st) != 0) {
      std::fprintf(stderr, "cod1: %s not found\n", f.c_str());
      return 2;
    }

  recomp::GameConfig game;
  game.id = "cod1";
  game.guest_dir = "C:\\Program Files\\Call of Duty";
  game.exe_name = "CoDSP.exe";
  // What the Steam install script writes (instscript.vdf).
  const std::string key = "HKLM\\SOFTWARE\\Activision\\Call of Duty";
  game.registry = {{key, "InstallPath", game.guest_dir},
                   {key, "Version", "1.5b"},
                   {key, "Language", "1"}};
  // The SP game modules, loaded by the engine with LoadLibrary and lifted
  // with the EXE (see CLAUDE.md, Lift).
  game.lifted_dlls = {{"gamex86.dll"}, {"cgamex86.dll"}, {"uix86.dll"}};
  // The top row of configure.csv (autoconfigure): full texture detail
  // and 44 kHz sound.
  game.video_memory_mb = 256;
  return recomp::RunProcess(game, exe, game_dir, game_args);
}
