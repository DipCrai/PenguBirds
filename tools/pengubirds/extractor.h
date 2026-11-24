// Native extraction of Advanced Installer self-extracting EXEs.
//
// The retail Rovio PC installers are Advanced Installer SFX packages. They
// carry an ADVINSTSFX footer with a file table; the game payload is a
// FILES.7z entry whose first 0x200 bytes are XORed with 0xFF. No Wine and
// no Windows installer is involved.
#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace pengubirds {

struct AdvancedInstallerEntry {
  std::string name;  // UTF-8, may contain backslashes
  uint32_t size = 0;
  uint32_t offset = 0;
  uint32_t flags = 0;
};

class AdvancedInstaller {
 public:
  explicit AdvancedInstaller(std::filesystem::path path);

  // Parses the footer and file table. Returns false and fills `error` on
  // malformed input.
  bool Open(std::string* error);

  const std::vector<AdvancedInstallerEntry>& entries() const { return entries_; }
  const std::filesystem::path& path() const { return path_; }

  // The FILES.7z payload entry, or nullptr.
  const AdvancedInstallerEntry* Payload() const;

  // Writes `entry` to `out`, undoing the optional first-0x200-byte XOR.
  bool ExtractEntry(const AdvancedInstallerEntry& entry,
                    const std::filesystem::path& out, std::string* error) const;

  // Extracts the payload to a temporary .7z next to `work_dir`, unpacks it
  // with 7z into `game_dir`, and removes the temporary file. Returns false
  // and fills `error` on failure.
  bool ExtractGame(const std::filesystem::path& game_dir,
                   std::string* error) const;

 private:
  std::filesystem::path path_;
  uint64_t file_size_ = 0;
  std::vector<AdvancedInstallerEntry> entries_;
};

// Finds a 7z-compatible executable (7z, 7zz, 7za) in PATH.
std::string Find7z();

// Runs `7z x -y -o<game_dir> <archive>`; returns true on exit status 0.
bool Unpack7z(const std::string& seven_zip,
              const std::filesystem::path& archive,
              const std::filesystem::path& game_dir,
              std::string* error);

}  // namespace pengubirds
