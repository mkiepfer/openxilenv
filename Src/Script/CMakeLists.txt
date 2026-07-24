set(CommonFileList
    BaseCmd.cpp
    CmdTable.cpp
    DefLocalsTree.cpp
    FileCache.cpp
    ParamList.cpp
    Proc.cpp
    Stack.cpp
    Breakpoints.cpp
    CompilerTree.cpp
    Executor.cpp
    FormatMessageOutput.cpp
    Parser.cpp
    Script.cpp
    ScriptErrorMsgDlg.cpp
    Tokenizer.cpp
    InterfaceToScript.c
    ScriptChangeSettings.c
    ScriptDebugFile.c
    ScriptErrorFile.c
    ScriptHtmlFunctions.c
    ScriptList.c
    ScriptMessageFile.c
    ScriptStartExe.c
    ToParseFileStack.cpp
)

target_sources(xilenv_core PRIVATE ${CommonFileList})

add_subdirectory("Commands")
