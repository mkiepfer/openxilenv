#include "Widgets/UserControl/UserControlElement.h"

#include <cstdlib>
#include <string>
#include <utility>

extern "C" {
#include "IniDataBase.h"
}

namespace XilEnvImGui
{

namespace
{

/**
 * @brief Parses an unsigned integer literal, returning 0 on any failure (Qt toUInt() semantics).
 */
int ParseUInt(const std::string &par_Value)
{
    char *end{nullptr};
    const unsigned long parsed{std::strtoul(par_Value.c_str(), &end, 10)};
    if (end == par_Value.c_str())
    {
        return 0;
    }
    return static_cast<int>(parsed);
}

} // namespace

bool UserControlIEqualsAscii(const std::string &par_Value, const char *par_Literal)
{
    std::size_t index{0u};
    for (; index < par_Value.size(); ++index)
    {
        const char literalChar{par_Literal[index]};
        if ('\0' == literalChar)
        {
            return false;
        }
        char valueChar{par_Value[index]};
        char cmpChar{literalChar};
        if (('A' <= valueChar) && ('Z' >= valueChar))
        {
            valueChar = static_cast<char>(valueChar - 'A' + 'a');
        }
        if (('A' <= cmpChar) && ('Z' >= cmpChar))
        {
            cmpChar = static_cast<char>(cmpChar - 'A' + 'a');
        }
        if (valueChar != cmpChar)
        {
            return false;
        }
    }
    return '\0' == par_Literal[index];
}

void UserControlElement::ResetToDefault()
{
    m_color = 0;
    m_x = 0;
    m_y = 0;
    m_width = 1;
    m_height = 1;
}

bool UserControlElement::ParseOneBaseParameter(const std::string &par_Key, const std::string &par_Value)
{
    if (UserControlIEqualsAscii(par_Key, "x"))
    {
        m_x = ParseUInt(par_Value);
    }
    else if (UserControlIEqualsAscii(par_Key, "y"))
    {
        m_y = ParseUInt(par_Value);
    }
    else if (UserControlIEqualsAscii(par_Key, "width"))
    {
        m_width = ParseUInt(par_Value);
    }
    else if (UserControlIEqualsAscii(par_Key, "height"))
    {
        m_height = ParseUInt(par_Value);
    }
    else if (UserControlIEqualsAscii(par_Key, "color") || UserControlIEqualsAscii(par_Key, "c"))
    {
        m_color = ParseUInt(par_Value);
    }
    else
    {
        return false;
    }
    return true;
}

void UserControlElement::WriteToIni(const char *par_Section, const std::string &par_Entry,
                                    int par_FileDescriptor)
{
    std::string line{GetTypeString()};
    line.append(m_parameterLine);
    (void)IniFileDataBaseWriteString(par_Section, par_Entry.c_str(), line.c_str(), par_FileDescriptor);
    for (int index{0}; index < GetChildCount(); ++index)
    {
        UserControlElement *child{GetChild(index)};
        if (nullptr != child)
        {
            std::string childEntry{par_Entry};
            childEntry.append("_");
            childEntry.append(std::to_string(index));
            child->WriteToIni(par_Section, childEntry, par_FileDescriptor);
        }
    }
}

} // namespace XilEnvImGui
