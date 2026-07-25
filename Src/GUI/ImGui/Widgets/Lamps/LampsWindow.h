/**
 * @file Src/GUI/ImGui/Widgets/Lamps/LampsWindow.h
 * @brief ImGui port of the Qt Control-Lamps widget: a grid of value-colored indicators.
 * @author Martin Kiepfer martin.kiepfer@karcher.com
 * @copyright Alfred Kaercher SE & Co. KG
 */

#ifndef SRC_GUI_IMGUI_WIDGETS_LAMPS_LAMPSWINDOW_H
#define SRC_GUI_IMGUI_WIDGETS_LAMPS_LAMPSWINDOW_H

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "Framework/IWindowInstance.h"
#include "Framework/IWindowType.h"
#include "Framework/WindowContext.h"

namespace XilEnvImGui
{

/**
 * @class LampsWindowType
 * @brief Window type/factory for Control-Lamps windows (mirrors the Qt LampsType, Medium update).
 */
class LampsWindowType : public IWindowType
{
public:
    [[nodiscard]] UpdateRate GetUpdateRate() const override
    {
        return UpdateRate::Medium;
    }
    [[nodiscard]] const char *GetTypeName() const override
    {
        return "ControlLamps";
    }
    [[nodiscard]] const char *GetIniListName() const override
    {
        return "GUI/AllControlLampsViewWindows";
    }
    [[nodiscard]] const char *GetMenuEntryName() const override
    {
        return "Control Lamps";
    }
    [[nodiscard]] bool HasStorage() const override
    {
        return false;
    }
    [[nodiscard]] std::unique_ptr<IWindowInstance> CreateNew(const std::string &par_Title) override;
    [[nodiscard]] std::unique_ptr<IWindowInstance> CreateFromConfig(int par_FileDescriptor,
                                                                    const char *par_Section,
                                                                    const std::string &par_Name) override;
};

/**
 * @class LampsWindow
 * @brief One Control-Lamps window: a grid of shapes colored by each variable's enum color.
 * @details Display-only. Each lamp reads its variable's textreplace color for the
 *          current value (read_bbvari_textreplace) and paints an ImDrawList circle;
 *          the enum text is the label. Config round-trips to the Qt "Lamp_%i" /
 *          "Columns" / "Rows" / "BkColor" entries. The Qt QGraphicsView stencil shapes
 *          are approximated by filled circles.
 */
class LampsWindow : public IWindowInstance
{
public:
    /**
     * @brief Constructs a Control-Lamps window bound to its type and name.
     * @param[in] par_Type Owning window type. Must not be nullptr.
     * @param[in] par_Name Unique window name.
     */
    LampsWindow(IWindowType *par_Type, std::string par_Name);
    ~LampsWindow() override;

    [[nodiscard]] IWindowType *GetType() const override
    {
        return m_pType;
    }
    void RenderContent(const WindowContext &par_Context) override;
    void OnUpdateTick(const WindowContext &par_Context) override;
    void RenderSettingsPopup() override;
    void WriteConfig(int par_FileDescriptor, const char *par_Section) override;
    void ReadConfig(int par_FileDescriptor, const char *par_Section) override;
    void OnAcceptVariableDrop(const std::string &par_VariableName) override;

private:
    /**
     * @struct Lamp
     * @brief One lamp: a variable placed at a grid cell, colored by its enum state.
     */
    struct Lamp
    {
        std::string name{};           /*!< Variable name. */
        std::int32_t vid{0};          /*!< Cached VID (>0 when attached). */
        int row{0};                   /*!< Grid row. */
        int column{0};                /*!< Grid column. */
        int rowSpan{1};               /*!< Grid row span. */
        int columnSpan{1};            /*!< Grid column span. */
        std::string stencil{};        /*!< Stencil name (persisted, approximated as a circle). */
        std::uint32_t color{0};       /*!< Cached 0x00bbggrr foreground color. */
        std::string stateText{};      /*!< Cached enum state text. */
        bool valid{false};            /*!< Whether the last read produced a color. */
    };

    void AddLamp(const std::string &par_Name);
    void RemoveLamp(std::size_t par_Index);
    void RefreshLamp(Lamp &par_Lamp);

    IWindowType *m_pType{nullptr};       /*!< Owning window type. */
    std::vector<Lamp> m_lamps{};         /*!< Lamps in the grid. */
    int m_columns{2};                    /*!< Grid column count. */
    int m_rows{2};                       /*!< Grid row count. */
    std::uint32_t m_backgroundColor{0};  /*!< 0x00bbggrr scene background color. */
    char m_addBuffer[512]{};             /*!< Settings "add variable" text buffer. */
};

} // namespace XilEnvImGui

#endif // SRC_GUI_IMGUI_WIDGETS_LAMPS_LAMPSWINDOW_H
