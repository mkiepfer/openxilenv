if(UNIX) 
    target_sources(${XilEnvExtProcTargetName}
        PRIVATE Platform.c)
    if(${MY_CMAKE_SIZEOF_VOID_P} GREATER 4)
        target_sources(LinuxRemoteMasterCore PRIVATE
            Platform.c
            Compare2DoubleEqual.c
            Fifos.c
            VersionInfoSection.c)

        target_sources(LinuxRemoteMaster PRIVATE 
            VersionInfoSection.c) 
    
        target_sources(LinuxRemoteMaster.Out PRIVATE 
            VersionInfoSection.c) 

        target_sources(XilEnvRpc PRIVATE
            Platform.c)
    endif()
endif()

if(${MY_CMAKE_SIZEOF_VOID_P} GREATER 4)

    set(CmmonFileList
        CheckIfAlreadyRunning.c
        RunTimeMeasurement.c
        InitProcess.c
        StartExeAndWait.c
        Compare2DoubleEqual.c
        MainValues.c
        Platform.c
        UtilsWindow.c
        EnvironmentVariables.c
        ImExportDskFile.c
        Message.c
        ReadConfig.c
        Fifos.c
        ImExportVarProperties.c
        MyMemory.c
        ReplaceFuncWithProg.c
        TimeProcess.c
        Wildcards.c
        Files.c
        IniFileDontExist.c
        my_udiv128.c
        UniqueNumber.c
        WindowIniHelper.c)

    target_sources(xilenv_core PRIVATE ${CmmonFileList})

    # NO_GUI-dependent border files: compiled per front-end with that
    # front-end's NO_GUI state (see the XILENV CORE note in the top-level
    # CMakeLists.txt). ThrowError.c is used pervasively by the core, so the
    # core library carries an undefined reference resolved by the front-end.
    set(XilEnvBorderFileList
        ParseCommandLine.c
        StartupInit.c
        ThrowError.c)
    target_sources(XilEnv PRIVATE ${XilEnvBorderFileList})
    target_sources(XilEnvGui PRIVATE ${XilEnvBorderFileList})
endif()
