if(UNIX) 
    target_sources(LinuxRemoteMasterCore PRIVATE
        ReadFromBlackboardPipe.c)
endif()
    
set(CommonFileList
    ReadFromBlackboardPipe.c
    TraceRecorder.c
    TraceWriteFile.c
    TraceWriteMdfFile.c
    TraceWriteMdf4File.c
)

target_sources(xilenv_core PRIVATE ${CommonFileList})
