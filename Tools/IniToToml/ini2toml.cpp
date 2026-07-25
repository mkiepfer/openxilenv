// One-shot migration tool: convert a legacy OpenXilEnv INI config to TOML,
// split into an engine file and a layout file (engine/layout config split).
//
// It parses the old INI syntax (case-sensitive sections/keys, ';' comments,
// '[section]', 'key=value') and re-emits through the SAME toml++ backend the
// runtime uses (Src/IniFileDataBase/TomlConfigBackend). Sections whose name
// starts with "GUI/" go into the layout file, everything else into the engine
// file — matching the facade's section-name routing.
//
// Usage: xilenv_ini2toml <input.ini> <engine.toml> <layout.toml>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

extern "C" {
#include "TomlConfigBackend.h"
}

namespace {

// The legacy [BasicSettings] section historically mixed engine config with
// GUI/desktop state. These individual keys belong to the layout file and are
// re-homed into its "GUI/BasicSettings" section, matching the runtime code that
// reads/writes them via OPT_GUI_SECTION. Keep this list in sync with the
// OPT_GUI_SECTION usage in Src/Global/MainValues.c and the Qt GUI.
const char* const BASIC_SETTINGS_SECTION = "BasicSettings";
const char* const GUI_BASIC_SETTINGS_SECTION = "GUI/BasicSettings";

const char* const BASIC_SETTINGS_GUI_KEYS[] = {
    "xdim", "ydim", "maximized",
    "ControlpanelXpos", "ControlpanelYpos",
    "DarkMode", "HideControlPanel", "SelectedControl",
    "TextDefaultFont", "OscilloscopeDefaultFont", "OscilloscopeDefaultBufferDepth",
    "SelectedSheet",
    "MessageWindowStayOnTop", "MessageWindowXPosition", "MessageWindowYPosition",
    "MessageWindowXSize", "MessageWindowYSize", "MessageWindowMaxLines",
};

// Keys generated at runtime with a variable suffix (e.g. "Last Directory for *.dsk").
const char* const BASIC_SETTINGS_GUI_KEY_PREFIXES[] = {
    "Last Directory for ",
};

bool IsSpaceChar(char c)
{
    return ((c >= 9) && (c <= 13)) || (c == 32);
}

// Extract the key part of a "key=value" line (up to the first '='), matching the
// split TomlConfigBackendAddEntry performs, and report whether it is a GUI key.
bool IsBasicSettingsGuiKey(const char* par_KeyValueLine)
{
    const char* equals = std::strchr(par_KeyValueLine, '=');
    size_t keyLen = (equals != nullptr) ? static_cast<size_t>(equals - par_KeyValueLine)
                                        : std::strlen(par_KeyValueLine);
    std::string key(par_KeyValueLine, keyLen);

    for (const char* guiKey : BASIC_SETTINGS_GUI_KEYS)
    {
        if (key == guiKey)
        {
            return true;
        }
    }
    for (const char* prefix : BASIC_SETTINGS_GUI_KEY_PREFIXES)
    {
        if (key.rfind(prefix, 0) == 0)
        {
            return true;
        }
    }
    return false;
}

int WriteTomlFile(const char* par_Path, void* par_Toml)
{
    char* text = TomlConfigBackendSerialize(par_Toml);
    if (text == nullptr)
    {
        std::fprintf(stderr, "error: TOML serialization failed for '%s'\n", par_Path);
        return -1;
    }
    std::FILE* out = std::fopen(par_Path, "wt");
    if (out == nullptr)
    {
        TomlConfigBackendFreeString(text);
        std::fprintf(stderr, "error: cannot open output '%s'\n", par_Path);
        return -1;
    }
    std::fputs("# This is an OpenXilEnv configuration file (TOML format).\n"
               "# Section and entry names are case sensitive.\n", out);
    std::fputs(text, out);
    std::fclose(out);
    TomlConfigBackendFreeString(text);
    return 0;
}

} // namespace

int main(int argc, char** argv)
{
    if (argc != 4)
    {
        std::fprintf(stderr, "Usage: %s <input.ini> <engine.toml> <layout.toml>\n", argv[0]);
        return 2;
    }

    std::FILE* in = std::fopen(argv[1], "rt");
    if (in == nullptr)
    {
        std::fprintf(stderr, "error: cannot open input '%s'\n", argv[1]);
        return 1;
    }

    void* engineToml = TomlConfigBackendCreate();
    void* layoutToml = TomlConfigBackendCreate();
    if ((engineToml == nullptr) || (layoutToml == nullptr))
    {
        std::fclose(in);
        std::fprintf(stderr, "error: out of memory\n");
        return 1;
    }

    std::string currentSection;
    void* currentToml = engineToml;
    char line[1 << 20];   // 1 MiB: covers the long ByteImage hex lines
    while (std::fgets(line, static_cast<int>(sizeof(line)), in) != nullptr)
    {
        size_t len = std::strlen(line);
        while ((len > 0) && ((line[len - 1] == '\n') || (line[len - 1] == '\r')))
        {
            line[--len] = '\0';
        }

        const char* p = line;
        while (IsSpaceChar(*p))
        {
            ++p;
        }
        if ((*p == ';') || (*p == '\0'))
        {
            continue;   // comment or blank line
        }
        if (*p == '[')
        {
            ++p;
            const char* end = std::strchr(p, ']');
            currentSection.assign(p, (end != nullptr) ? static_cast<size_t>(end - p) : std::strlen(p));
            // "GUI/" sections belong to the layout file.
            currentToml = (currentSection.rfind("GUI/", 0) == 0) ? layoutToml : engineToml;
            TomlConfigBackendAddSection(currentToml, currentSection.c_str());
            continue;
        }
        // entry line: "key=value" — the backend splits at the first '='
        if (!currentSection.empty())
        {
            // Re-home the mixed-in GUI/desktop-state keys of the legacy
            // [BasicSettings] section into the layout file's GUI section.
            if ((currentSection == BASIC_SETTINGS_SECTION) && IsBasicSettingsGuiKey(p))
            {
                TomlConfigBackendAddEntry(layoutToml, GUI_BASIC_SETTINGS_SECTION, p);
            }
            else
            {
                TomlConfigBackendAddEntry(currentToml, currentSection.c_str(), p);
            }
        }
    }
    std::fclose(in);

    int rc = 0;
    if (WriteTomlFile(argv[2], engineToml) != 0) { rc = 1; }
    if (WriteTomlFile(argv[3], layoutToml) != 0) { rc = 1; }
    TomlConfigBackendFree(engineToml);
    TomlConfigBackendFree(layoutToml);

    if (rc == 0)
    {
        std::printf("converted '%s' -> engine '%s' + layout '%s'\n", argv[1], argv[2], argv[3]);
    }
    return rc;
}
