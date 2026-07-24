
set(CommonFileList
    XcpConnector.cpp
    XcpCopyPages.cpp
    XcpOverEthernet.cpp
    XcpWrapper.cpp
)

target_sources(xilenv_core PRIVATE ${CommonFileList})
