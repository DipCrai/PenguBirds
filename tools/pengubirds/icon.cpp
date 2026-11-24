#include "icon.h"

#include <cstdint>
#include <cstring>
#include <fstream>
#include <vector>

namespace pengubirds {
namespace {

uint16_t Le16(const std::vector<uint8_t>& b, size_t o) {
  return o + 1 < b.size() ? uint16_t(b[o] | b[o + 1] << 8) : 0;
}
uint32_t Le32(const std::vector<uint8_t>& b, size_t o) {
  return o + 3 < b.size() ? uint32_t(b[o]) | uint32_t(b[o + 1]) << 8 |
                                uint32_t(b[o + 2]) << 16 | uint32_t(b[o + 3]) << 24
                          : 0;
}

struct Section {
  uint32_t va, vsize, raw, rawsize;
};

struct Pe {
  const std::vector<uint8_t>& b;
  uint32_t resource_rva = 0, resource_size = 0;
  std::vector<Section> sections;

  size_t Off(uint32_t rva) const {
    for (const auto& s : sections) {
      if (rva >= s.va && rva < s.va + s.vsize) {
        const uint32_t d = rva - s.va;
        if (d < s.rawsize) return size_t(s.raw + d);
        break;
      }
    }
    return size_t(-1);
  }

  bool Load() {
    if (b.size() < 0x40 || b[0] != 'M' || b[1] != 'Z') return false;
    const size_t pe = Le32(b, 0x3C);
    if (pe + 24 > b.size() || Le32(b, pe) != 0x00004550) return false;
    const size_t coff = pe + 4;
    const uint16_t nsec = Le16(b, coff + 2);
    const uint16_t opt_size = Le16(b, coff + 16);
    const size_t opt = coff + 20;
    if (opt + 2 > b.size()) return false;
    const uint16_t magic = Le16(b, opt);
    const size_t dir = opt + (magic == 0x20B ? 112 : 96);
    if (dir + 16 > b.size()) return false;
    resource_rva = Le32(b, dir + 16);
    resource_size = Le32(b, dir + 20);
    const size_t sec = opt + opt_size;
    if (sec + size_t(nsec) * 40 > b.size()) return false;
    for (uint16_t i = 0; i < nsec; i++) {
      const size_t s = sec + size_t(i) * 40;
      sections.push_back({Le32(b, s + 12), Le32(b, s + 8), Le32(b, s + 20),
                          Le32(b, s + 16)});
    }
    return resource_rva && resource_size;
  }
};

struct Entry {
  uint32_t id = 0;
  bool named = false;
  bool subdir = false;
  uint32_t offset = 0;  // relative to the resource directory
};

std::vector<Entry> Entries(const Pe& pe, uint32_t dir_rel) {
  std::vector<Entry> out;
  const size_t base = pe.Off(pe.resource_rva);
  if (base == size_t(-1) || base + dir_rel + 16 > pe.b.size()) return out;
  const size_t d = base + dir_rel;
  const uint16_t named = Le16(pe.b, d + 12);
  const uint16_t ids = Le16(pe.b, d + 14);
  const size_t n = size_t(named) + ids;
  if (d + 16 + n * 8 > pe.b.size()) return out;
  for (size_t i = 0; i < n; i++) {
    const size_t e = d + 16 + i * 8;
    Entry x;
    const uint32_t name = Le32(pe.b, e);
    const uint32_t off = Le32(pe.b, e + 4);
    x.named = (name & 0x80000000) != 0;
    x.id = name & 0x7FFFFFFF;
    x.subdir = (off & 0x80000000) != 0;
    x.offset = off & 0x7FFFFFFF;
    out.push_back(x);
  }
  return out;
}

struct DataEntry {
  uint32_t rva = 0, size = 0;
  bool valid = false;
};

DataEntry Data(const Pe& pe, uint32_t dir_rel) {
  DataEntry out;
  const size_t base = pe.Off(pe.resource_rva);
  if (base == size_t(-1) || base + dir_rel + 16 > pe.b.size()) return out;
  const size_t d = base + dir_rel;
  out.rva = Le32(pe.b, d);
  out.size = Le32(pe.b, d + 4);
  out.valid = out.rva && out.size && out.rva + out.size <= pe.b.size();
  return out;
}

// Walks type -> name -> language and returns the first data entry.
DataEntry FirstData(const Pe& pe, uint32_t type_id) {
  for (const auto& t : Entries(pe, 0)) {
    if (t.named || t.id != type_id || !t.subdir) continue;
    for (const auto& n : Entries(pe, t.offset)) {
      if (!n.subdir) continue;
      for (const auto& l : Entries(pe, n.offset))
        if (!l.subdir) return Data(pe, l.offset);
    }
  }
  return {};
}

}  // namespace

bool ExtractIcon(const std::filesystem::path& pe_file,
                 const std::filesystem::path& out, std::string* error) {
  std::ifstream in(pe_file, std::ios::binary);
  if (!in) {
    *error = "cannot open " + pe_file.string();
    return false;
  }
  std::vector<uint8_t> b((std::istreambuf_iterator<char>(in)),
                         std::istreambuf_iterator<char>());
  Pe pe{b, 0, 0, {}};
  if (!pe.Load()) {
    *error = "not a PE file with resources";
    return false;
  }
  const DataEntry group = FirstData(pe, 14);  // RT_GROUP_ICON
  if (!group.valid) {
    *error = "no RT_GROUP_ICON";
    return false;
  }
  const size_t g = pe.Off(group.rva);
  if (g == size_t(-1) || g + 6 > b.size()) {
    *error = "invalid icon group";
    return false;
  }
  const uint16_t count = Le16(b, g + 4);
  if (count == 0 || g + 6 + size_t(count) * 14 > b.size()) {
    *error = "empty icon group";
    return false;
  }
  uint16_t best_id = 0, best_w = 0, best_h = 0, best_planes = 1, best_bits = 32;
  for (uint16_t i = 0; i < count; i++) {
    const size_t e = g + 6 + size_t(i) * 14;
    const uint16_t w = b[e], h = b[e + 1];
    const uint16_t planes = Le16(b, e + 4), bits = Le16(b, e + 6);
    const uint16_t id = Le16(b, e + 12);
    const uint32_t area = uint32_t(w ? w : 256) * uint32_t(h ? h : 256);
    const uint32_t best_area = uint32_t(best_w ? best_w : 256) *
                               uint32_t(best_h ? best_h : 256);
    if (best_id == 0 || area > best_area) {
      best_id = id; best_w = w; best_h = h; best_planes = planes;
      best_bits = bits;
    }
  }
  DataEntry image = FirstData(pe, 3);  // RT_ICON: first only; find by id below
  // FirstData cannot select by id, so walk again for the chosen icon id.
  image = {};
  for (const auto& t : Entries(pe, 0)) {
    if (t.named || t.id != 3 || !t.subdir) continue;
    for (const auto& n : Entries(pe, t.offset)) {
      if (n.named || n.id != best_id || !n.subdir) continue;
      for (const auto& l : Entries(pe, n.offset))
        if (!l.subdir) { image = Data(pe, l.offset); break; }
      break;
    }
    if (image.valid) break;
  }
  if (!image.valid) {
    *error = "icon data not found";
    return false;
  }
  const size_t img = pe.Off(image.rva);
  if (img == size_t(-1) || img + image.size > b.size()) {
    *error = "invalid icon data";
    return false;
  }
  std::vector<uint8_t> ico(22 + image.size, 0);
  ico[2] = 1;                 // type: icon
  ico[4] = 1; ico[5] = 0;     // count
  ico[6] = uint8_t(best_w);   // width (0 = 256)
  ico[7] = uint8_t(best_h);
  ico[10] = uint8_t(best_planes & 0xFF);
  ico[11] = uint8_t(best_planes >> 8);
  ico[12] = uint8_t(best_bits & 0xFF);
  ico[13] = uint8_t(best_bits >> 8);
  ico[14] = uint8_t(image.size & 0xFF);
  ico[15] = uint8_t((image.size >> 8) & 0xFF);
  ico[16] = uint8_t((image.size >> 16) & 0xFF);
  ico[17] = uint8_t((image.size >> 24) & 0xFF);
  ico[18] = 22;  // offset to image
  std::memcpy(ico.data() + 22, b.data() + img, image.size);
  std::ofstream os(out, std::ios::binary | std::ios::trunc);
  if (!os) {
    *error = "cannot write " + out.string();
    return false;
  }
  os.write(reinterpret_cast<const char*>(ico.data()), std::streamsize(ico.size()));
  if (!os) return false;
  // Modern icons often contain a PNG image; expose it directly so desktops
  // that do not load .ico files still get a proper icon.
  static const unsigned char kPng[8] = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
  if (image.size > 8 && std::memcmp(b.data() + img, kPng, 8) == 0) {
    std::ofstream png(out.parent_path() / "icon.png",
                      std::ios::binary | std::ios::trunc);
    if (png) png.write(reinterpret_cast<const char*>(b.data() + img),
                       std::streamsize(image.size));
  }
  return true;
}

}  // namespace pengubirds
