// pengubirds: native Angry Birds installer (detection, extraction, lift,
// build, desktop entry and safe removal).
#include <spawn.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <termios.h>
#include <unistd.h>

extern char** environ;

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

#include "extractor.h"
#include "icon.h"

namespace fs = std::filesystem;

namespace {

struct Part {
  std::string id;       // abclassic
  std::string name;     // Angry Birds
  std::string version;  // 4.0.0
  std::string exe;      // AngryBirds.exe
  std::string key;      // substring in the installer .ini name
  // Build-kit names (Phase 2).
  std::string target;       // abclassic
  std::string generated;    // generated_classic
  std::string symbols;      // symbols/classic
  std::string manifest;     // manifest/AngryBirds.manifest
  std::string grgl_symbols; // symbols/classic_grgl1
  std::string grgl_manifest;// manifest/classic_grgl1.manifest
  std::string grgl2_symbols;
  std::string grgl2_manifest;
};

const Part kParts[] = {
    {"abclassic", "Angry Birds", "4.0.0", "AngryBirds.exe", "angrybirdsclassic",
     "abclassic", "generated_classic", "classic", "AngryBirds.manifest",
     "classic_grgl1", "classic_grgl1.manifest", "", ""},
    {"abseasons", "Angry Birds Seasons", "4.1.0", "AngryBirdsSeasons.exe", "angrybirdsseasons",
     "abseasons", "generated_seasons", "seasons", "Seasons.manifest",
     "seasons_grgl1", "seasons_grgl1.manifest", "", ""},
    {"abrio", "Angry Birds Rio", "2.2.0", "AngryBirdsRio.exe", "angrybirdsrio",
     "abrio", "generated_rio", "rio", "Rio.manifest",
     "rio_grgl1", "rio_grgl1.manifest", "", ""},
    {"abspace", "Angry Birds Space", "2.0.0", "AngryBirdsSpace.exe", "angrybirdsspace",
     "abspace", "generated_space", "space", "Space.manifest",
     "space_grgl1", "space_grgl1.manifest", "", ""},
    {"absw1", "Angry Birds Star Wars", "1.5.0", "AngryBirdsStarWars.exe", "starwars",
     "absw1", "generated", "sw1", "AngryBirdsStarWars.manifest",
     "grgl1", "grgl1.manifest", "", ""},
    {"absw2", "Angry Birds Star Wars II", "1.5.1", "AngryBirdsStarWarsII.exe", "starwarsii",
     "absw2", "generated_sw2", "sw2", "AngryBirdsStarWarsII.manifest",
     "sw2_grgl1", "sw2_grgl1.manifest",
     "sw2_grgl2", "sw2_grgl2.manifest"},
};

struct Item {
  const Part* part = nullptr;
  fs::path installer;
  std::string installer_version;
  bool installed = false;
  bool selected = false;
  std::string status;  // installed, ready, unsupported build, not found
};

std::string Home() {
  const char* h = std::getenv("HOME");
  return h && *h ? h : "/tmp";
}

fs::path InstallRoot() { return fs::path(Home()) / ".local/share/pengubirds/games"; }
fs::path DesktopDir() { return fs::path(Home()) / ".local/share/applications"; }
fs::path LauncherDir() { return fs::path(Home()) / ".local/bin"; }
fs::path SavesDir(const Part& p) { return fs::path(Home()) / ".local/share" / p.id; }

std::string ToolDir() {
  std::error_code ec;
  fs::path self = fs::read_symlink("/proc/self/exe", ec);
  if (ec) return fs::current_path().string();
  return self.parent_path().string();
}

std::string Lower(std::string s) {
  for (char& c : s) c = char(std::tolower(static_cast<unsigned char>(c)));
  return s;
}

bool EndsWith(const std::string& s, const std::string& suffix) {
  return s.size() >= suffix.size() &&
         s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0;
}

std::string Trim(std::string s) {
  while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front()))) s.erase(s.begin());
  while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) s.pop_back();
  return s;
}

// A version-looking underscore-separated segment: 4.0.0, 2.2.0, ...
std::string VersionFromName(const std::string& name) {
  std::string cur;
  for (size_t i = 0; i <= name.size(); i++) {
    const char c = i < name.size() ? name[i] : '_';
    if (c == '_' || c == '\\' || c == '.') {
      if (c == '_' || c == '\\') {
        int dots = int(std::count(cur.begin(), cur.end(), '.'));
        bool ok = !cur.empty() && dots >= 1;
        for (char ch : cur)
          if (!std::isdigit(static_cast<unsigned char>(ch)) && ch != '.') ok = false;
        if (ok) return cur;
        cur.clear();
      } else {
        cur += c;
      }
    } else {
      cur += c;
    }
  }
  return {};
}

std::string ReadIniValue(const fs::path& ini, const std::string& key) {
  std::ifstream in(ini);
  std::string line;
  while (std::getline(in, line)) {
    line = Trim(line);
    if (line.size() > key.size() && line.compare(0, key.size(), key) == 0 &&
        line[key.size()] == '=') {
      return Trim(line.substr(key.size() + 1));
    }
  }
  return {};
}

std::optional<Part> PartForInstaller(const pengubirds::AdvancedInstaller& ai) {
  std::string hint;
  for (const auto& e : ai.entries()) {
    if (EndsWith(Lower(e.name), ".ini")) {
      hint = Lower(e.name);
      break;
    }
  }
  if (hint.empty()) {
    hint = Lower(ai.path().filename().string());
  }
  // Star Wars II must be checked before Star Wars.
  if (hint.find("starwarsii") != std::string::npos ||
      hint.find("starwars2") != std::string::npos)
    return kParts[5];
  if (hint.find("starwars") != std::string::npos) return kParts[4];
  if (hint.find("seasons") != std::string::npos) return kParts[1];
  if (hint.find("rio") != std::string::npos) return kParts[2];
  if (hint.find("space") != std::string::npos) return kParts[3];
  if (hint.find("classic") != std::string::npos ||
      hint.find("angrybirds") != std::string::npos)
    return kParts[0];
  return std::nullopt;
}

std::string InstallerVersion(const pengubirds::AdvancedInstaller& ai) {
  for (const auto& e : ai.entries()) {
    if (EndsWith(Lower(e.name), ".ini")) {
      std::string v = VersionFromName(e.name);
      if (!v.empty()) return v;
    }
  }
  return VersionFromName(ai.path().filename().string());
}

std::vector<Item> Detect(const std::vector<std::string>& scan_dirs) {
  std::vector<Item> items;
  for (const auto& p : kParts) {
    Item it;
    it.part = &p;
    it.installed = fs::exists(InstallRoot() / p.id / "install.json");
    items.push_back(it);
  }
  for (const auto& dir : scan_dirs) {
    std::error_code ec;
    if (!fs::is_directory(dir, ec)) continue;
    for (auto d = fs::recursive_directory_iterator(
             dir, fs::directory_options::skip_permission_denied, ec);
         d != fs::recursive_directory_iterator(); d.increment(ec)) {
      if (ec) break;
      if (d.depth() > 1) { d.disable_recursion_pending(); continue; }
      if (!d->is_regular_file(ec)) continue;
      const std::string name = Lower(d->path().filename().string());
      if (!EndsWith(name, ".exe")) continue;
      pengubirds::AdvancedInstaller ai(d->path());
      std::string error;
      if (!ai.Open(&error)) continue;
      auto part = PartForInstaller(ai);
      if (!part) continue;
      for (auto& it : items) {
        if (it.part->id != part->id) continue;
        if (!it.installer.empty()) break;  // first one wins
        it.installer = d->path();
        it.installer_version = InstallerVersion(ai);
        break;
      }
    }
  }
  for (auto& it : items) {
    if (it.installed) {
      it.status = "installed";
    } else if (it.installer.empty()) {
      it.status = "not found";
    } else if (it.installer_version.empty() ||
               it.installer_version != it.part->version) {
      it.status = "unsupported build";
    } else {
      it.status = "ready";
    }
  }
  return items;
}

fs::path RepoRoot() {
  fs::path d = ToolDir();
  for (int i = 0; i < 6; i++) {
    std::error_code ec;
    if (fs::exists(d / "CMakeLists.txt", ec) && fs::exists(d / "manifest", ec))
      return d;
    const fs::path parent = d.parent_path();
    if (parent == d) break;
    d = parent;
  }
  return ToolDir();
}

std::string BinDir() {
  if (const char* e = std::getenv("PENGUBIRDS_BIN_DIR")) return e;
  std::error_code ec;
  fs::path p = fs::path(ToolDir()) / "payload/bin";
  if (fs::is_directory(p, ec)) return p.string();
  p = RepoRoot() / "out/build/release";
  return fs::weakly_canonical(p, ec).string();
}

fs::path RuntimeDir(const std::string& bin_dir) {
  std::error_code ec;
  fs::path p = fs::path(bin_dir) / "swarc";
  if (fs::is_directory(p, ec)) return p;
  p = fs::path(bin_dir) / "../lib";
  if (fs::is_directory(p, ec)) return fs::weakly_canonical(p, ec);
  return fs::path(bin_dir);
}

std::string ShellQuote(const std::string& s) {
  std::string out = "'";
  for (char c : s) {
    if (c == '\'') out += "'\\''";
    else out += c;
  }
  out += "'";
  return out;
}

std::string DesktopQuote(const std::string& s) {
  std::string out = "\"";
  for (char c : s) {
    if (c == '"' || c == '\\') out += '\\';
    out += c;
  }
  out += "\"";
  return out;
}

bool CopyExecutable(const fs::path& from, const fs::path& to, std::string* err) {
  std::error_code ec;
  fs::copy_file(from, to, fs::copy_options::overwrite_existing, ec);
  if (ec) {
    *err = "cannot copy " + from.string() + ": " + ec.message();
    return false;
  }
  fs::permissions(to, fs::perms::owner_all, fs::perm_options::add, ec);
  return true;
}

bool WriteFile(const fs::path& p, const std::string& text, std::string* err) {
  std::ofstream out(p, std::ios::binary | std::ios::trunc);
  if (!out) {
    *err = "cannot write " + p.string();
    return false;
  }
  out << text;
  if (!out) {
    *err = "write failed for " + p.string();
    return false;
  }
  return true;
}

std::string JsonEscape(const std::string& s) {
  std::string out;
  for (char c : s) {
    if (c == '"' || c == '\\') { out += '\\'; out += c; }
    else if (c == '\n') out += "\\n";
    else out += c;
  }
  return out;
}

fs::path KitRoot() {
  const fs::path k = ToolDir();
  std::error_code ec;
  if (fs::is_regular_file(k / "payload/bin/recomp_lift", ec)) return k;
  return {};
}

bool KitMode() { return !KitRoot().empty(); }

std::string CmakeQuote(const std::string& s) {
  std::string out = "\"";
  for (char c : s) {
    if (c == '"' || c == '\\') out += '\\';
    out += c;
  }
  out += '"';
  return out;
}

std::string GameMacro(const std::string& id) {
  if (id == "abclassic") return "AB_GAME_CLASSIC";
  if (id == "abseasons") return "AB_GAME_SEASONS";
  if (id == "abrio") return "AB_GAME_RIO";
  if (id == "abspace") return "AB_GAME_SPACE";
  if (id == "absw2") return "AB_GAME_SW2";
  return "AB_GAME_SW1";
}

std::string MainSource(const std::string& id) {
  if (id == "abclassic") return "main_classic.cpp";
  if (id == "abseasons") return "main_seasons.cpp";
  if (id == "abrio") return "main_rio.cpp";
  if (id == "abspace") return "main_space.cpp";
  if (id == "absw2") return "main_sw2.cpp";
  return "main.cpp";
}

bool RunTool(const std::vector<std::string>& args, std::string* err) {
  if (args.empty()) { *err = "empty command"; return false; }
  std::vector<char*> argv;
  argv.reserve(args.size() + 1);
  for (const auto& a : args) argv.push_back(const_cast<char*>(a.c_str()));
  argv.push_back(nullptr);
  pid_t pid = 0;
  const int rc = posix_spawnp(&pid, argv[0], nullptr, nullptr, argv.data(), environ);
  if (rc != 0) {
    *err = "cannot run " + args[0] + ": " + std::strerror(rc);
    return false;
  }
  int status = 0;
  while (waitpid(pid, &status, 0) < 0 && errno == EINTR) {
  }
  if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
    *err = args[0] + " failed" +
           (WIFEXITED(status) ? " (exit " + std::to_string(WEXITSTATUS(status)) + ")"
                              : std::string(" (signal)"));
    return false;
  }
  return true;
}

// Phase 3: standalone payload. Uses payload/{bin,lib,include,src,symbols,
// manifests} and a generated CMake project; no repository checkout needed.
bool BuildNativeKit(const Part& p, const fs::path& game,
                    const fs::path& out_binary, std::string* err) {
  const fs::path kit = KitRoot();
  const fs::path payload = kit / "payload";
  const fs::path work =
      fs::temp_directory_path() / ("pengubirds-build-" + std::to_string(getpid()));
  std::error_code ec;
  fs::remove_all(work, ec);
  fs::create_directories(work / "generated", ec);
  if (ec) { *err = "cannot create " + work.string(); return false; }

  const fs::path lifter = payload / "bin/recomp_lift";
  std::vector<std::string> lift = {
      lifter.string(),
      "--exe", (game / p.exe).string(),
      "--symbols", (payload / "symbols" / p.symbols).string(),
      "--manifest", (payload / "manifests" / p.manifest).string(),
      "--module", (game / "msvcr100.dll").string(),
      "--symbols", (payload / "symbols/msvcr100").string(),
      "--manifest", (payload / "manifests/msvcr100.manifest").string(),
      "--module", (game / "msvcp100.dll").string(),
      "--symbols", (payload / "symbols/msvcp100").string(),
      "--manifest", (payload / "manifests/msvcp100.manifest").string(),
      "--module", (game / "grgl1.dll").string(),
      "--symbols", (payload / "symbols" / p.grgl_symbols).string(),
      "--manifest", (payload / "manifests" / p.grgl_manifest).string(),
      "--base", "0x30000000",
      "--out", (work / "generated").string(),
  };
  if (!p.grgl2_symbols.empty() && fs::exists(game / "grgl2.dll")) {
    lift.push_back("--module");
    lift.push_back((game / "grgl2.dll").string());
    lift.push_back("--symbols");
    lift.push_back((payload / "symbols" / p.grgl2_symbols).string());
    lift.push_back("--manifest");
    lift.push_back((payload / "manifests" / p.grgl2_manifest).string());
  }
  std::cout << "Lifting " << p.name << " (standalone build) ...\n" << std::flush;
  if (!RunTool(lift, err)) return false;

  std::string extra = (p.id == "abrio") ? "\n  src/rio_key.cpp" : "";
  std::ostringstream cm;
  cm << "cmake_minimum_required(VERSION 3.25)\n"
     << "project(abgame CXX)\n"
     << "set(CMAKE_CXX_STANDARD 20)\n"
     << "set(CMAKE_CXX_STANDARD_REQUIRED ON)\n"
     << "file(GLOB GEN "
     << CmakeQuote((work / "generated").string() + "/*.cpp") << ")\n"
     << "add_executable(abgame ${GEN}\n"
     << "  " << CmakeQuote((payload / "src" / MainSource(p.id)).string()) << "\n"
     << "  " << CmakeQuote((payload / "src/hle_ext.cpp").string()) << "\n"
     << "  " << CmakeQuote((payload / "src/mpg123_hle.cpp").string()) << "\n"
     << "  " << CmakeQuote((payload / "src/seh_native.cpp").string()) << extra << ")\n"
     << "target_include_directories(abgame PRIVATE "
     << CmakeQuote((payload / "include").string()) << ")\n"
     << "target_compile_definitions(abgame PRIVATE " << GameMacro(p.id) << ")\n"
     << "target_link_directories(abgame PRIVATE "
     << CmakeQuote((payload / "lib").string()) << ")\n"
     << "target_link_libraries(abgame PRIVATE recomp_win32 recomp_opengl "
        "recomp_dsound recomp_xinput recomp_base recomp_x86 recomp_x87 "
        "recomp_pe recomp_gfx recomp_hlsl recomp_audio recomp_log mpg123 "
        "SDL3 avformat avcodec swscale avutil)\n"
     << "target_link_options(abgame PRIVATE \"LINKER:--no-as-needed\")\n"
     << "set_target_properties(abgame PROPERTIES ENABLE_EXPORTS ON "
        "BUILD_WITH_INSTALL_RPATH TRUE INSTALL_RPATH \"$ORIGIN/lib\")\n";
  if (!WriteFile(work / "CMakeLists.txt", cm.str(), err)) return false;

  std::cout << "Compiling " << p.name << " (standalone build) ...\n" << std::flush;
  if (!RunTool({"cmake", "-S", work.string(), "-B", (work / "build").string(),
                "-G", "Ninja", "-DCMAKE_BUILD_TYPE=Release"}, err))
    return false;
  if (!RunTool({"cmake", "--build", (work / "build").string(), "-j"}, err))
    return false;
  const fs::path built = work / "build/abgame";
  if (!fs::is_regular_file(built)) {
    *err = "standalone build produced no binary in " + work.string();
    return false;
  }
  fs::copy_file(built, out_binary, fs::copy_options::overwrite_existing, ec);
  if (ec) { *err = "cannot copy built binary: " + ec.message(); return false; }
  // Runtime libraries live next to the installed binary.
  fs::copy(payload / "lib", out_binary.parent_path() / "lib",
           fs::copy_options::recursive | fs::copy_options::overwrite_existing, ec);
  if (ec) { *err = "cannot copy runtime libraries: " + ec.message(); return false; }
  fs::permissions(out_binary, fs::perms::owner_all, fs::perm_options::add, ec);
  fs::remove_all(work, ec);
  return true;
}

// Phase 2: lift the extracted game and compile it with the repo build kit.
bool BuildNative(const Part& p, const fs::path& game, const fs::path& out_binary,
                 std::string* err) {
  if (KitMode()) return BuildNativeKit(p, game, out_binary, err);
  const fs::path repo = RepoRoot();
  const fs::path lifter =
      repo / "out/build/release/swarc/tools/lifter_x86/recomp_lift";
  const fs::path build_dir = repo / "out/build/release";
  if (!fs::is_regular_file(lifter)) {
    *err = "build kit not found: " + lifter.string();
    return false;
  }
  const fs::path sym = repo / "symbols" / p.symbols;
  const fs::path man = repo / "manifest" / p.manifest;
  const fs::path grgl_sym = repo / "symbols" / p.grgl_symbols;
  const fs::path grgl_man = repo / "manifest" / p.grgl_manifest;
  for (const auto& path : {game / p.exe, sym / "functions.tsv", man,
                           game / "msvcr100.dll", game / "msvcp100.dll",
                           game / "grgl1.dll", grgl_sym / "functions.tsv", grgl_man}) {
    if (!fs::exists(path)) {
      *err = "missing build input: " + path.string();
      return false;
    }
  }

  std::vector<std::string> lift = {
      lifter.string(),
      "--exe", (game / p.exe).string(),
      "--symbols", sym.string(),
      "--manifest", man.string(),
      "--module", (game / "msvcr100.dll").string(),
      "--symbols", (repo / "symbols/msvcr100").string(),
      "--manifest", (repo / "manifest/msvcr100.manifest").string(),
      "--module", (game / "msvcp100.dll").string(),
      "--symbols", (repo / "symbols/msvcp100").string(),
      "--manifest", (repo / "manifest/msvcp100.manifest").string(),
      "--module", (game / "grgl1.dll").string(),
      "--symbols", grgl_sym.string(),
      "--manifest", grgl_man.string(),
      "--base", "0x30000000",
      "--out", (repo / p.generated).string(),
  };
  if (!p.grgl2_symbols.empty() && fs::exists(game / "grgl2.dll")) {
    lift.push_back("--module");
    lift.push_back((game / "grgl2.dll").string());
    lift.push_back("--symbols");
    lift.push_back((repo / "symbols" / p.grgl2_symbols).string());
    lift.push_back("--manifest");
    lift.push_back((repo / "manifest" / p.grgl2_manifest).string());
  }
  std::cout << "Lifting " << p.name << " ...\n" << std::flush;
  if (!RunTool(lift, err)) return false;
  std::cout << "Compiling " << p.name << " ...\n" << std::flush;
  if (!RunTool({"ninja", "-C", build_dir.string(), p.target}, err)) return false;
  const fs::path built = build_dir / p.target;
  if (!fs::is_regular_file(built)) {
    *err = "build produced no binary: " + built.string();
    return false;
  }
  std::error_code ec;
  fs::copy_file(built, out_binary, fs::copy_options::overwrite_existing, ec);
  if (ec) {
    *err = "cannot copy built binary: " + ec.message();
    return false;
  }
  fs::permissions(out_binary, fs::perms::owner_all, fs::perm_options::add, ec);
  return true;
}

bool Install(const Item& it, std::string* err) {
  const Part& p = *it.part;
  if (it.installed) {
    *err = p.name + " is already installed; remove it first";
    return false;
  }
  if (it.installer.empty()) {
    *err = "installer for " + p.name + " not found";
    return false;
  }
  const std::string bin_dir = BinDir();
  const char* prebuilt = std::getenv("PENGUBIRDS_PREBUILT");
  const fs::path binary_src = fs::path(bin_dir) / p.id;
  std::error_code ec;
  if (prebuilt && *prebuilt && !fs::is_regular_file(binary_src, ec)) {
    *err = "prebuilt native binary not found: " + binary_src.string();
    return false;
  }

  const fs::path final_dir = InstallRoot() / p.id;
  const fs::path temp_dir = InstallRoot() / (p.id + ".tmp-" + std::to_string(getpid()));
  fs::remove_all(temp_dir, ec);
  fs::create_directories(temp_dir, ec);
  if (ec) { *err = "cannot create " + temp_dir.string(); return false; }

  auto cleanup = [&] { std::error_code e; fs::remove_all(temp_dir, e); };

  pengubirds::AdvancedInstaller ai(it.installer);
  if (!ai.Open(err)) { cleanup(); return false; }
  std::cout << "Extracting " << it.installer << " ...\n" << std::flush;
  if (!ai.ExtractGame(temp_dir / "game", err)) { cleanup(); return false; }

  const fs::path game = temp_dir / "game";
  if (!fs::is_regular_file(game / p.exe, ec)) {
    *err = "extracted game does not contain " + p.exe;
    cleanup();
    return false;
  }
  const std::string appver = ReadIniValue(game / "updater.ini", "ApplicationVersion");
  if (!appver.empty() && appver != p.version) {
    *err = "installer contains version " + appver + ", expected " + p.version;
    cleanup();
    return false;
  }

  if (prebuilt && *prebuilt) {
    if (!CopyExecutable(binary_src, temp_dir / p.id, err)) { cleanup(); return false; }
  } else {
    if (!BuildNative(p, game, temp_dir / p.id, err)) { cleanup(); return false; }
  }

  std::string icon_error;
  const bool has_icon = pengubirds::ExtractIcon(it.installer, temp_dir / "icon.ico",
                                               &icon_error);
  if (!has_icon)
    std::cout << "Icon extraction skipped: " << icon_error << "\n";
  const bool has_png = has_icon && fs::exists(temp_dir / "icon.png");
  const std::string icon_name = has_png ? "icon.png" : "icon.ico";

  const fs::path runtime = KitMode() ? final_dir / "lib" : RuntimeDir(bin_dir);
  const fs::path launcher = temp_dir / "run.sh";
  std::ostringstream sh;
  sh << "#!/bin/sh\n";
  sh << "export LD_LIBRARY_PATH=" << ShellQuote(runtime.string())
     << "${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}\n";
  sh << "exec " << ShellQuote((final_dir / p.id).string())
     << " --game-dir " << ShellQuote((final_dir / "game").string())
     << " --exe " << ShellQuote((final_dir / "game" / p.exe).string())
     << " \"$@\"\n";
  if (!WriteFile(launcher, sh.str(), err)) { cleanup(); return false; }
  fs::permissions(launcher, fs::perms::owner_all, fs::perm_options::add, ec);

  std::ostringstream manifest;
  manifest << "{\n";
  manifest << "  \"part\": \"" << p.id << "\",\n";
  manifest << "  \"name\": \"" << JsonEscape(p.name) << "\",\n";
  manifest << "  \"version\": \"" << p.version << "\",\n";
  manifest << "  \"installer\": \"" << JsonEscape(it.installer.string()) << "\",\n";
  manifest << "  \"game_dir\": \"" << JsonEscape((final_dir / "game").string()) << "\",\n";
  manifest << "  \"binary\": \"" << JsonEscape((final_dir / p.id).string()) << "\",\n";
  manifest << "  \"launcher\": \"" << JsonEscape((final_dir / "run.sh").string()) << "\",\n";
  manifest << "  \"icon\": \"" << JsonEscape(has_icon ? (final_dir / icon_name).string() : "") << "\",\n";
  manifest << "  \"runtime\": \"" << JsonEscape(runtime.string()) << "\"\n";
  manifest << "}\n";
  if (!WriteFile(temp_dir / "install.json", manifest.str(), err)) { cleanup(); return false; }

  // The temporary tree is complete; move it into place.
  fs::create_directories(InstallRoot(), ec);
  fs::remove_all(final_dir, ec);
  fs::rename(temp_dir, final_dir, ec);
  if (ec) {
    *err = "cannot move installation into place: " + ec.message();
    cleanup();
    return false;
  }

  fs::create_directories(DesktopDir(), ec);
  fs::create_directories(LauncherDir(), ec);
  std::ostringstream desktop;
  desktop << "[Desktop Entry]\n"
          << "Type=Application\n"
          << "Name=" << p.name << "\n"
          << "Comment=" << p.name << " (PenguBirds)\n"
          << "Exec=" << DesktopQuote((final_dir / "run.sh").string()) << " %f\n"
          << "Path=" << (final_dir / "game").string() << "\n"
          << "Icon=" << (has_icon ? (final_dir / icon_name).string()
                                   : std::string("applications-games")) << "\n"
          << "Terminal=false\n"
          << "Categories=Game;\n";
  if (!WriteFile(DesktopDir() / ("pengubirds-" + p.id + ".desktop"), desktop.str(),
                 err))
    return false;
  std::error_code e2;
  fs::create_symlink(final_dir / "run.sh", LauncherDir() / ("pengubirds-" + p.id),
                     e2);  // best effort

  std::cout << "Installed " << p.name << " " << p.version << " to "
            << final_dir << "\n";
  return true;
}

bool Remove(const Item& it, bool remove_saves, std::string* err) {
  const Part& p = *it.part;
  const fs::path dir = InstallRoot() / p.id;
  if (!fs::exists(dir / "install.json")) {
    *err = p.name + " is not installed";
    return false;
  }
  std::error_code ec;
  fs::remove_all(dir, ec);
  if (ec) { *err = "cannot remove " + dir.string() + ": " + ec.message(); return false; }
  fs::remove(DesktopDir() / ("pengubirds-" + p.id + ".desktop"), ec);
  fs::remove(LauncherDir() / ("pengubirds-" + p.id), ec);
  if (remove_saves) {
    fs::remove_all(SavesDir(p), ec);
    if (ec) { *err = "cannot remove saves " + SavesDir(p).string(); return false; }
  }
  return true;
}

void PrintTable(const std::vector<Item>& items, int cursor, bool tui) {
  if (tui) std::cout << "\033[H\033[2J";
  std::cout << "PenguBirds 1.0 — Angry Birds installer\n\n";
  std::cout << "  " << std::left << std::setw(3) << "Sel" << "  "
            << std::setw(28) << "Part" << "  " << std::setw(8) << "Version"
            << "  " << "Status\n";
  for (size_t i = 0; i < items.size(); i++) {
    const Item& it = items[i];
    std::string part = it.part->name;
    if (part.size() > 28) part = part.substr(0, 27) + "…";
    const char mark = it.installed ? 'x' : (it.selected ? '*' : ' ');
    std::cout << (tui && int(i) == cursor ? "> " : "  ") << "["
              << mark << "]  " << std::left
              << std::setw(28) << part << "  " << std::setw(8)
              << it.part->version << "  " << it.status << "\n";
  }
  if (tui) {
    std::cout << "\n  ↑/↓ move   space select   enter install/remove   q quit\n";
    std::cout << std::flush;
  }
}

struct Terminal {
  termios old{};
  bool raw = false;
  void Enter() {
    if (!isatty(STDIN_FILENO)) return;
    tcgetattr(STDIN_FILENO, &old);
    termios t = old;
    t.c_lflag &= ~(ICANON | ECHO);
    t.c_cc[VMIN] = 1;
    t.c_cc[VTIME] = 0;
    tcsetattr(STDIN_FILENO, TCSANOW, &t);
    raw = true;
    std::cout << "\033[?1049h\033[?25l";
  }
  void Leave() {
    if (!raw) return;
    std::cout << "\033[?25h\033[?1049l" << std::flush;
    tcsetattr(STDIN_FILENO, TCSANOW, &old);
    raw = false;
  }
  ~Terminal() { Leave(); }
};

int ReadKey() {
  char c = 0;
  if (read(STDIN_FILENO, &c, 1) != 1) return -1;
  if (c == '\033') {
    char seq[2] = {};
    if (read(STDIN_FILENO, &seq[0], 1) != 1) return '\033';
    if (read(STDIN_FILENO, &seq[1], 1) != 1) return '\033';
    if (seq[0] == '[' && seq[1] == 'A') return 'U';
    if (seq[0] == '[' && seq[1] == 'B') return 'D';
    return '\033';
  }
  return static_cast<unsigned char>(c);
}

bool Ask(const std::string& question) {
  std::cout << question << " [y/N] " << std::flush;
  std::string line;
  std::getline(std::cin, line);
  line = Lower(Trim(line));
  return line == "y" || line == "yes";
}

int Tui(std::vector<Item>& items) {
  Terminal term;
  term.Enter();
  int cursor = 0;
  for (;;) {
    PrintTable(items, cursor, true);
    const int key = ReadKey();
    if (key == 'q' || key == 27) break;
    if (key == 'U' && cursor > 0) cursor--;
    if (key == 'D' && cursor + 1 < int(items.size())) cursor++;
    if (key == ' ' && !items[cursor].installed)
      items[cursor].selected = !items[cursor].selected;
    if (key == '\n' || key == '\r') {
      Item& it = items[cursor];
      term.Leave();
      std::string err;
      if (it.installed) {
        if (Ask("Remove \"" + it.part->name + "\" and all its saves?")) {
          if (Remove(it, true, &err)) std::cout << "Removed.\n";
          else std::cout << "Error: " << err << "\n";
        }
      } else {
        std::vector<Item*> targets;
        for (auto& x : items)
          if (x.selected && !x.installed) targets.push_back(&x);
        if (targets.empty() && it.status == "ready") targets.push_back(&it);
        if (targets.empty()) {
          std::cout << "Cannot install: " << it.status << "\n";
        } else {
          bool all_ready = true;
          for (Item* t : targets)
            if (t->status != "ready") all_ready = false;
          if (!all_ready) {
            std::cout << "Some selected parts are not ready; fix the selection.\n";
          } else if (Ask(targets.size() == 1
                             ? "Install \"" + targets[0]->part->name + "\"?"
                             : "Install " + std::to_string(targets.size()) +
                                   " selected parts?")) {
            for (Item* t : targets) {
              std::cout << "\n== " << t->part->name << " ==\n" << std::flush;
              if (Install(*t, &err)) {
                t->selected = false;
                std::cout << "Done.\n";
              } else {
                std::cout << "Error: " << err << "\n";
              }
            }
          }
        }
      }
      std::cout << "Press Enter to return..." << std::flush;
      std::string dummy;
      std::getline(std::cin, dummy);
      // Re-scan installed flags and statuses.
      for (auto& x : items) {
        x.installed = fs::exists(InstallRoot() / x.part->id / "install.json");
        if (x.installed) x.status = "installed";
        else if (x.installer.empty()) x.status = "not found";
        else if (x.installer_version.empty() ||
                 x.installer_version != x.part->version)
          x.status = "unsupported build";
        else x.status = "ready";
      }
      term.Enter();
    }
  }
  return 0;
}

void Usage(const char* argv0) {
  std::cout << "usage: " << argv0 << " [options]\n"
            << "  --scan DIR        scan DIR for installers (repeatable)\n"
            << "  --list            list parts and exit\n"
            << "  --install ID      install one part (ID like abclassic)\n"
            << "  --uninstall ID    remove one part\n"
            << "  --yes             do not ask for confirmation\n"
            << "  --remove-saves    also remove the game's saves on uninstall\n"
            << "  -h, --help        this help\n";
}

}  // namespace

int main(int argc, char** argv) {
  std::vector<std::string> scan_dirs;
  bool list = false;
  std::string install_id, uninstall_id;
  bool yes = false, remove_saves = false;
  for (int i = 1; i < argc; i++) {
    std::string a = argv[i];
    if (a == "--scan" && i + 1 < argc) scan_dirs.push_back(argv[++i]);
    else if (a == "--list") list = true;
    else if (a == "--install" && i + 1 < argc) install_id = argv[++i];
    else if (a == "--uninstall" && i + 1 < argc) uninstall_id = argv[++i];
    else if (a == "--yes") yes = true;
    else if (a == "--remove-saves") remove_saves = true;
    else if (a == "-h" || a == "--help") { Usage(argv[0]); return 0; }
    else { std::fprintf(stderr, "unknown option: %s\n", a.c_str()); return 2; }
  }
  if (scan_dirs.empty()) {
    const std::string cwd = fs::current_path().string();
    scan_dirs.push_back(cwd);
    if (cwd != ToolDir()) scan_dirs.push_back(ToolDir());
  }
  auto items = Detect(scan_dirs);

  if (list || !install_id.empty() || !uninstall_id.empty()) {
    if (list) { PrintTable(items, -1, false); return 0; }
    const std::string id = !install_id.empty() ? install_id : uninstall_id;
    auto it = std::find_if(items.begin(), items.end(),
                           [&](const Item& x) { return x.part->id == id; });
    if (it == items.end()) { std::fprintf(stderr, "unknown part: %s\n", id.c_str()); return 2; }
    std::string err;
    if (!install_id.empty()) {
      if (!yes && !Ask("Install \"" + it->part->name + "\"?")) return 1;
      if (!Install(*it, &err)) { std::fprintf(stderr, "error: %s\n", err.c_str()); return 1; }
      return 0;
    }
    if (!yes && !Ask("Remove \"" + it->part->name + "\" and all its saves?")) return 1;
    if (!Remove(*it, remove_saves || yes, &err)) {
      std::fprintf(stderr, "error: %s\n", err.c_str());
      return 1;
    }
    return 0;
  }

  return Tui(items);
}
