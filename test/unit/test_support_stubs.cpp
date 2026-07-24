// Link-support stubs for the unit-test binary.
//
// The test binary reuses the GUI-free Console seam implementation but excludes
// Console/Main.cpp (its main() would clash with gtest_main). A few symbols
// defined there are still referenced by the Console stubs; provide no-op
// definitions so the test binary links. These are never exercised by the tests.

// Defined in Src/GUI/Console/Main.cpp (C++ linkage). Referenced by
// Src/GUI/Console/MainWindow.cpp::CloseFromOtherThread.
void terminate_main_loop(int /*par_ExitCode*/, int /*par_ExitCodeValid*/)
{
}
