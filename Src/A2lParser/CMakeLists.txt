set(CommonFileList
    A2LAccess.c
    A2LBuffer.c
    A2LConvert.c
    A2LParser.c
    A2LRecordLayout.c
    A2LTokenizer.c
    A2LValue.c
    A2LAccessData.c
    A2LCache.c
    A2LConvertToXcp.c
    A2LLinkThread.c
    A2LParserModuleIfData.c
    A2LUpdate.c
    S19BinFile.c
)

target_sources(xilenv_core PRIVATE ${CommonFileList})

# NO_GUI-dependent border file: compiled per front-end with that front-end's
# NO_GUI state (see the XILENV CORE note in the top-level CMakeLists.txt).
target_sources(XilEnv PRIVATE A2LLink.c)
target_sources(XilEnvGui PRIVATE A2LLink.c)
