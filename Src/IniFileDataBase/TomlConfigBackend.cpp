#include "TomlConfigBackend.h"

#include <cstdlib>
#include <cstring>
#include <sstream>
#include <string>
#include <string_view>

// toml++ in error-code mode (no exceptions), matching the project's no-exceptions preference.
#define TOML_EXCEPTIONS 0
#include <toml++/toml.hpp>

namespace {

toml::table *GetOrCreateSection(toml::table *par_Root, const char *par_Section)
{
    if (auto *node = par_Root->get(par_Section); (node != nullptr) && node->is_table())
    {
        return node->as_table();
    }
    auto result = par_Root->insert_or_assign(par_Section, toml::table{});
    return result.first->second.as_table();
}

// Render any TOML scalar as the plain text the in-memory `key=value` model expects.
// Values written by this backend are always strings; this also tolerates hand-edited
// typed TOML (integers, floats, booleans) by stringifying them.
void ValueToString(const toml::node &par_Node, std::string &ret_Value)
{
    if (auto s = par_Node.value<std::string>())
    {
        ret_Value = *s;
    }
    else if (auto i = par_Node.value<int64_t>())
    {
        ret_Value = std::to_string(*i);
    }
    else if (auto b = par_Node.value<bool>())
    {
        ret_Value = *b ? "1" : "0";
    }
    else if (auto d = par_Node.value<double>())
    {
        std::ostringstream oss;
        oss << *d;
        ret_Value = oss.str();
    }
    else if (auto dt = par_Node.value<toml::date_time>())
    {
        std::ostringstream oss;
        oss << *dt;
        ret_Value = oss.str();
    }
    else if (auto dd = par_Node.value<toml::date>())
    {
        std::ostringstream oss;
        oss << *dd;
        ret_Value = oss.str();
    }
    else if (auto tt = par_Node.value<toml::time>())
    {
        std::ostringstream oss;
        oss << *tt;
        ret_Value = oss.str();
    }
    else
    {
        // Arrays / tables never occur in the flat key=value model this backend writes.
        ret_Value.clear();
    }
}

} // namespace

extern "C" void *TomlConfigBackendCreate(void)
{
    return new (std::nothrow) toml::table();
}

extern "C" void TomlConfigBackendFree(void *par_Handle)
{
    delete static_cast<toml::table *>(par_Handle);
}

extern "C" void TomlConfigBackendAddSection(void *par_Handle, const char *par_Section)
{
    if ((par_Handle == nullptr) || (par_Section == nullptr))
    {
        return;
    }
    GetOrCreateSection(static_cast<toml::table *>(par_Handle), par_Section);
}

extern "C" void TomlConfigBackendAddEntry(void *par_Handle, const char *par_Section,
                                          const char *par_KeyValueLine)
{
    if ((par_Handle == nullptr) || (par_Section == nullptr) || (par_KeyValueLine == nullptr))
    {
        return;
    }
    toml::table *section = GetOrCreateSection(static_cast<toml::table *>(par_Handle), par_Section);

    const char *equals = std::strchr(par_KeyValueLine, '=');
    std::string key;
    std::string value;
    if (equals != nullptr)
    {
        key.assign(par_KeyValueLine, static_cast<size_t>(equals - par_KeyValueLine));
        value.assign(equals + 1);
    }
    else
    {
        key.assign(par_KeyValueLine);
    }
    section->insert_or_assign(key, value);
}

extern "C" char *TomlConfigBackendSerialize(void *par_Handle)
{
    if (par_Handle == nullptr)
    {
        return nullptr;
    }
    std::ostringstream oss;
    oss << *static_cast<toml::table *>(par_Handle);
    const std::string text = oss.str();

    char *out = static_cast<char *>(std::malloc(text.size() + 1u));
    if (out != nullptr)
    {
        std::memcpy(out, text.c_str(), text.size() + 1u);
    }
    return out;
}

extern "C" void TomlConfigBackendFreeString(char *par_Text)
{
    std::free(par_Text);
}

extern "C" int TomlConfigBackendParseBuffer(const char *par_Buffer, size_t par_Length,
                                            TomlEntryCallback par_Callback, void *par_Ctx)
{
    if ((par_Buffer == nullptr) || (par_Callback == nullptr))
    {
        return -1;
    }

    toml::parse_result result = toml::parse(std::string_view(par_Buffer, par_Length));
    if (!result)
    {
        return -1;
    }

    const toml::table &root = result.table();
    for (auto &&[sectionKey, sectionValue] : root)
    {
        if (!sectionValue.is_table())
        {
            // Our format nests every entry under a section table; skip stray scalars.
            continue;
        }
        const std::string section(sectionKey.str());
        for (auto &&[entryKey, entryValue] : *sectionValue.as_table())
        {
            const std::string key(entryKey.str());
            std::string value;
            ValueToString(entryValue, value);
            if (par_Callback(section.c_str(), key.c_str(), value.c_str(), par_Ctx) != 0)
            {
                return -2;
            }
        }
    }
    return 0;
}
