#include "WindowTypeRegistry.h"

#include <cstring>
#include <memory>
#include <utility>

#include "Widgets/Text/TextWindow.h"

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
    registry.Register(std::make_unique<TextWindowType>());
    // Further built-in types (Oscilloscope, Lamps, Slider, ...) follow in M4+.
}

} // namespace XilEnvImGui
