/**
 * @file Src/GUI/ImGui/Framework/IWindowType.h
 * @brief Abstract window-type factory + metadata (replaces the Qt InterfaceWidgetPlugin).
 * @author Martin Kiepfer martin.kiepfer@karcher.com
 * @copyright Alfred Kaercher SE & Co. KG
 */

#ifndef SRC_GUI_IMGUI_FRAMEWORK_IWINDOWTYPE_H
#define SRC_GUI_IMGUI_FRAMEWORK_IWINDOWTYPE_H

#include <memory>
#include <string>

#include "IWindowInstance.h"
#include "WindowContext.h"

namespace XilEnvImGui
{

/**
 * @class IWindowType
 * @brief Metadata provider and factory for one kind of ImGui window.
 * @details Registered once with the WindowTypeRegistry. Mirrors the Qt
 *          MdiWindowType/InterfaceWidgetPlugin contract (update rate, type name,
 *          ini list name, menu entry, storage flag, factories).
 */
class IWindowType
{
public:
    virtual ~IWindowType() = default;

    /**
     * @brief Update rate at which instances of this type refresh their values.
     * @return The classified update rate.
     */
    [[nodiscard]] virtual UpdateRate GetUpdateRate() const = 0;

    /**
     * @brief Persistent type identifier written as the "type" ini entry.
     * @return Stable type name, e.g. "Text". Never nullptr.
     */
    [[nodiscard]] virtual const char *GetTypeName() const = 0;

    /**
     * @brief Ini section listing all windows of this type (Qt "GUI/AllTextWindows").
     * @return Ini list section name. Never nullptr.
     */
    [[nodiscard]] virtual const char *GetIniListName() const = 0;

    /**
     * @brief Human-readable label shown in the "new window" menu.
     * @return Menu entry text. Never nullptr.
     */
    [[nodiscard]] virtual const char *GetMenuEntryName() const = 0;

    /**
     * @brief Whether instances keep history that must persist across sheet switches.
     * @return true if the type has storage (e.g. oscilloscope), false otherwise.
     */
    [[nodiscard]] virtual bool HasStorage() const = 0;

    /**
     * @brief Creates a fresh, empty instance with the given title.
     * @param[in] par_Title Unique window title/name.
     * @return Owning instance. Never nullptr.
     */
    [[nodiscard]] virtual std::unique_ptr<IWindowInstance> CreateNew(const std::string &par_Title) = 0;

    /**
     * @brief Creates an instance and loads its configuration from an ini section.
     * @param[in] par_FileDescriptor Ini-database descriptor.
     * @param[in] par_Section Section holding the window config. Must not be nullptr.
     * @param[in] par_Name Unique window name.
     * @return Owning, configured instance. Never nullptr.
     */
    [[nodiscard]] virtual std::unique_ptr<IWindowInstance> CreateFromConfig(int par_FileDescriptor,
                                                                            const char *par_Section,
                                                                            const std::string &par_Name) = 0;
};

} // namespace XilEnvImGui

#endif // SRC_GUI_IMGUI_FRAMEWORK_IWINDOWTYPE_H
