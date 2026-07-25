/**
 * @file Src/GUI/ImGui/Framework/WindowTypeRegistry.h
 * @brief Owning singleton registry of all window types (replaces Qt loadAllPlugins).
 * @author Martin Kiepfer martin.kiepfer@karcher.com
 * @copyright Alfred Kaercher SE & Co. KG
 */

#ifndef SRC_GUI_IMGUI_FRAMEWORK_WINDOWTYPEREGISTRY_H
#define SRC_GUI_IMGUI_FRAMEWORK_WINDOWTYPEREGISTRY_H

#include <memory>
#include <vector>

#include "IWindowType.h"

namespace XilEnvImGui
{

/**
 * @class WindowTypeRegistry
 * @brief Central owning collection of the available window types.
 * @details Populated once at startup via RegisterBuiltinWindowTypes(). Used by the
 *          window menu (iterate) and by the config loader (look up by type name).
 * @warning Single-threaded: register at startup, then read-only on the render thread.
 */
class WindowTypeRegistry
{
public:
    /**
     * @brief Returns the process-wide registry instance.
     * @return Reference to the singleton.
     */
    static WindowTypeRegistry &GetInstance();

    /**
     * @brief Adds a window type to the registry (takes ownership).
     * @param[in] par_Type Type to register. Must not be nullptr.
     */
    void Register(std::unique_ptr<IWindowType> par_Type);

    /**
     * @brief Looks up a registered type by its persistent type name.
     * @param[in] par_TypeName Type name to search for. Must not be nullptr.
     * @return Non-owning pointer to the type, or nullptr if not found.
     */
    [[nodiscard]] IWindowType *FindByTypeName(const char *par_TypeName) const;

    /**
     * @brief Provides read access to all registered types (for menu building).
     * @return Const reference to the owning type vector.
     */
    [[nodiscard]] const std::vector<std::unique_ptr<IWindowType>> &GetTypes() const
    {
        return m_types;
    }

private:
    std::vector<std::unique_ptr<IWindowType>> m_types; /*!< Owning list of registered types. */
};

/**
 * @brief Registers every built-in window type with the WindowTypeRegistry.
 * @details Called once at startup (replaces the Qt MainWindow::loadAllPlugins()).
 */
void RegisterBuiltinWindowTypes();

} // namespace XilEnvImGui

#endif // SRC_GUI_IMGUI_FRAMEWORK_WINDOWTYPEREGISTRY_H
