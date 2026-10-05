/*
 * Skin simulator.
 *
 * Compiles the REAL skin sources (max.cpp / volt.cpp / the stock default skin) against
 * desktop LVGL, renders them into a memory framebuffer at the device's exact resolution
 * and colour depth, and writes PNGs. No hardware, no flashing.
 *
 * This exists because v1.0.0 shipped without ever having been rendered. Anything that
 * shows up here would have shown up on the device.
 *
 *   ./skinsim <outdir> <assets_dir>
 */
#include <lvgl.h>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <string>
#include <vector>

#include "../../firmware/main/stackchan/avatar/skins/skins.h"
#include "../../firmware/main/stackchan/avatar/skins/max/max.h"
#include "../../firmware/main/stackchan/avatar/skins/volt/volt.h"
#include "../../firmware/main/stackchan/avatar/skins/default/default.h"

void sim_set_assets_dir(const char* dir);

static const int W = 320;
static const int H = 240;

static uint16_t g_fb[W * H];

static void flush_cb(lv_display_t* disp, const lv_area_t* area, uint8_t* px_map)
{
    // LV_COLOR_DEPTH 16 -> two bytes per pixel, same as the panel.
    const uint16_t* src = reinterpret_cast<const uint16_t*>(px_map);
    for (int y = area->y1; y <= area->y2; y++) {
        for (int x = area->x1; x <= area->x2; x++) {
            if (x >= 0 && x < W && y >= 0 && y < H) {
                g_fb[y * W + x] = *src;
            }
            src++;
        }
    }
    lv_display_flush_ready(disp);
}

/* Minimal PNG writer: zlib stored blocks + CRC32, so we need no image library. */
static uint32_t crc_table[256];
static bool crc_ready = false;

static uint32_t crc32_buf(const unsigned char* buf, size_t len, uint32_t crc)
{
    if (!crc_ready) {
        for (uint32_t n = 0; n < 256; n++) {
            uint32_t c = n;
            for (int k = 0; k < 8; k++) c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            crc_table[n] = c;
        }
        crc_ready = true;
    }
    crc = crc ^ 0xFFFFFFFFu;
    for (size_t i = 0; i < len; i++) crc = crc_table[(crc ^ buf[i]) & 0xFF] ^ (crc >> 8);
    return crc ^ 0xFFFFFFFFu;
}

static void put_be32(std::vector<unsigned char>& v, uint32_t x)
{
    v.push_back((x >> 24) & 0xFF);
    v.push_back((x >> 16) & 0xFF);
    v.push_back((x >> 8) & 0xFF);
    v.push_back(x & 0xFF);
}

static void chunk(std::vector<unsigned char>& out, const char* type,
                  const std::vector<unsigned char>& data)
{
    put_be32(out, (uint32_t)data.size());
    std::vector<unsigned char> td;
    td.insert(td.end(), type, type + 4);
    td.insert(td.end(), data.begin(), data.end());
    out.insert(out.end(), td.begin(), td.end());
    put_be32(out, crc32_buf(td.data(), td.size(), 0));
}

static bool write_png(const char* path)
{
    std::vector<unsigned char> raw;
    raw.reserve((size_t)H * (W * 3 + 1));
    for (int y = 0; y < H; y++) {
        raw.push_back(0);  // filter: none
        for (int x = 0; x < W; x++) {
            uint16_t p = g_fb[y * W + x];
            unsigned r = (p >> 11) & 0x1F, g = (p >> 5) & 0x3F, b = p & 0x1F;
            raw.push_back((unsigned char)((r << 3) | (r >> 2)));
            raw.push_back((unsigned char)((g << 2) | (g >> 4)));
            raw.push_back((unsigned char)((b << 3) | (b >> 2)));
        }
    }

    // zlib stream with stored (uncompressed) deflate blocks
    std::vector<unsigned char> z;
    z.push_back(0x78);
    z.push_back(0x01);
    size_t pos = 0;
    while (pos < raw.size()) {
        size_t n     = raw.size() - pos;
        if (n > 65535) n = 65535;
        bool last    = (pos + n) >= raw.size();
        z.push_back(last ? 1 : 0);
        z.push_back(n & 0xFF);
        z.push_back((n >> 8) & 0xFF);
        z.push_back((~n) & 0xFF);
        z.push_back(((~n) >> 8) & 0xFF);
        z.insert(z.end(), raw.begin() + pos, raw.begin() + pos + n);
        pos += n;
    }
    uint32_t a = 1, b = 0;
    for (size_t i = 0; i < raw.size(); i++) {
        a = (a + raw[i]) % 65521;
        b = (b + a) % 65521;
    }
    put_be32(z, (b << 16) | a);

    std::vector<unsigned char> png = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
    std::vector<unsigned char> ihdr;
    put_be32(ihdr, W);
    put_be32(ihdr, H);
    ihdr.push_back(8);  // bit depth
    ihdr.push_back(2);  // truecolour
    ihdr.push_back(0);
    ihdr.push_back(0);
    ihdr.push_back(0);
    chunk(png, "IHDR", ihdr);
    chunk(png, "IDAT", z);
    chunk(png, "IEND", {});

    FILE* f = fopen(path, "wb");
    if (!f) return false;
    fwrite(png.data(), 1, png.size(), f);
    fclose(f);
    return true;
}

static void settle()
{
    // Give LVGL a few cycles so layout, transforms and redraw all land.
    for (int i = 0; i < 12; i++) {
        lv_tick_inc(20);
        lv_timer_handler();
    }
}

using namespace stackchan::avatar;

struct EmoRow {
    Emotion e;
    const char* name;
    int mouthWeight;
};

static EmoRow EMOS[] = {
    {Emotion::Neutral, "neutral", 12}, {Emotion::Happy, "happy", 62},
    {Emotion::Angry, "angry", 22},     {Emotion::Sad, "sad", 8},
    {Emotion::Doubt, "doubt", 30},     {Emotion::Sleepy, "sleepy", 5},
};

static void shoot(SkinBase* av, const std::string& outdir, const char* skin, const char* tag)
{
    settle();
    std::string path = outdir + "/" + skin + "_" + tag + ".png";
    if (write_png(path.c_str())) {
        printf("  wrote %s\n", path.c_str());
    } else {
        printf("  FAILED %s\n", path.c_str());
    }
}

static void run_skin(SkinId id, const char* skin, const std::string& outdir)
{
    printf("[%s]\n", skin);

    lv_obj_t* scr = lv_screen_active();
    lv_obj_clean(scr);

    auto avatar = create_avatar_of(id, scr);
    if (!avatar) {
        printf("  create_avatar_of returned null\n");
        return;
    }

    for (auto& row : EMOS) {
        avatar->setEmotion(row.e);
        avatar->mouth().setWeight(row.mouthWeight);
        shoot(avatar.get(), outdir, skin, row.name);
    }

    // Blink: eyes fully shut. Exercises the weight->geometry path at its extreme.
    avatar->setEmotion(Emotion::Neutral);
    avatar->leftEye().setWeight(0);
    avatar->rightEye().setWeight(0);
    avatar->mouth().setWeight(10);
    shoot(avatar.get(), outdir, skin, "blink");

    // Speaking: wide open mouth.
    avatar->setEmotion(Emotion::Neutral);
    avatar->mouth().setWeight(100);
    shoot(avatar.get(), outdir, skin, "speaking");

    // Gaze extremes — what GazeModifier drives during a hard turn.
    avatar->setEmotion(Emotion::Neutral);
    avatar->mouth().setWeight(20);
    avatar->leftEye().setPosition(uitk::Vector2i(100, 0));
    avatar->rightEye().setPosition(uitk::Vector2i(100, 0));
    shoot(avatar.get(), outdir, skin, "gaze_right");

    avatar->leftEye().setPosition(uitk::Vector2i(-100, -100));
    avatar->rightEye().setPosition(uitk::Vector2i(-100, -100));
    shoot(avatar.get(), outdir, skin, "gaze_upleft");

    avatar->leftEye().setPosition(uitk::Vector2i(0, 0));
    avatar->rightEye().setPosition(uitk::Vector2i(0, 0));

    // Size extremes — the other axis of the Feature contract.
    avatar->leftEye().setSize(-100);
    avatar->rightEye().setSize(-100);
    shoot(avatar.get(), outdir, skin, "size_min");

    avatar->leftEye().setSize(100);
    avatar->rightEye().setSize(100);
    shoot(avatar.get(), outdir, skin, "size_max");

    avatar.reset();          // exercise teardown — this is where lifetime bugs surface
    lv_obj_clean(scr);
    settle();
    printf("  teardown OK\n");
}

int main(int argc, char** argv)
{
    std::string outdir = (argc > 1) ? argv[1] : "out";
    if (argc > 2) sim_set_assets_dir(argv[2]);

    lv_init();

    lv_display_t* disp = lv_display_create(W, H);
    static uint8_t buf[W * H * 2];
    lv_display_set_buffers(disp, buf, nullptr, sizeof(buf), LV_DISPLAY_RENDER_MODE_FULL);
    lv_display_set_flush_cb(disp, flush_cb);

    printf("LVGL %d.%d.%d, %dx%d, colour depth %d\n", LVGL_VERSION_MAJOR, LVGL_VERSION_MINOR,
           LVGL_VERSION_PATCH, W, H, LV_COLOR_DEPTH);

    run_skin(SkinId::Max, "max", outdir);
    run_skin(SkinId::Volt, "volt", outdir);
    run_skin(SkinId::Default, "default", outdir);

    printf("done\n");
    return 0;
}
