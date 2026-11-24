// abclassic: Angry Birds Classic (PC 4.0.0), natively.
//
//   abclassic [--game-dir DIR] [--exe FILE] [-- GAME ARGS...]
//
// DIR is the game install (config.lua, data/, the DLLs). Defaults to
// $ABCLASSIC_GAME_DIR. FILE is AngryBirds.exe; defaults to $ABCLASSIC_EXE.
// Set ABclassic_TRACE=1 to log every HLE call.

#include <sys/stat.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include "runtime/log.h"
#include "win32/process.h"

int main(int argc, char** argv) {
  std::string game_dir, exe, game_args;
  if (const char* e = std::getenv("ABCLASSIC_GAME_DIR")) game_dir = e;
  if (const char* e = std::getenv("ABCLASSIC_EXE")) exe = e;
  for (int i = 1; i < argc; i++) {
    if (!std::strcmp(argv[i], "--game-dir") && i + 1 < argc) {
      game_dir = argv[++i];
    } else if (!std::strcmp(argv[i], "--exe") && i + 1 < argc) {
      exe = argv[++i];
    } else if (!std::strcmp(argv[i], "--")) {
      for (i++; i < argc; i++) {
        if (!game_args.empty()) game_args += ' ';
        bool quote = std::strpbrk(argv[i], " \t");
        if (quote) game_args += '"';
        game_args += argv[i];
        if (quote) game_args += '"';
      }
    } else {
      std::fprintf(stderr, "usage: %s [--game-dir DIR] [--exe FILE] [-- GAME ARGS...]\n", argv[0]);
      return 2;
    }
  }
  if (game_dir.empty() || exe.empty()) {
    std::fprintf(stderr, "abclassic: pass --game-dir and --exe (or set ABclassic_GAME_DIR and ABclassic_EXE)\n");
    return 2;
  }
  while (game_dir.size() > 1 && game_dir.back() == '/') game_dir.pop_back();
  if (const char* t = std::getenv("ABCLASSIC_TRACE")) recomp::g_trace = *t && *t != '0';

  struct stat st;
  if (stat(exe.c_str(), &st) != 0) {
    std::fprintf(stderr, "abclassic: %s not found\n", exe.c_str());
    return 2;
  }

  recomp::win32::GameConfig game;
  game.id = "abclassic";
  game.guest_dir = "C:\\Program Files (x86)\\Rovio Entertainment Ltd\\Angry Birds";
  game.exe_name = "AngryBirds.exe";
  game.lifted_dlls = {{"grgl1.dll", 0x30000000}, {"msvcr100.dll"}, {"msvcp100.dll"}};
  return recomp::RunProcess(game, exe, game_dir, game_args);
}
