set(CommonFileList
    BaseMessages.c
    ExtProcessRefFilter.c
    PipeMessages.c
    ScBbCopyLists.c
    SchedEnableDisable.c
    SocketMessages.c
    VirtualNetwork.c
    ExternLoginTimeoutControl.c
    KillAllExternProcesses.c
    ProcessEquations.c
    SchedBarrier.c
    Scheduler.c
    UnixDomainSocketMessages.c
)

target_sources(xilenv_core PRIVATE ${CommonFileList})
