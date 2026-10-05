/*
 * Host-side stubs for the two firmware services the skins touch.
 *
 * assets::get_image() normally reads the mmap'd assets partition. Here it reads the same
 * .bin files straight off disk, parsed byte-for-byte the way main/assets/assets.cpp does,
 * so the simulator exercises the real image path rather than a shortcut.
 */
#include <lvgl.h>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <string>
#include <string_view>
#include <map>
#include <vector>

static std::string g_assets_dir = "assets_bin";
static std::map<std::string, std::vector<unsigned char>> g_cache;

void sim_set_assets_dir(const char* dir)
{
    g_assets_dir = dir;
}

namespace assets {

static bool has_suffix(std::string_view str, std::string_view suffix)
{
    return str.size() >= suffix.size() &&
           str.compare(str.size() - suffix.size(), suffix.size(), suffix) == 0;
}

lv_image_dsc_t get_image(std::string_view name)
{
    std::string key(name);
    lv_image_dsc_t dsc = {};

    auto it = g_cache.find(key);
    if (it == g_cache.end()) {
        std::string path = g_assets_dir + "/" + key;
        FILE* f = fopen(path.c_str(), "rb");
        if (!f) {
            fprintf(stderr, "[assets] MISS %s\n", path.c_str());
            return dsc;
        }
        fseek(f, 0, SEEK_END);
        long sz = ftell(f);
        fseek(f, 0, SEEK_SET);
        std::vector<unsigned char> buf(static_cast<size_t>(sz));
        if (fread(buf.data(), 1, buf.size(), f) != buf.size()) {
            fclose(f);
            fprintf(stderr, "[assets] SHORT READ %s\n", path.c_str());
            return dsc;
        }
        fclose(f);
        it = g_cache.emplace(key, std::move(buf)).first;
        fprintf(stderr, "[assets] loaded %s (%ld bytes)\n", key.c_str(), sz);
    }

    void* data_ptr   = it->second.data();
    size_t data_size = it->second.size();

    // Mirrors main/assets/assets.cpp exactly.
    if (has_suffix(name, ".bin")) {
        if (data_size > sizeof(lv_image_header_t)) {
            memcpy(&dsc.header, data_ptr, sizeof(lv_image_header_t));
            dsc.data_size = data_size - sizeof(lv_image_header_t);
            dsc.data      = static_cast<const uint8_t*>(data_ptr) + sizeof(lv_image_header_t);
        }
    } else {
        dsc.header.magic = LV_IMAGE_HEADER_MAGIC;
        dsc.header.cf    = LV_COLOR_FORMAT_RAW_ALPHA;
        dsc.data_size    = data_size;
        dsc.data         = static_cast<const uint8_t*>(data_ptr);
    }

    return dsc;
}

}  // namespace assets
