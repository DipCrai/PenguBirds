#include <sys/stat.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include "runtime/log.h"
#include "win32/process.h"

int main(int argc, char** argv) {
  std::string game_dir, exe, game_args;
  if (const char* e = std::getenv("ABSW2_GAME_DIR")) game_dir = e;
  if (const char* e = std::getenv("ABSW2_EXE")) exe = e;
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
    std::fprintf(stderr, "absw2: pass --game-dir and --exe (or set ABSW2_GAME_DIR and ABSW2_EXE)\n");
    return 2;
  }
  while (game_dir.size() > 1 && game_dir.back() == '/') game_dir.pop_back();
  if (const char* t = std::getenv("ABSW2_TRACE")) recomp::g_trace = *t && *t != '0';

  struct stat st;
  if (stat(exe.c_str(), &st) != 0) {
    std::fprintf(stderr, "absw2: %s not found\n", exe.c_str());
    return 2;
  }

  recomp::win32::GameConfig game;
  game.id = "absw2";
  game.guest_dir = "C:\\Program Files\\Rovio\\Angry Birds Star Wars II";
  game.exe_name = "AngryBirdsStarWarsII.exe";
  game.lifted_dlls = {{"grgl1.dll", 0x30000000}, {"grgl2.dll"}, {"msvcr100.dll"}, {"msvcp100.dll"}};
  return recomp::RunProcess(game, exe, game_dir, game_args);
}
