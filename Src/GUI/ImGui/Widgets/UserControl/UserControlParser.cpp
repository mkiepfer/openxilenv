#include "Widgets/UserControl/UserControlParser.h"

#include <cctype>
#include <memory>
#include <string>
#include <utility>

extern "C" {
#include "IniDataBase.h"
}

namespace XilEnvImGui
{

std::unique_ptr<UserControlElement> UserControlParser::ParseOneLine(const std::string &par_Line)
{
    std::unique_ptr<UserControlElement> element;
    int pos{0};

    if (0 != (pos = UserControlGroup::Is(par_Line)))
    {
        element = std::make_unique<UserControlGroup>();
    }
    else if (0 != (pos = UserControlButton::Is(par_Line)))
    {
        element = std::make_unique<UserControlButton>();
    }

    if (nullptr != element)
    {
        element->ResetToDefault();
        ParseParameters(element.get(), par_Line, pos);
    }
    return element;
}

void UserControlParser::ParseParameters(UserControlElement *par_Element, const std::string &par_Line,
                                        int par_Pos)
{
    const std::string tail{par_Line.substr(static_cast<std::size_t>(par_Pos))};
    par_Element->SetParameterLine(tail);

    const int length{static_cast<int>(par_Line.size())};
    int index{par_Pos};
    for (; index < length; ++index)
    {
        if (0 == std::isspace(static_cast<unsigned char>(par_Line[static_cast<std::size_t>(index)])))
        {
            break;
        }
    }
    if ((index >= length) || ('(' != par_Line[static_cast<std::size_t>(index)]))
    {
        return;
    }

    int braceCounter{0};
    int startParameter{index};
    for (; index < length; ++index)
    {
        const char character{par_Line[static_cast<std::size_t>(index)]};
        if ('(' == character)
        {
            ++braceCounter;
            if (1 == braceCounter)
            {
                startParameter = index;
            }
        }
        else if (')' == character)
        {
            if (1 == braceCounter)
            {
                par_Element->ParseOneParameter(
                    par_Line.substr(static_cast<std::size_t>(startParameter + 1),
                                    static_cast<std::size_t>(index - startParameter - 1)));
                startParameter = index;
            }
            --braceCounter;
        }
        else if (',' == character)
        {
            if (1 == braceCounter)
            {
                par_Element->ParseOneParameter(
                    par_Line.substr(static_cast<std::size_t>(startParameter + 1),
                                    static_cast<std::size_t>(index - startParameter - 1)));
                startParameter = index;
            }
        }
    }
}

void UserControlParser::ParseGroupRecursive(UserControlElement *par_Parent, const char *par_Section,
                                            const std::string &par_Entry, int par_FileDescriptor)
{
    for (int index{0}; index < 10000; ++index) // max 10000 entries inside one root/group
    {
        std::string entry{par_Entry};
        entry.append("_");
        entry.append(std::to_string(index));

        char lineBuffer[2048]{};
        (void)IniFileDataBaseReadString(par_Section, entry.c_str(), "", lineBuffer, sizeof(lineBuffer),
                                        par_FileDescriptor);
        if ('\0' == lineBuffer[0])
        {
            break;
        }

        std::unique_ptr<UserControlElement> element{ParseOneLine(lineBuffer)};
        if (nullptr == element)
        {
            break;
        }
        UserControlElement *rawElement{element.get()};
        par_Parent->AddChild(std::move(element));
        if (rawElement->IsGroup())
        {
            ParseGroupRecursive(rawElement, par_Section, entry, par_FileDescriptor);
        }
    }
}

std::unique_ptr<UserControlRoot> UserControlParser::Parse(const char *par_Section, int par_FileDescriptor)
{
    auto root = std::make_unique<UserControlRoot>();
    root->ResetToDefault();
    ParseGroupRecursive(root.get(), par_Section, "x", par_FileDescriptor);
    return root;
}

} // namespace XilEnvImGui
