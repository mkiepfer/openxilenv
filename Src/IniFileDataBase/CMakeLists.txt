set(CommonFileList
    DeleteWindowFromIni.c
    IniDataBase.c
    IniDataBaseInOutputFilter.c
    TomlConfigBackend.cpp
)

target_sources(xilenv_core PRIVATE ${CommonFileList})
