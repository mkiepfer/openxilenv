set(CommonFileList
    GetElfSectionVersionInfos.c
    RemoteMasterBlackboard.c
    RemoteMasterCopyStartExecutable.c
    RemoteMasterMessage.c
    RemoteMasterScheduler.c
#    ReadDspConfig.c
    RemoteMasterCanFifo.c
    RemoteMasterNet.c
#    ReadFlexcardDigIOConfig.c
    RemoteMasterControlProcess.c
    RemoteMasterFiFo.c
    RemoteMasterOther.c
)

target_sources(xilenv_core PRIVATE ${CommonFileList})
