// One-shot migration tool: convert a legacy OpenXilEnv INI config to TOML.
//
// It parses the old INI syntax (case-sensitive sections/keys, ';' comments,
// '[section]', 'key=value') and re-emits the document through the SAME toml++
// backend the runtime now uses (Src/IniFileDataBase/TomlConfigBackend), so the
// converted file is byte-for-byte the format the engine writes itself.
//
// Usage: xilenv_ini2toml <input.ini> <output.toml>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

extern "C" {
#include "TomlConfigBackend.h"
}

namespace {

bool IsSpaceChar(char c)
{
    return ((c >= 9) && (c <= 13)) || (c == 32);
}

} // namespace

int main(int argc, char** argv)
{
    if (argc != 3)
    {
        std::fprintf(stderr, "Usage: %s <input.ini> <output.toml>\n", argv[0]);
        return 2;
    }

    std::FILE* in = std::fopen(argv[1], "rt");
    if (in == nullptr)
    {
        std::fprintf(stderr, "error: cannot open input '%s'\n", argv[1]);
        return 1;
    }

    void* toml = TomlConfigBackendCreate();
    if (toml == nullptr)
    {
        std::fclose(in);
        std::fprintf(stderr, "error: out of memory\n");
        return 1;
    }

    std::string currentSection;
    char line[1 << 20];   // 1 MiB: covers the long ByteImage hex lines
    while (std::fgets(line, static_cast<int>(sizeof(line)), in) != nullptr)
    {
        // strip trailing newline / whitespace
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
            TomlConfigBackendAddSection(toml, currentSection.c_str());
            continue;
        }
        // entry line: "key=value" — pass the raw line, the backend splits at first '='
        if (!currentSection.empty())
        {
            TomlConfigBackendAddEntry(toml, currentSection.c_str(), p);
        }
    }
    std::fclose(in);

    char* text = TomlConfigBackendSerialize(toml);
    TomlConfigBackendFree(toml);
    if (text == nullptr)
    {
        std::fprintf(stderr, "error: TOML serialization failed\n");
        return 1;
    }

    std::FILE* out = std::fopen(argv[2], "wt");
    if (out == nullptr)
    {
        TomlConfigBackendFreeString(text);
        std::fprintf(stderr, "error: cannot open output '%s'\n", argv[2]);
        return 1;
    }
    std::fputs("# This is an OpenXilEnv configuration file (TOML format).\n"
               "# Section and entry names are case sensitive.\n", out);
    std::fputs(text, out);
    std::fclose(out);
    TomlConfigBackendFreeString(text);

    std::printf("converted '%s' -> '%s'\n", argv[1], argv[2]);
    return 0;
}
