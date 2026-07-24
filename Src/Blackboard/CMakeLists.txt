if(UNIX) 
    target_sources(LinuxRemoteMasterCore PRIVATE
                   Blackboard.c
                   BlackboardAccess.c
                   BlackboardHashIndex.c
                   BlackboardIniCache.c
                   BlackboardConversion.c
                   EquationParser.c
                   ExecutionStack.c
                   TextReplace.c)
endif()

set(CommonFileList
    BlackboardAccess.c
    BlackboardHashIndex.c
    BlackboardIniCleaner.c
    EquationParser.c
    ExportA2L.c
    TextReplace.c
    Blackboard.c
    EquationList.c
    ExecutionStack.c
    BlackboardConversion.c
)

target_sources(xilenv_core PRIVATE ${CommonFileList})


    
