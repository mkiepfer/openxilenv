set(CommonFileList
#    AzgEdicCfg.c
    CanDataBase.c
    CanRecorder.c
    ImportDbc.c
    ReadCanCfg.c
)

target_sources(xilenv_core PRIVATE ${CommonFileList})
