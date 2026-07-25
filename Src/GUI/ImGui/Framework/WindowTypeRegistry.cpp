#include "WindowTypeRegistry.h"

#include <cstring>
#include <memory>
#include <utility>

#include "Widgets/Bargraph/BargraphWindow.h"
#include "Widgets/Enum/EnumWindow.h"
#include "Widgets/Knob/KnobWindow.h"
#include "Widgets/Lamps/LampsWindow.h"
#include "Widgets/Slider/SliderWindow.h"
#include "Widgets/Tacho/TachoWindow.h"
#include "Widgets/Text/TextWindow.h"
#include "Widgets/VariableList/VariableListWindow.h"

namespace XilEnvImGui
{

WindowTypeRegistry &WindowTypeRegistry::GetInstance()
{
    static WindowTypeRegistry s_instance;
    return s_instance;
}

void WindowTypeRegistry::Register(std::unique_ptr<IWindowType> par_Type)
{
    if (nullptr != par_Type)
    {
        m_types.push_back(std::move(par_Type));
    }
}

IWindowType *WindowTypeRegistry::FindByTypeName(const char *par_TypeName) const
{
    if (nullptr == par_TypeName)
    {
        return nullptr;
    }
    for (const std::unique_ptr<IWindowType> &type : m_types)
    {
        if (0 == std::strcmp(type->GetTypeName(), par_TypeName))
        {
            return type.get();
        }
    }
    return nullptr;
}

void RegisterBuiltinWindowTypes()
{
    WindowTypeRegistry &registry = WindowTypeRegistry::GetInstance();
    registry.Register(std::make_unique<VariableListWindowType>());
    registry.Register(std::make_unique<TextWindowType>());
    registry.Register(std::make_unique<SliderWindowType>());
    registry.Register(std::make_unique<EnumWindowType>());
    registry.Register(std::make_unique<BargraphWindowType>());
    registry.Register(std::make_unique<LampsWindowType>());
    registry.Register(std::make_unique<KnobWindowType>());
    registry.Register(std::make_unique<TachoWindowType>());
    // Further built-in types (Oscilloscope, ...) follow in M5+.
}

} // namespace XilEnvImGui
