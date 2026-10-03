#include "check.h"

// The engine's tests. (juce_add_console_app wants a main of its own.)
int main()
{
    for (const auto& c : ssbtest::cases())
    {
        const auto before = ssbtest::failures();
        c.run();
        std::printf ("%s  %s\n", ssbtest::failures() == before ? "ok  " : "FAIL", c.name);
    }
    std::printf ("\n%zu tests, %d failed checks\n", ssbtest::cases().size(), ssbtest::failures());
    return ssbtest::failures() == 0 ? 0 : 1;
}
