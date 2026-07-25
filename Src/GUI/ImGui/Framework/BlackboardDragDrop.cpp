#include "Framework/BlackboardDragDrop.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include "imgui.h"

extern "C" {
#include "Blackboard.h"
#include "BlackboardAccess.h"
}

namespace XilEnvImGui
{

namespace
{

/**
 * @brief Extracts the value for a "key=value;" entry from a Qt info string.
 * @return The value string, or empty if the key is absent.
 */
std::string GetInfo(const std::string &par_Infos, const std::string &par_Key)
{
    std::size_t pos{0};
    while (pos < par_Infos.size())
    {
        const std::size_t sep{par_Infos.find(';', pos)};
        const std::size_t end{(std::string::npos == sep) ? par_Infos.size() : sep};
        const std::string entry{par_Infos.substr(pos, end - pos)};
        const std::size_t eq{entry.find('=')};
        if (std::string::npos != eq)
        {
            if (0 == entry.compare(0, eq, par_Key))
            {
                return entry.substr(eq + 1);
            }
        }
        if (std::string::npos == sep)
        {
            break;
        }
        pos = sep + 1;
    }
    return std::string{};
}

} // namespace

std::string SerializeBlackboardVariablePayload(const BlackboardVariablePayload &par_Payload)
{
    std::string infos{};
    infos.append("name=").append(par_Payload.name).append(";");
    if (par_Payload.hasMinMax)
    {
        char buffer[128];
        std::snprintf(buffer, sizeof(buffer), "min_max=%g/%g;", par_Payload.minValue, par_Payload.maxValue);
        infos.append(buffer);
    }
    if (par_Payload.hasColor)
    {
        char buffer[64];
        std::snprintf(buffer, sizeof(buffer), "color=%d;", par_Payload.color);
        infos.append(buffer);
    }
    if (DragDropDisplayMode::Unknown != par_Payload.displayMode)
    {
        char buffer[64];
        std::snprintf(buffer, sizeof(buffer), "display_mode=%d;", static_cast<int>(par_Payload.displayMode));
        infos.append(buffer);
    }
    return infos;
}

bool ParseBlackboardVariablePayload(const void *par_Data, std::size_t par_Size,
                                    BlackboardVariablePayload &out_Payload)
{
    if ((nullptr == par_Data) || (0u == par_Size))
    {
        return false;
    }

    // Drop any trailing NUL that ImGui includes when the source appended one.
    std::size_t length{par_Size};
    const char *bytes{static_cast<const char *>(par_Data)};
    while ((0u < length) && ('\0' == bytes[length - 1u]))
    {
        --length;
    }
    const std::string infos{bytes, length};
    if (infos.empty())
    {
        return false;
    }

    out_Payload = BlackboardVariablePayload{};

    // Backward compatibility: a bare name (no "key=value;") is treated as the name.
    if (std::string::npos == infos.find('='))
    {
        out_Payload.name = infos;
        return !out_Payload.name.empty();
    }

    out_Payload.name = GetInfo(infos, "name");
    if (out_Payload.name.empty())
    {
        // Not the Qt format after all; fall back to the raw string as the name.
        out_Payload.name = infos;
        return !out_Payload.name.empty();
    }

    const std::string minMax{GetInfo(infos, "min_max")};
    if (!minMax.empty())
    {
        const std::size_t slash{minMax.find('/')};
        if (std::string::npos != slash)
        {
            out_Payload.minValue = std::atof(minMax.substr(0, slash).c_str());
            out_Payload.maxValue = std::atof(minMax.substr(slash + 1).c_str());
            out_Payload.hasMinMax = true;
        }
    }

    const std::string color{GetInfo(infos, "color")};
    if (!color.empty())
    {
        out_Payload.color = std::atoi(color.c_str());
        out_Payload.hasColor = true;
    }

    const std::string displayMode{GetInfo(infos, "display_mode")};
    if (!displayMode.empty())
    {
        out_Payload.displayMode = static_cast<DragDropDisplayMode>(std::atoi(displayMode.c_str()));
    }
    return true;
}

BlackboardVariablePayload MakeBlackboardVariablePayloadFromVid(const std::string &par_Name,
                                                               std::int32_t par_Vid,
                                                               DragDropDisplayMode par_DisplayMode)
{
    BlackboardVariablePayload payload{};
    payload.name = par_Name;
    payload.displayMode = par_DisplayMode;
    if (0 < par_Vid)
    {
        payload.minValue = get_bbvari_min(par_Vid);
        payload.maxValue = get_bbvari_max(par_Vid);
        payload.hasMinMax = true;
        payload.color = get_bbvari_color(par_Vid);
        payload.hasColor = true;
    }
    return payload;
}

void SetBlackboardVariableDragPayload(const BlackboardVariablePayload &par_Payload)
{
    const std::string infos{SerializeBlackboardVariablePayload(par_Payload)};
    // Include the terminating NUL so name-only targets can read a C-string safely.
    ImGui::SetDragDropPayload(BBVAR_PAYLOAD_TYPE, infos.c_str(), infos.size() + 1u);
    ImGui::TextUnformatted(par_Payload.name.c_str());
}

bool AcceptBlackboardVariableDrop(BlackboardVariablePayload &out_Payload)
{
    const ImGuiPayload *payload{ImGui::AcceptDragDropPayload(BBVAR_PAYLOAD_TYPE)};
    if (nullptr == payload)
    {
        return false;
    }
    return ParseBlackboardVariablePayload(payload->Data, static_cast<std::size_t>(payload->DataSize),
                                          out_Payload);
}

} // namespace XilEnvImGui
