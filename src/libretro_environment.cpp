#include "libretro_environment.h"
#include "libretro_extensions.h"

#include <cstdarg>
#include <cstdio>
#include <cstring>

#include "host_log.h"
#include "libretro.h"
#include "libretro_host.h"

// ---------------------------------------------------------------------------
// Core log interface (RETRO_ENVIRONMENT_GET_LOG_INTERFACE)
// ---------------------------------------------------------------------------

// Doesn't need the current host, so it's safe for cores that log from their
// own threads.
static void RETRO_CALLCONV core_log(enum retro_log_level level, const char* format, ...)
{
    LogLevel our_level = LogLevel::info;
    switch (level)
    {
        case RETRO_LOG_DEBUG: our_level = LogLevel::debug;   break;
        case RETRO_LOG_INFO:  our_level = LogLevel::info;    break;
        case RETRO_LOG_WARN:  our_level = LogLevel::warning; break;
        case RETRO_LOG_ERROR: our_level = LogLevel::error;   break;
        default:              our_level = LogLevel::info;    break;
    }

    va_list args;
    va_start(args, format);
    host_log_va(our_level, "[core] ", format, args);
    va_end(args);
}

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static const char* pixel_format_name(retro_pixel_format format)
{
    switch (format)
    {
        case RETRO_PIXEL_FORMAT_0RGB1555: return "0RGB1555";
        case RETRO_PIXEL_FORMAT_XRGB8888: return "XRGB8888";
        case RETRO_PIXEL_FORMAT_RGB565:   return "RGB565";
        default:                          return "invalid";
    }
}

// Directory strings: empty means "not defined", which libretro expresses as NULL.
static const char* directory_or_null(const char* directory)
{
    return directory[0] != '\0' ? directory : nullptr;
}

// ---------------------------------------------------------------------------
// The handler
// ---------------------------------------------------------------------------

// What happened to one environment call. The core only sees true/false
// (handled vs. the other two); the distinction is for our log.
enum class EnvironmentResult
{
    handled,         // understood and done                      -> core sees true
    not_supported,   // a command we don't implement (yet)       -> core sees false
    rejected,        // a command we implement, with bad data    -> core sees false, always logged as a warning
};

// For commands we handle that read or write `data`: a NULL pointer there is a
// core bug. We log it instead of crashing. (Commands we DON'T handle may pass
// NULL legitimately, e.g. GET_INPUT_BITMASKS, so they never get here.)
static EnvironmentResult null_data(char* note, size_t note_size)
{
    std::snprintf(note, note_size, "core passed NULL data");
    return EnvironmentResult::rejected;
}

// ---------------------------------------------------------------------------
// Multitap's text input extension (libretro_extensions.h)
// ---------------------------------------------------------------------------

// Copies a core's UTF-8 string, cut at a character boundary if it doesn't fit
// (a frontend draws what we keep, so no half characters). NULL -> "".
static void copy_utf8(char* out, size_t out_size, const char* text)
{
    out[0] = '\0';
    if (text == nullptr)
    {
        return;
    }
    size_t length = std::strlen(text);
    if (length >= out_size)
    {
        length = out_size - 1;
        // Back up over continuation bytes (10xxxxxx) to the start of the cut character.
        while (length > 0 && (static_cast<unsigned char>(text[length]) & 0xC0u) == 0x80u)
        {
            length--;
        }
    }
    std::memcpy(out, text, length);
    out[length] = '\0';
}

static EnvironmentResult set_text_input(LibretroHost* host, const retro_ext_text_input* request, char* note,
                                        size_t note_size)
{
    if (request->mode > RETRO_EXT_TEXT_INPUT_TEXT)
    {
        std::snprintf(note, note_size, "unknown mode %u", request->mode);
        return EnvironmentResult::rejected;
    }
    if (request->mode == RETRO_EXT_TEXT_INPUT_TEXT && request->done == nullptr)
    {
        std::snprintf(note, note_size, "TEXT without a done callback");
        return EnvironmentResult::rejected;
    }

    LibretroTextInput* input = &host->text_input;
    uint32_t serial = input->serial + 1;
    *input = LibretroTextInput{};
    input->serial = serial;
    input->mode = request->mode;
    input->type = RETRO_EXT_TEXT_INPUT_TYPE_TEXT;   // an unknown type: plain text
    if (request->type == RETRO_EXT_TEXT_INPUT_TYPE_NUMBER)
    {
        input->type = RETRO_EXT_TEXT_INPUT_TYPE_NUMBER;
    }
    if (request->mode == RETRO_EXT_TEXT_INPUT_TEXT)
    {
        copy_utf8(input->prompt, sizeof(input->prompt), request->prompt);
        copy_utf8(input->initial, sizeof(input->initial), request->initial);
        input->max_length = request->max_length;
        input->done = request->done;
        std::snprintf(note, note_size, "TEXT \"%s\" (%s, up to %u characters)", input->prompt,
                      input->type == RETRO_EXT_TEXT_INPUT_TYPE_NUMBER ? "number" : "text", input->max_length);
    }
    else
    {
        std::snprintf(note, note_size, "%s", request->mode == RETRO_EXT_TEXT_INPUT_KEYS ? "KEYS" : "OFF");
    }
    return EnvironmentResult::handled;
}

// Handles the command and writes a short human-readable note for the log.
static EnvironmentResult handle_command(LibretroHost* host, uint32_t command, void* data, char* note, size_t note_size)
{
    switch (command)
    {
        // --- Basics ---------------------------------------------------------

        case RETRO_ENVIRONMENT_GET_LOG_INTERFACE:
        {
            if (data == nullptr)
            {
                return null_data(note, note_size);
            }
            retro_log_callback* callback = static_cast<retro_log_callback*>(data);
            callback->log = core_log;
            return EnvironmentResult::handled;
        }

        case RETRO_ENVIRONMENT_SET_PIXEL_FORMAT:
        {
            if (data == nullptr)
            {
                return null_data(note, note_size);
            }
            retro_pixel_format format = *static_cast<const retro_pixel_format*>(data);
            if (format != RETRO_PIXEL_FORMAT_0RGB1555 && format != RETRO_PIXEL_FORMAT_XRGB8888 &&
                format != RETRO_PIXEL_FORMAT_RGB565)
            {
                std::snprintf(note, note_size, "invalid format %d", static_cast<int>(format));
                return EnvironmentResult::rejected;
            }
            host->pixel_format = format;
            std::snprintf(note, note_size, "%s", pixel_format_name(format));
            return EnvironmentResult::handled;
        }

        case RETRO_ENVIRONMENT_GET_CAN_DUPE:
        {
            if (data == nullptr)
            {
                return null_data(note, note_size);
            }
            // We can show the previous frame again when the core passes NULL to video_refresh.
            *static_cast<bool*>(data) = true;
            return EnvironmentResult::handled;
        }

        case RETRO_ENVIRONMENT_GET_OVERSCAN:
        {
            if (data == nullptr)
            {
                return null_data(note, note_size);
            }
            // Deprecated. true = keep overscan (don't crop); cropping is a frontend scaling option.
            *static_cast<bool*>(data) = true;
            return EnvironmentResult::handled;
        }

        case RETRO_ENVIRONMENT_GET_LANGUAGE:
        {
            if (data == nullptr)
            {
                return null_data(note, note_size);
            }
            *static_cast<unsigned*>(data) = host->language;
            return EnvironmentResult::handled;
        }

        case RETRO_ENVIRONMENT_SET_SUPPORT_NO_GAME:
        {
            if (data == nullptr)
            {
                return null_data(note, note_size);
            }
            host->supports_no_game = *static_cast<const bool*>(data);
            std::snprintf(note, note_size, "%s", host->supports_no_game ? "yes" : "no");
            return EnvironmentResult::handled;
        }

        case RETRO_ENVIRONMENT_SET_PERFORMANCE_LEVEL:
        {
            if (data == nullptr)
            {
                return null_data(note, note_size);
            }
            host->performance_level = *static_cast<const unsigned*>(data);
            std::snprintf(note, note_size, "%u", host->performance_level);
            return EnvironmentResult::handled;
        }

        case RETRO_ENVIRONMENT_SHUTDOWN:
        {
            host->shutdown_requested = true;
            return EnvironmentResult::handled;
        }

        // --- Paths ----------------------------------------------------------

        case RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY:
        {
            if (data == nullptr)
            {
                return null_data(note, note_size);
            }
            const char* directory = directory_or_null(host->system_directory);
            *static_cast<const char**>(data) = directory;
            std::snprintf(note, note_size, "%s", directory != nullptr ? directory : "(none)");
            return EnvironmentResult::handled;
        }

        case RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY:
        {
            if (data == nullptr)
            {
                return null_data(note, note_size);
            }
            const char* directory = directory_or_null(host->save_directory);
            *static_cast<const char**>(data) = directory;
            std::snprintf(note, note_size, "%s", directory != nullptr ? directory : "(none)");
            return EnvironmentResult::handled;
        }

        case RETRO_ENVIRONMENT_GET_LIBRETRO_PATH:
        {
            if (data == nullptr)
            {
                return null_data(note, note_size);
            }
            *static_cast<const char**>(data) = host->core_path;
            return EnvironmentResult::handled;
        }

        // --- Messages -------------------------------------------------------

        case RETRO_ENVIRONMENT_SET_MESSAGE:
        {
            if (data == nullptr)
            {
                return null_data(note, note_size);
            }
            const retro_message* message = static_cast<const retro_message*>(data);
            host_log(LogLevel::info, "[core message] %s", message->msg != nullptr ? message->msg : "");
            return EnvironmentResult::handled;
        }

        case RETRO_ENVIRONMENT_GET_MESSAGE_INTERFACE_VERSION:
        {
            if (data == nullptr)
            {
                return null_data(note, note_size);
            }
            // 1 = we accept SET_MESSAGE_EXT.
            *static_cast<unsigned*>(data) = 1;
            return EnvironmentResult::handled;
        }

        case RETRO_ENVIRONMENT_SET_MESSAGE_EXT:
        {
            if (data == nullptr)
            {
                return null_data(note, note_size);
            }
            const retro_message_ext* message = static_cast<const retro_message_ext*>(data);
            host_log(LogLevel::info, "[core message] %s", message->msg != nullptr ? message->msg : "");
            return EnvironmentResult::handled;
        }

        // --- Video geometry (sent by cores while running) -------------------

        case RETRO_ENVIRONMENT_SET_GEOMETRY:
        {
            if (data == nullptr)
            {
                return null_data(note, note_size);
            }
            // Only base size and aspect may change here; max size is fixed until SET_SYSTEM_AV_INFO.
            const retro_game_geometry* geometry = static_cast<const retro_game_geometry*>(data);
            host->av_info.geometry.base_width   = geometry->base_width;
            host->av_info.geometry.base_height  = geometry->base_height;
            host->av_info.geometry.aspect_ratio = geometry->aspect_ratio;
            std::snprintf(note, note_size, "%ux%u, aspect %.4f", geometry->base_width, geometry->base_height,
                          static_cast<double>(geometry->aspect_ratio));
            return EnvironmentResult::handled;
        }

        case RETRO_ENVIRONMENT_SET_SYSTEM_AV_INFO:
        {
            if (data == nullptr)
            {
                return null_data(note, note_size);
            }
            host->av_info = *static_cast<const retro_system_av_info*>(data);
            std::snprintf(note, note_size, "%ux%u @ %.3f fps", host->av_info.geometry.base_width,
                          host->av_info.geometry.base_height, host->av_info.timing.fps);
            return EnvironmentResult::handled;
        }

        // --- Runtime state queries -----------------------------------------

        case RETRO_ENVIRONMENT_GET_AUDIO_VIDEO_ENABLE:
        {
            if (data == nullptr)
            {
                return null_data(note, note_size);
            }
            // bit 0 = video, bit 1 = audio: both, except in a hidden frame
            // (rollback, run-ahead), which shows and plays nothing. Bit 2
            // (fast savestates) while states are saved for those.
            int flags = host->hidden ? 0 : 3;
            if (host->savestate_context != RETRO_SAVESTATE_CONTEXT_NORMAL)
            {
                flags |= 4;
            }
            *static_cast<int*>(data) = flags;
            return EnvironmentResult::handled;
        }

        case RETRO_ENVIRONMENT_GET_TARGET_SAMPLE_RATE:
        {
            if (host->target_sample_rate == 0)
            {
                std::snprintf(note, note_size, "not answered (no rate configured)");
                return EnvironmentResult::not_supported;
            }
            if (data == nullptr)
            {
                return null_data(note, note_size);
            }
            *static_cast<unsigned*>(data) = host->target_sample_rate;
            std::snprintf(note, note_size, "%u Hz", host->target_sample_rate);
            return EnvironmentResult::handled;
        }

        case RETRO_ENVIRONMENT_GET_FASTFORWARDING:
        {
            if (data == nullptr)
            {
                return null_data(note, note_size);
            }
            *static_cast<bool*>(data) = host->fast_forwarding;
            return EnvironmentResult::handled;
        }

        // --- Input -----------------------------------------------------------

        case RETRO_ENVIRONMENT_SET_KEYBOARD_CALLBACK:
        {
            if (data == nullptr)
            {
                return null_data(note, note_size);
            }
            host->keyboard_callback = static_cast<const retro_keyboard_callback*>(data)->callback;
            std::snprintf(note, note_size, "%s", host->keyboard_callback != nullptr ? "registered" : "cleared");
            return EnvironmentResult::handled;
        }

        case RETRO_ENVIRONMENT_GET_INPUT_BITMASKS:
        {
            // Specified with NULL data: returning true IS the answer
            // ("RETRO_DEVICE_ID_JOYPAD_MASK works"). No data check on purpose.
            return EnvironmentResult::handled;
        }

        // --- Core options (libretro_options.h) ------------------------------
        //
        // Every format is accepted whatever version we report: the reported
        // version only steers which one the core picks (see LibretroHostConfig).

        case RETRO_ENVIRONMENT_GET_CORE_OPTIONS_VERSION:
        {
            if (host->core_options_version == 0)
            {
                // Pretending to be a frontend from before options v1: it wouldn't know this command.
                std::snprintf(note, note_size, "not answered (configured as a v0 frontend)");
                return EnvironmentResult::not_supported;
            }
            if (data == nullptr)
            {
                return null_data(note, note_size);
            }
            *static_cast<unsigned*>(data) = host->core_options_version;
            std::snprintf(note, note_size, "%u", host->core_options_version);
            return EnvironmentResult::handled;
        }

        case RETRO_ENVIRONMENT_SET_VARIABLES:
        {
            if (data == nullptr)
            {
                return null_data(note, note_size);
            }
            if (!libretro_options_register_v0(&host->options, static_cast<const retro_variable*>(data)))
            {
                std::snprintf(note, note_size, "unusable definitions (see above)");
                return EnvironmentResult::rejected;
            }
            std::snprintf(note, note_size, "%u options (v0)", host->options.count);
            return EnvironmentResult::handled;
        }

        case RETRO_ENVIRONMENT_SET_CORE_OPTIONS:
        case RETRO_ENVIRONMENT_SET_CORE_OPTIONS_INTL:
        {
            if (data == nullptr)
            {
                return null_data(note, note_size);
            }
            // _INTL: English ("us") only for now; translations ("local") are ignored.
            const retro_core_option_definition* definitions =
                command == RETRO_ENVIRONMENT_SET_CORE_OPTIONS
                    ? static_cast<const retro_core_option_definition*>(data)
                    : static_cast<const retro_core_options_intl*>(data)->us;
            if (definitions == nullptr || !libretro_options_register_v1(&host->options, definitions))
            {
                std::snprintf(note, note_size, "unusable definitions");
                return EnvironmentResult::rejected;
            }
            std::snprintf(note, note_size, "%u options (v1)", host->options.count);
            return EnvironmentResult::handled;
        }

        case RETRO_ENVIRONMENT_SET_CORE_OPTIONS_V2:
        case RETRO_ENVIRONMENT_SET_CORE_OPTIONS_V2_INTL:
        {
            // libretro.h: NULL here is valid and means "remove all options".
            const retro_core_options_v2* definitions = nullptr;
            if (data != nullptr)
            {
                definitions = command == RETRO_ENVIRONMENT_SET_CORE_OPTIONS_V2
                                  ? static_cast<const retro_core_options_v2*>(data)
                                  : static_cast<const retro_core_options_v2_intl*>(data)->us;
            }
            if (definitions == nullptr)
            {
                libretro_options_free(&host->options);
                std::snprintf(note, note_size, "all options removed");
                return EnvironmentResult::handled;
            }
            if (!libretro_options_register_v2(&host->options, definitions))
            {
                std::snprintf(note, note_size, "unusable definitions");
                return EnvironmentResult::rejected;
            }
            std::snprintf(note, note_size, "%u options, %u categories (v2)", host->options.count,
                          host->options.category_count);
            return EnvironmentResult::handled;
        }

        case RETRO_ENVIRONMENT_SET_CORE_OPTIONS_DISPLAY:
        {
            if (data == nullptr)
            {
                return null_data(note, note_size);
            }
            const retro_core_option_display* display = static_cast<const retro_core_option_display*>(data);
            if (display->key == nullptr || !libretro_options_set_visible(&host->options, display->key, display->visible))
            {
                std::snprintf(note, note_size, "unknown option %s", display->key != nullptr ? display->key : "(null)");
                return EnvironmentResult::rejected;
            }
            std::snprintf(note, note_size, "%s %s", display->key, display->visible ? "shown" : "hidden");
            return EnvironmentResult::handled;
        }

        case RETRO_ENVIRONMENT_GET_VARIABLE:
        {
            if (data == nullptr)
            {
                return null_data(note, note_size);
            }
            retro_variable* variable = static_cast<retro_variable*>(data);
            variable->value = libretro_options_get(&host->options, variable->key);
            if (variable->value == nullptr)
            {
                // Not a core bug as such (it may ask before declaring), but worth seeing.
                std::snprintf(note, note_size, "%s: unknown option", variable->key != nullptr ? variable->key : "(null)");
                return EnvironmentResult::not_supported;
            }
            std::snprintf(note, note_size, "%s = %s", variable->key, variable->value);
            return EnvironmentResult::handled;
        }

        case RETRO_ENVIRONMENT_GET_VARIABLE_UPDATE:
        {
            if (data == nullptr)
            {
                return null_data(note, note_size);
            }
            bool updated = libretro_options_take_update(&host->options);
            *static_cast<bool*>(data) = updated;
            std::snprintf(note, note_size, "%s", updated ? "yes" : "no");
            return EnvironmentResult::handled;
        }

        // --- Input -----------------------------------------------------------

        case RETRO_ENVIRONMENT_SET_CONTROLLER_INFO:
        {
            if (data == nullptr)
            {
                return null_data(note, note_size);
            }
            if (host->port_layout.offered)
            {
                // The port adapters describe the ports (for this frontend); this
                // list is for frontends without the extension.
                std::snprintf(note, note_size, "not used: the core's port adapters describe its ports");
                return EnvironmentResult::handled;
            }
            uint32_t described = libretro_input_store_controllers(&host->input_info,
                                                                  static_cast<const retro_controller_info*>(data));
            uint32_t types = 0;
            for (uint32_t p = 0; p < host->input_info.port_count; p++)
            {
                types += host->input_info.ports[p].count;
            }
            std::snprintf(note, note_size, "%u port(s), %u device type(s)%s", described, types,
                          described > LIBRETRO_MAX_PORTS ? ", ports past the host's maximum dropped" : "");
            return EnvironmentResult::handled;
        }

        case RETRO_ENVIRONMENT_SET_INPUT_DESCRIPTORS:
        {
            if (data == nullptr)
            {
                return null_data(note, note_size);
            }
            uint32_t sent = libretro_input_store_descriptors(&host->input_info,
                                                             static_cast<const retro_input_descriptor*>(data));
            std::snprintf(note, note_size, "%u control(s)%s", sent,
                          sent > LIBRETRO_INPUT_DESCRIPTORS_MAX ? ", some dropped" : "");
            return EnvironmentResult::handled;
        }

        case RETRO_ENVIRONMENT_GET_INPUT_DEVICE_CAPABILITIES:
        {
            if (data == nullptr)
            {
                return null_data(note, note_size);
            }
            *static_cast<uint64_t*>(data) = LIBRETRO_INPUT_DEVICE_CAPABILITIES;
            std::snprintf(note, note_size, "joypad, analog, pointer, keyboard");
            return EnvironmentResult::handled;
        }

        case RETRO_ENVIRONMENT_GET_INPUT_MAX_USERS:
        {
            if (data == nullptr)
            {
                return null_data(note, note_size);
            }
            *static_cast<unsigned*>(data) = LIBRETRO_MAX_PORTS;
            std::snprintf(note, note_size, "%u", LIBRETRO_MAX_PORTS);
            return EnvironmentResult::handled;
        }

        case RETRO_ENVIRONMENT_GET_RUMBLE_INTERFACE:
        {
            if (data == nullptr)
            {
                return null_data(note, note_size);
            }
            static_cast<retro_rumble_interface*>(data)->set_rumble_state = libretro_host_rumble_callback();
            std::snprintf(note, note_size, "strong and weak, per port");
            return EnvironmentResult::handled;
        }

        // --- Content --------------------------------------------------------

        case RETRO_ENVIRONMENT_SET_SUBSYSTEM_INFO:
        {
            if (data == nullptr)
            {
                return null_data(note, note_size);
            }
            uint32_t sent = libretro_subsystem_store(&host->subsystems, static_cast<const retro_subsystem_info*>(data));
            size_t kept = host->subsystems.list.size();
            std::snprintf(note, note_size, "%u subsystem(s)%s", sent, kept < sent ? ", some dropped" : "");
            for (size_t i = 0; i < kept; i++)
            {
                const LibretroSubsystem* subsystem = &host->subsystems.list[i];
                host_log(host->peeking ? LogLevel::debug : LogLevel::info, "Subsystem %s: \"%s\", id %u, %zu content file(s)",
                         subsystem->ident, subsystem->description, subsystem->id, subsystem->roms.size());
            }
            return EnvironmentResult::handled;
        }

        // --- Informational: accepted, stored later when a feature needs it --

        case RETRO_ENVIRONMENT_SET_SERIALIZATION_QUIRKS:
        {
            if (data == nullptr)
            {
                return null_data(note, note_size);
            }
            // Kept: whether rollback / run-ahead by savestates can work with
            // this core (an INCOMPLETE state reinitializes the system...).
            host->serialization_quirks = *static_cast<const uint64_t*>(data);
            std::snprintf(note, note_size, "0x%llx", static_cast<unsigned long long>(host->serialization_quirks));
            return EnvironmentResult::handled;
        }

        case RETRO_ENVIRONMENT_GET_SAVESTATE_CONTEXT:
        {
            if (data == nullptr)
            {
                return null_data(note, note_size);
            }
            *static_cast<int*>(data) = static_cast<int>(host->savestate_context);
            return EnvironmentResult::handled;
        }

        case RETRO_ENVIRONMENT_SET_MEMORY_MAPS:
        case RETRO_ENVIRONMENT_SET_SUPPORT_ACHIEVEMENTS:
        {
            if (data == nullptr)
            {
                return null_data(note, note_size);
            }
            std::snprintf(note, note_size, "accepted, not used yet");
            return EnvironmentResult::handled;
        }

        // --- Multitap's own (libretro_extensions.h) ---------------------------

        case RETRO_ENVIRONMENT_EXT_SET_RUN_AHEAD:
        {
            if (data == nullptr)
            {
                return null_data(note, note_size);
            }
            host->core_run_ahead_max = *static_cast<const unsigned*>(data);
            std::snprintf(note, note_size, "the core's own run-ahead, up to %u frames: driven by the frontend",
                          host->core_run_ahead_max);
            return EnvironmentResult::handled;
        }

        case RETRO_ENVIRONMENT_EXT_GET_RUN_AHEAD:
        {
            if (data == nullptr)
            {
                return null_data(note, note_size);
            }
            *static_cast<unsigned*>(data) = libretro_host_run_ahead_applied(host);
            return EnvironmentResult::handled;
        }

        case RETRO_ENVIRONMENT_EXT_SET_PORT_ADAPTERS:
        {
            if (data == nullptr)
            {
                return null_data(note, note_size);
            }
            char error[128] = {};
            if (!libretro_ports_store(&host->port_layout, static_cast<const retro_ext_port_adapters*>(data), error,
                                      sizeof(error)))
            {
                std::snprintf(note, note_size, "unusable: %s", error);
                return EnvironmentResult::rejected;
            }
            uint32_t adapters = 0;
            for (uint32_t p = 0; p < host->port_layout.port_count; p++)
            {
                adapters += host->port_layout.ports[p].adapter_count;
            }
            std::snprintf(note, note_size, "%u physical port(s), %u adapter(s): driven by the frontend",
                          host->port_layout.port_count, adapters);
            return EnvironmentResult::handled;
        }

        case RETRO_ENVIRONMENT_EXT_SUGGEST_PORT_ADAPTERS:
        {
            if (data == nullptr)
            {
                return null_data(note, note_size);
            }
            if (!host->port_layout.offered)
            {
                std::snprintf(note, note_size, "no port adapters described first");
                return EnvironmentResult::rejected;
            }
            const unsigned* wanted = static_cast<const unsigned*>(data);
            char* cursor = note;
            size_t left = note_size;
            for (uint32_t p = 0; p < host->port_layout.port_count; p++)
            {
                host->port_layout.suggested[p] = wanted[p];
                int written = std::snprintf(cursor, left, "%sport %u: %u", p > 0 ? ", " : "", p + 1, wanted[p]);
                if (written > 0 && static_cast<size_t>(written) < left)
                {
                    cursor += written;
                    left -= static_cast<size_t>(written);
                }
            }
            return EnvironmentResult::handled;
        }

        case RETRO_ENVIRONMENT_EXT_SET_TEXT_INPUT:
        {
            if (data == nullptr)
            {
                return null_data(note, note_size);
            }
            return set_text_input(host, static_cast<const retro_ext_text_input*>(data), note, note_size);
        }

        default:
        {
            return EnvironmentResult::not_supported;
        }
    }
}

// libretro's names come from the generated table; Multitap's own calls
// (libretro_extensions.h) aren't in libretro.h.
static const char* command_name(uint32_t command)
{
    if (command == RETRO_ENVIRONMENT_EXT_SET_RUN_AHEAD)
    {
        return "EXT_SET_RUN_AHEAD";
    }
    if (command == RETRO_ENVIRONMENT_EXT_GET_RUN_AHEAD)
    {
        return "EXT_GET_RUN_AHEAD";
    }
    if (command == RETRO_ENVIRONMENT_EXT_SET_TEXT_INPUT)
    {
        return "EXT_SET_TEXT_INPUT";
    }
    if (command == RETRO_ENVIRONMENT_EXT_SET_PORT_ADAPTERS)
    {
        return "EXT_SET_PORT_ADAPTERS";
    }
    if (command == RETRO_ENVIRONMENT_EXT_SUGGEST_PORT_ADAPTERS)
    {
        return "EXT_SUGGEST_PORT_ADAPTERS";
    }
    return libretro_environment_command_name(command);
}

bool libretro_environment_handle(LibretroHost* host, uint32_t command, void* data)
{
    char note[256] = {};
    EnvironmentResult result = EnvironmentResult::not_supported;

    // Each handled command checks `data` itself: some commands are specified with
    // NULL data (SHUTDOWN, GET_INPUT_BITMASKS...), so a blanket check is wrong.
    result = handle_command(host, command, data, note, sizeof(note));

    // The low 16 bits are the command number; EXPERIMENTAL (0x10000) and PRIVATE (0x20000) sit above.
    uint32_t index = command & 0xFFFFu;
    uint32_t previous_calls = 0;
    if (index < 256)
    {
        previous_calls = host->environment_call_counts[index];
        host->environment_call_counts[index] = previous_calls + 1;
    }
    else if ((command & 0xFF00u) == 0x4D00u)   // Multitap's private calls (asked every frame: counted too)
    {
        previous_calls = host->extension_call_counts[command & 0xFFu];
        host->extension_call_counts[command & 0xFFu] = previous_calls + 1;
    }

    // First call of each command: info. Repeats: debug, so per-frame calls
    // (GET_VARIABLE_UPDATE...) don't flood the log. Rejections: always a warning.
    LogLevel level = previous_calls == 0 && !host->peeking ? LogLevel::info : LogLevel::debug;
    const char* outcome = "handled";
    if (result == EnvironmentResult::not_supported)
    {
        outcome = "NOT handled";
    }
    else if (result == EnvironmentResult::rejected)
    {
        outcome = "REJECTED";
        level = LogLevel::warning;
    }

    bool experimental = (command & RETRO_ENVIRONMENT_EXPERIMENTAL) != 0;
    host_log(level, "env %-34s (%3u%s) %-11s %s",
             command_name(command),
             index,
             experimental ? ", exp" : "",
             outcome,
             note);

    return result == EnvironmentResult::handled;
}
