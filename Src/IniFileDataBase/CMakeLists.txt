set(CommonFileList
    DeleteWindowFromIni.c
    IniDataBase.c
    IniDataBaseInOutputFilter.c
)

target_sources(xilenv_core PRIVATE ${CommonFileList})
