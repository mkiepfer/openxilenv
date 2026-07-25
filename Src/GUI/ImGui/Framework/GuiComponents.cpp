#include "Framework/GuiComponents.h"

#include <cctype>
#include <cfloat>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "imgui.h"

extern "C" {
#include "Blackboard.h"
}

namespace XilEnvImGui
{

namespace
{

constexpr int MAX_PRECISION{15};

/**
 * @brief Case-insensitive substring test used by the variable filter.
 */
bool ContainsCaseInsensitive(const std::string &par_Haystack, const std::string &par_Needle)
{
    if (par_Needle.empty())
    {
        return true;
    }
    const auto toLower{[](unsigned char c) { return static_cast<char>(std::tolower(c)); }};
    std::string haystack{par_Haystack};
    std::string needle{par_Needle};
    for (char &c : haystack)
    {
        c = toLower(static_cast<unsigned char>(c));
    }
    for (char &c : needle)
    {
        c = toLower(static_cast<unsigned char>(c));
    }
    return (std::string::npos != haystack.find(needle));
}

} // namespace

void EnumerateBlackboardVariables(std::vector<std::string> &out_Names)
{
    out_Names.clear();
    char nameBuffer[BBVARI_NAME_SIZE];
    int index{0};
    // read_next_blackboard_vari returns the next start index, or <0 when exhausted.
    while (0 <= (index = read_next_blackboard_vari(index, nameBuffer, sizeof(nameBuffer))))
    {
        out_Names.emplace_back(nameBuffer);
    }
}

bool DrawBlackboardSelector(const char *par_Label, char *par_Buffer, std::size_t par_BufferSize)
{
    if ((nullptr == par_Buffer) || (0u == par_BufferSize))
    {
        return false;
    }

    bool changed{false};

    ImGui::PushID(par_Label);
    const float buttonWidth{ImGui::GetFrameHeight()};
    ImGui::SetNextItemWidth(ImGui::CalcItemWidth() - buttonWidth - ImGui::GetStyle().ItemSpacing.x);
    if (ImGui::InputText("##bbsel_edit", par_Buffer, par_BufferSize))
    {
        changed = true;
    }
    ImGui::SameLine();
    if (ImGui::Button("v", ImVec2(buttonWidth, 0.0f)))
    {
        ImGui::OpenPopup("##bbsel_popup");
    }

    if (ImGui::BeginPopup("##bbsel_popup"))
    {
        static char s_filter[256]{};
        ImGui::SetNextItemWidth(280.0f);
        if (ImGui::IsWindowAppearing())
        {
            ImGui::SetKeyboardFocusHere();
        }
        ImGui::InputTextWithHint("##bbsel_filter", "filter...", s_filter, sizeof(s_filter));

        std::vector<std::string> names;
        EnumerateBlackboardVariables(names);

        ImGui::BeginChild("##bbsel_list", ImVec2(280.0f, 240.0f), true);
        const std::string filter{s_filter};
        for (const std::string &name : names)
        {
            if (!ContainsCaseInsensitive(name, filter))
            {
                continue;
            }
            if (ImGui::Selectable(name.c_str()))
            {
                std::snprintf(par_Buffer, par_BufferSize, "%s", name.c_str());
                changed = true;
                s_filter[0] = '\0';
                ImGui::CloseCurrentPopup();
            }
        }
        ImGui::EndChild();
        ImGui::EndPopup();
    }
    ImGui::PopID();

    return changed;
}

bool DrawPhysValueInput(const char *par_Label, double *par_Value, double par_MinValue, double par_MaxValue,
                        int par_Precision)
{
    if (nullptr == par_Value)
    {
        return false;
    }

    int precision{par_Precision};
    if (0 > precision)
    {
        precision = 0;
    }
    if (MAX_PRECISION < precision)
    {
        precision = MAX_PRECISION;
    }

    char format[16];
    std::snprintf(format, sizeof(format), "%%.%df", precision);

    bool changed{false};
    if (ImGui::InputDouble(par_Label, par_Value, 0.0, 0.0, format))
    {
        if (par_MaxValue > par_MinValue)
        {
            if (*par_Value < par_MinValue)
            {
                *par_Value = par_MinValue;
            }
            if (*par_Value > par_MaxValue)
            {
                *par_Value = par_MaxValue;
            }
        }
        changed = true;
    }
    return changed;
}

bool RenderForm(std::vector<FormField> &par_Fields)
{
    bool anyChanged{false};

    if (!ImGui::BeginTable("##form", 2,
                           ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_BordersInnerV))
    {
        return false;
    }
    ImGui::TableSetupColumn("##label", ImGuiTableColumnFlags_WidthFixed, 160.0f);
    ImGui::TableSetupColumn("##editor", ImGuiTableColumnFlags_WidthStretch);

    for (std::size_t i{0}; i < par_Fields.size(); ++i)
    {
        FormField &field{par_Fields[i]};
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(field.label.c_str());
        if (!field.tooltip.empty() && ImGui::IsItemHovered())
        {
            ImGui::SetTooltip("%s", field.tooltip.c_str());
        }

        ImGui::TableSetColumnIndex(1);
        ImGui::PushID(static_cast<int>(i));
        ImGui::SetNextItemWidth(-FLT_MIN);
        switch (field.kind)
        {
        case FormFieldKind::Int:
            if ((nullptr != field.pIntValue) && ImGui::InputInt("##v", field.pIntValue))
            {
                anyChanged = true;
            }
            break;
        case FormFieldKind::Double:
            if (nullptr != field.pDoubleValue)
            {
                if (DrawPhysValueInput("##v", field.pDoubleValue, field.minValue, field.maxValue,
                                       field.precision))
                {
                    anyChanged = true;
                }
            }
            break;
        case FormFieldKind::Text:
            if ((nullptr != field.pTextBuffer) && (0u < field.textBufferSize) &&
                ImGui::InputText("##v", field.pTextBuffer, field.textBufferSize))
            {
                anyChanged = true;
            }
            break;
        case FormFieldKind::Bool:
            if ((nullptr != field.pBoolValue) && ImGui::Checkbox("##v", field.pBoolValue))
            {
                anyChanged = true;
            }
            break;
        case FormFieldKind::Enum:
            if ((nullptr != field.pIntValue) && !field.enumLabels.empty())
            {
                std::vector<const char *> labels;
                labels.reserve(field.enumLabels.size());
                for (const std::string &label : field.enumLabels)
                {
                    labels.push_back(label.c_str());
                }
                if (ImGui::Combo("##v", field.pIntValue, labels.data(), static_cast<int>(labels.size())))
                {
                    anyChanged = true;
                }
            }
            break;
        default:
            break;
        }
        ImGui::PopID();
    }

    ImGui::EndTable();
    return anyChanged;
}

} // namespace XilEnvImGui
