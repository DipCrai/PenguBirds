#include "extractor.h"

#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <cctype>
#include <cstring>
#include <fstream>
#include <vector>

extern char** environ;

namespace pengubirds {
namespace {

constexpr size_t kTail = 10000;       // search window at the end of the file
constexpr size_t kFooter = 0x46;      // ADVINSTSFX footer size
constexpr size_t kEntry = 24;         // file-table entry size
constexpr size_t kXor = 0x200;        // encrypted prefix of each file

uint32_t Le32(const unsigned char* p) {
  return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 |
         uint32_t(p[3]) << 24;
}

std::string Utf16ToUtf8(const std::vector<unsigned char>& bytes) {
  std::string out;
  for (size_t i = 0; i + 1 < bytes.size(); i += 2) {
    uint32_t c = uint32_t(bytes[i]) | uint32_t(bytes[i + 1]) << 8;
    if (c == 0) break;
    if (c < 0x80) {
      out += char(c);
    } else if (c < 0x800) {
      out += char(0xC0 | (c >> 6));
      out += char(0x80 | (c & 0x3F));
    } else {
      out += char(0xE0 | (c >> 12));
      out += char(0x80 | ((c >> 6) & 0x3F));
      out += char(0x80 | (c & 0x3F));
    }
  }
  return out;
}

std::string Lower(std::string s) {
  for (char& c : s) c = char(std::tolower(static_cast<unsigned char>(c)));
  return s;
}

bool ReadAt(std::ifstream& in, uint64_t offset, void* buf, size_t size) {
  in.clear();
  in.seekg(std::streamoff(offset));
  if (!in) return false;
  in.read(static_cast<char*>(buf), std::streamsize(size));
  return in.gcount() == std::streamsize(size);
}

}  // namespace

AdvancedInstaller::AdvancedInstaller(std::filesystem::path path)
    : path_(std::move(path)) {}

bool AdvancedInstaller::Open(std::string* error) {
  std::error_code ec;
  file_size_ = std::filesystem::file_size(path_, ec);
  if (ec) {
    *error = "cannot stat " + path_.string() + ": " + ec.message();
    return false;
  }
  if (file_size_ < kFooter + 6) {
    *error = "file is too small to be an Advanced Installer package";
    return false;
  }

  std::ifstream in(path_, std::ios::binary);
  if (!in) {
    *error = "cannot open " + path_.string();
    return false;
  }

  const size_t tail_len = size_t(std::min<uint64_t>(file_size_, kTail));
  std::string tail(tail_len, '\0');
  if (!ReadAt(in, file_size_ - tail_len, tail.data(), tail_len)) {
    *error = "cannot read the end of " + path_.string();
    return false;
  }
  const size_t magic = tail.rfind("ADVINSTSFX");
  if (magic == std::string::npos) {
    *error = "ADVINSTSFX not found (not an Advanced Installer package)";
    return false;
  }

  // The footer begins 0x3C bytes before the magic marker.
  const uint64_t from_end = uint64_t(tail_len - magic);
  const uint64_t footer_pos = from_end + 0x48 - 12;
  if (footer_pos > file_size_) {
    *error = "invalid ADVINSTSFX footer position";
    return false;
  }
  const uint64_t footer_start = file_size_ - footer_pos;
  unsigned char footer[kFooter] = {};
  if (!ReadAt(in, footer_start, footer, kFooter)) {
    *error = "cannot read the ADVINSTSFX footer";
    return false;
  }

  const uint32_t nfiles = Le32(footer + 4);
  const uint32_t info_off = Le32(footer + 16);
  if (nfiles > 100000) {
    *error = "invalid file count in ADVINSTSFX footer";
    return false;
  }
  if (info_off >= file_size_) {
    *error = "invalid file table offset in ADVINSTSFX footer";
    return false;
  }

  entries_.clear();
  entries_.reserve(nfiles);
  uint64_t p = info_off;
  for (uint32_t i = 0; i < nfiles; i++) {
    unsigned char info[kEntry] = {};
    if (!ReadAt(in, p, info, kEntry)) {
      *error = "truncated ADVINSTSFX file table";
      return false;
    }
    p += kEntry;
    const uint32_t flags = Le32(info + 8);
    const uint32_t size = Le32(info + 12);
    const uint32_t offset = Le32(info + 16);
    const uint32_t namesize = Le32(info + 20);
    if (namesize >= 0xffff || uint64_t(namesize) * 2 > file_size_ - p) {
      *error = "invalid file name in ADVINSTSFX file table";
      return false;
    }
    std::vector<unsigned char> name_bytes(size_t(namesize) * 2);
    if (!ReadAt(in, p, name_bytes.data(), name_bytes.size())) {
      *error = "truncated file name in ADVINSTSFX file table";
      return false;
    }
    p += name_bytes.size();
    if (uint64_t(offset) + size > file_size_) {
      *error = "file range outside the installer";
      return false;
    }
    entries_.push_back({Utf16ToUtf8(name_bytes), size, offset, flags});
  }
  return true;
}

const AdvancedInstallerEntry* AdvancedInstaller::Payload() const {
  for (const auto& e : entries_) {
    const std::string n = Lower(e.name);
    if (n.size() >= 8 && n.compare(n.size() - 8, 8, "files.7z") == 0)
      return &e;
  }
  return nullptr;
}

bool AdvancedInstaller::ExtractEntry(const AdvancedInstallerEntry& entry,
                                     const std::filesystem::path& out,
                                     std::string* error) const {
  std::ifstream in(path_, std::ios::binary);
  if (!in) {
    *error = "cannot open " + path_.string();
    return false;
  }
  in.seekg(std::streamoff(entry.offset));
  std::ofstream os(out, std::ios::binary | std::ios::trunc);
  if (!os) {
    *error = "cannot create " + out.string();
    return false;
  }
  std::vector<char> buf(1 << 20);
  uint64_t left = entry.size;
  uint64_t pos = 0;
  while (left) {
    const size_t n = size_t(std::min<uint64_t>(left, buf.size()));
    in.read(buf.data(), std::streamsize(n));
    if (in.gcount() != std::streamsize(n)) {
      *error = "truncated payload in " + path_.string();
      return false;
    }
    if (entry.flags == 2 && pos < kXor) {
      const size_t x = size_t(std::min<uint64_t>(uint64_t(n), kXor - pos));
      for (size_t i = 0; i < x; i++) buf[i] = char(buf[i] ^ 0xFF);
    }
    os.write(buf.data(), std::streamsize(n));
    if (!os) {
      *error = "write failed for " + out.string();
      return false;
    }
    left -= n;
    pos += n;
  }
  return true;
}

std::string Find7z() {
  const char* path = std::getenv("PATH");
  if (!path) return {};
  for (const char* name : {"7z", "7zz", "7za"}) {
    std::string p(path);
    size_t start = 0;
    while (start <= p.size()) {
      size_t end = p.find(':', start);
      if (end == std::string::npos) end = p.size();
      std::string dir = p.substr(start, end - start);
      if (!dir.empty()) {
        std::filesystem::path candidate = std::filesystem::path(dir) / name;
        std::error_code ec;
        if (std::filesystem::is_regular_file(candidate, ec) &&
            access(candidate.c_str(), X_OK) == 0)
          return candidate.string();
      }
      if (end == p.size()) break;
      start = end + 1;
    }
  }
  return {};
}

bool Unpack7z(const std::string& seven_zip,
              const std::filesystem::path& archive,
              const std::filesystem::path& game_dir, std::string* error) {
  if (seven_zip.empty()) {
    *error = "7z not found; install the '7zip' package";
    return false;
  }
  std::error_code ec;
  std::filesystem::create_directories(game_dir, ec);
  if (ec) {
    *error = "cannot create " + game_dir.string() + ": " + ec.message();
    return false;
  }
  std::string out_arg = "-o" + game_dir.string();
  std::string arc_arg = archive.string();
  std::string exe_arg = seven_zip;
  char* argv[] = {const_cast<char*>(exe_arg.c_str()),
                  const_cast<char*>("x"), const_cast<char*>("-y"),
                  const_cast<char*>(out_arg.c_str()),
                  const_cast<char*>(arc_arg.c_str()), nullptr};
  pid_t pid = 0;
  const int rc = posix_spawnp(&pid, seven_zip.c_str(), nullptr, nullptr, argv,
                              environ);
  if (rc != 0) {
    *error = "cannot run 7z: " + std::string(std::strerror(rc));
    return false;
  }
  int status = 0;
  while (waitpid(pid, &status, 0) < 0 && errno == EINTR) {
  }
  if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
    *error = "7z failed to unpack " + archive.string();
    return false;
  }
  return true;
}

bool AdvancedInstaller::ExtractGame(const std::filesystem::path& game_dir,
                                    std::string* error) const {
  const AdvancedInstallerEntry* payload = Payload();
  if (!payload) {
    *error = "FILES.7z not found in the installer";
    return false;
  }
  std::error_code ec;
  std::filesystem::create_directories(game_dir, ec);
  if (ec) {
    *error = "cannot create " + game_dir.string() + ": " + ec.message();
    return false;
  }
  const std::filesystem::path archive = game_dir / ".pengubirds-payload.7z";
  if (!ExtractEntry(*payload, archive, error)) return false;
  const std::string seven = Find7z();
  if (!Unpack7z(seven, archive, game_dir, error)) {
    std::filesystem::remove(archive, ec);
    return false;
  }
  std::filesystem::remove(archive, ec);
  return true;
}

}  // namespace pengubirds
