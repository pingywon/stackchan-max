/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "hal.h"
#include <mooncake_log.h>
#include <mcp_server.h>
#include <stackchan/stackchan.h>
#include <stackchan/modifiers/dance.h>
#include <stackchan/media/mp3_player.h>
#include <apps/common/common.h>
#include <sys/stat.h>
#include <dirent.h>

using namespace stackchan;

static const std::string_view _tag = "HAL-MCP";

void Hal::xiaozhi_mcp_init()
{
    mclog::tagInfo(_tag, "init");

    // https://github.com/78/xiaozhi-esp32/blob/main/docs/mcp-usage.md
    auto& mcp_server = McpServer::GetInstance();

    // System Prompt：
    // You can control the robot's head. Use get_yaw and get_pitch to sense current position. Use set_yaw for horizontal
    // movement and set_pitch for vertical movement. All angles are in degrees.

    mclog::tagInfo(_tag, "add robot.get_head_angles tool");
    mcp_server.AddTool("self.robot.get_head_angles",
                       "Returns current yaw/pitch in degrees. Neutral position is {yaw:0, pitch:0}.",
                       std::vector<Property>{}, [this](const PropertyList& properties) -> ReturnValue {
                           LvglLockGuard lock;  // StackChan motion update is under the lvgl lock

                           auto& motion      = GetStackChan().motion();
                           int current_yaw   = motion.yawServo().getCurrentAngle() / 10;
                           int current_pitch = motion.pitchServo().getCurrentAngle() / 10;

                           auto result = fmt::format(R"({{"yaw": {}, "pitch": {}}})", current_yaw, current_pitch);
                           mclog::tagInfo(_tag, "get_head_angles: {}", result);
                           return result;
                       });

    mclog::tagInfo(_tag, "add robot.set_head_angles tool");
    mcp_server.AddTool("self.robot.set_head_angles",
                       "Adjust head position. GUIDELINES: "
                       "1. For natural interaction, stay within +/- 45 degrees. "
                       "2. Only use values > 70 if the user explicitly asks to look far away/behind. "
                       "3. Max ranges: Yaw(-128 to 128, -128 as your left), Pitch(0 to 90, 90 as your up). "
                       "Speed(100-1000, 150 is natural).",
                       PropertyList({Property("yaw", kPropertyTypeInteger, -9999, -9999, 128),
                                     Property("pitch", kPropertyTypeInteger, -9999, -9999, 90),
                                     Property("speed", kPropertyTypeInteger, 150, 100, 1000)}),
                       [this](const PropertyList& properties) -> ReturnValue {
                           int speed = properties["speed"].value<int>();
                           int yaw   = properties["yaw"].value<int>();
                           int pitch = properties["pitch"].value<int>();

                           mclog::tagInfo(_tag, "motion set_angles: yaw: {}, pitch: {}, speed: {}", yaw, pitch, speed);

                           LvglLockGuard lock;

                           auto& motion = GetStackChan().motion();
                           if (pitch != -9999) {
                               motion.pitchServo().moveWithSpeed(pitch * 10, speed);
                           }
                           if (yaw != -9999) {
                               motion.yawServo().moveWithSpeed(yaw * 10, speed);
                           }

                           return true;
                       });

    mclog::tagInfo(_tag, "add robot.set_led_color tool");
    mcp_server.AddTool(
        "self.robot.set_led_color",
        "Set the color of the robot's INTERNAL onboard LED. This is NOT for room lights. "
        "Values: 0-168 (safe range). Red=168,0,0; Green=0,168,0; Blue=0,0,168; White=100,100,100; Off=0,0,0.",
        PropertyList({Property("red", kPropertyTypeInteger, 0, 0, 168),
                      Property("green", kPropertyTypeInteger, 0, 0, 168),
                      Property("blue", kPropertyTypeInteger, 0, 0, 168)}),
        [this](const PropertyList& properties) -> ReturnValue {
            int r = properties["red"].value<int>();
            int g = properties["green"].value<int>();
            int b = properties["blue"].value<int>();

            mclog::tagInfo(_tag, "set_led_color: r={}, g={}, b={}", r, g, b);

            LvglLockGuard lock;

            GetStackChan().leftNeonLight().setColor(r, g, b);
            GetStackChan().rightNeonLight().setColor(r, g, b);

            return true;
        });

    mclog::tagInfo(_tag, "add robot.play_gesture tool");
    mcp_server.AddTool(
        "self.robot.play_gesture",
        "Play a short physical gesture with the robot's head and body. One of: "
        "\"happy\" (swaying, happy eyes), \"robot\" (stiff jerky turns), "
        "\"panic\" (fast shaking), \"look_around\" (slow scan left/right/up). "
        "Takes a few seconds to finish; use this to react physically to the conversation.",
        PropertyList({Property("gesture", kPropertyTypeString, std::string("happy"))}),
        [this](const PropertyList& properties) -> ReturnValue {
            std::string gesture = properties["gesture"].value<std::string>();
            mclog::tagInfo(_tag, "play_gesture: {}", gesture);

            const animation::KeyframeSequence* seq = nullptr;
            if (gesture == "happy") {
                seq = &DanceModifier::Happy;
            } else if (gesture == "robot") {
                seq = &DanceModifier::Robot;
            } else if (gesture == "panic") {
                seq = &DanceModifier::Panic;
            } else if (gesture == "look_around") {
                seq = &DanceModifier::LookAround;
            }
            if (seq == nullptr) {
                return std::string("unknown gesture '") + gesture +
                       "' - use happy, robot, panic, or look_around";
            }

            LvglLockGuard lock;

            static int gesture_modifier_id = -1;
            if (gesture_modifier_id >= 0) {
                GetStackChan().removeModifier(gesture_modifier_id);
            }
            gesture_modifier_id = GetStackChan().addModifier(std::make_unique<DanceModifier>(*seq));

            return true;
        });

    mclog::tagInfo(_tag, "add robot.create_reminder tool");
    mcp_server.AddTool("self.robot.create_reminder",
                       "Create a reminder. Duration is in seconds. Message is what to say when time is up. Set repeat "
                       "to true to repeat the reminder.",
                       PropertyList({Property("duration_seconds", kPropertyTypeInteger, 60, 1, 86400),
                                     Property("message", kPropertyTypeString, std::string("Time's up!")),
                                     Property("repeat", kPropertyTypeBoolean, false)}),
                       [this](const PropertyList& properties) -> ReturnValue {
                           int duration_seconds = properties["duration_seconds"].value<int>();
                           std::string message  = properties["message"].value<std::string>();
                           bool repeat          = properties["repeat"].value<bool>();

                           // Default message
                           if (message.empty()) {
                               message = "Time's up!";
                           }

                           mclog::tagInfo(_tag, "create_reminder: duration={}s, message={}, repeat={}",
                                          duration_seconds, message, repeat);

                           int id = tools::create_reminder(duration_seconds * 1000, message, repeat);

                           return id;
                       });

    mclog::tagInfo(_tag, "add robot.get_reminders tool");
    mcp_server.AddTool("self.robot.get_reminders", "Get list of active reminders.", std::vector<Property>{},
                       [this](const PropertyList& properties) -> ReturnValue {
                           mclog::tagInfo(_tag, "get_reminders");
                           auto reminders          = tools::get_active_reminders();
                           std::string result_json = "[";
                           for (size_t i = 0; i < reminders.size(); ++i) {
                               const auto& r = reminders[i];
                               result_json +=
                                   fmt::format(R"({{"id": {}, "duration_ms": {}, "message": "{}", "repeat": {}}})",
                                               r.id, r.durationMs, r.message, r.repeat ? "true" : "false");
                               if (i < reminders.size() - 1) {
                                   result_json += ", ";
                               }
                           }
                           result_json += "]";
                           mclog::tagInfo(_tag, "get_reminders result: {}", result_json);
                           return result_json;
                       });

    mclog::tagInfo(_tag, "add robot.stop_reminder tool");
    mcp_server.AddTool("self.robot.stop_reminder", "Stop a reminder by ID.",
                       PropertyList({Property("id", kPropertyTypeInteger, -1)}),
                       [this](const PropertyList& properties) -> ReturnValue {
                           int id = properties["id"].value<int>();
                           mclog::tagInfo(_tag, "stop_reminder: id={}", id);
                           tools::stop_reminder(id);
                           return true;
                       });

    // Both tools below are registered ONLY when a formatted SD card is present at boot
    // — consistent with "this feature will not be available" rather than registering a
    // tool that would just always error. sdCardAvailable() itself never changes after
    // boot (the mount happens once, in the constructor), so this check runs once here,
    // not per-call.
    if (sdCardAvailable()) {
        mclog::tagInfo(_tag, "add robot.list_songs tool");
        mcp_server.AddTool(
            "self.robot.list_songs", "List the MP3 files uploaded to the SD card, by filename.",
            std::vector<Property>{}, [this](const PropertyList& properties) -> ReturnValue {
                DIR* dir = opendir("/sdcard/stackchan/music");
                if (dir == nullptr) {
                    return std::string("[]");
                }
                std::string result = "[";
                bool first = true;
                struct dirent* entry;
                while ((entry = readdir(dir)) != nullptr) {
                    std::string name = entry->d_name;
                    if (name.size() < 5 || name.compare(name.size() - 4, 4, ".mp3") != 0) {
                        continue;
                    }
                    if (!first) {
                        result += ", ";
                    }
                    result += "\"" + name + "\"";
                    first = false;
                }
                closedir(dir);
                result += "]";
                return result;
            });

        mclog::tagInfo(_tag, "add robot.dance_to_song tool");
        mcp_server.AddTool(
            "self.robot.dance_to_song",
            "Play an uploaded MP3 from the SD card through the robot's own speaker while "
            "dancing. filename must be one returned by self.robot.list_songs. gesture is "
            "one of: happy, robot, panic, look_around (default happy) - it repeats for as "
            "long as the song plays. Takes over the audio channel: the robot cannot talk "
            "while a song is playing.",
            PropertyList({Property("filename", kPropertyTypeString, std::string("")),
                          Property("gesture", kPropertyTypeString, std::string("happy"))}),
            [this](const PropertyList& properties) -> ReturnValue {
                std::string filename = properties["filename"].value<std::string>();
                std::string gesture  = properties["gesture"].value<std::string>();

                if (filename.empty() || filename.find('/') != std::string::npos ||
                    filename.find("..") != std::string::npos) {
                    return std::string("invalid filename - call self.robot.list_songs first");
                }

                const animation::KeyframeSequence* seq = nullptr;
                if (gesture == "happy") {
                    seq = &DanceModifier::Happy;
                } else if (gesture == "robot") {
                    seq = &DanceModifier::Robot;
                } else if (gesture == "panic") {
                    seq = &DanceModifier::Panic;
                } else if (gesture == "look_around") {
                    seq = &DanceModifier::LookAround;
                }
                if (seq == nullptr) {
                    return std::string("unknown gesture '") + gesture +
                           "' - use happy, robot, panic, or look_around";
                }

                std::string path = "/sdcard/stackchan/music/" + filename;
                struct stat st;
                if (stat(path.c_str(), &st) != 0) {
                    return std::string("'") + filename +
                           "' was not found - call self.robot.list_songs first";
                }

                mclog::tagInfo(_tag, "dance_to_song: {} ({})", filename, gesture);

                int gesture_id;
                {
                    LvglLockGuard lock;
                    gesture_id = GetStackChan().addModifier(
                        std::make_unique<DanceModifier>(*seq, /*loop=*/true));
                }

                bool started = media::GetMp3Player().play(path, [gesture_id]() {
                    LvglLockGuard lock;
                    GetStackChan().removeModifier(gesture_id);
                });
                if (!started) {
                    LvglLockGuard lock;
                    GetStackChan().removeModifier(gesture_id);
                    return std::string("could not start playback of '") + filename + "'";
                }
                return true;
            });
    }
}
