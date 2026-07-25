# One-shot INI -> TOML config migration tool.
# @author Martin Kiepfer martin.kiepfer@karcher.com
# @copyright Alfred Kaercher SE & Co. KG

set(XILENV_INI2TOML_ROOT ${CMAKE_CURRENT_LIST_DIR})

add_executable(xilenv_ini2toml
    ${XILENV_INI2TOML_ROOT}/ini2toml.cpp
    ${CMAKE_SOURCE_DIR}/Src/IniFileDataBase/TomlConfigBackend.cpp)

target_include_directories(xilenv_ini2toml PRIVATE
    ${CMAKE_SOURCE_DIR}/Src/IniFileDataBase)

target_link_libraries(xilenv_ini2toml PRIVATE tomlplusplus::tomlplusplus)
