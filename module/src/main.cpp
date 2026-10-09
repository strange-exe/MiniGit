#include "CLI.h"

#ifdef __MINGW32__
// MinGW's runtime expands wildcards in argv before main() runs, so
// `minigit ignore *.log` would arrive as "b.log". Patterns must reach us verbatim.
extern "C" { int _CRT_glob = 0; }
#endif

int main(int argc, char** argv) {
    // Self-install lives here, not in dispatch(), so tests that call dispatch()
    // never copy the test binary into the user's PATH.
    std::string cmd = argc > 1 ? argv[1] : "";
    if (cmd != "uninstall" && cmd != "--uninstall") minigit::CLI::autoInstall();
    return minigit::dispatch(argc, argv);
}
