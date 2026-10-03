#include "PluginPaths.h"

#include <utility>

namespace ssb
{

juce::File webRoot()
{
    static const juce::File chosen = []
    {
        if (auto fromEnvironment = juce::SystemStats::getEnvironmentVariable ("RD_WEB_DIR", {});
            fromEnvironment.isNotEmpty())
        {
            const juce::File dir (fromEnvironment);
            if (dir.getChildFile ("index.html").existsAsFile())
                return dir;

            // A directory with no page in it is a typo, not a preference -- say so rather than
            // quietly serving the bundled copy while someone edits another.
            juce::Logger::writeToLog ("Roli Drummer: RD_WEB_DIR has no index.html: " + fromEnvironment);
        }

        // For a plugin this is the plugin's own binary, not the host's.
        const auto binary = juce::File::getSpecialLocation (juce::File::currentExecutableFile);

        // Bundles (.app, .component, .vst3 -- a .vst3 is a folder on Windows too):
        // .../Contents/MacOS/<binary> -> .../Contents/Resources/web
        const auto inBundle = binary.getParentDirectory().getSiblingFile ("Resources").getChildFile ("web");
        if (inBundle.getChildFile ("index.html").existsAsFile())
            return inBundle;

        // A Windows standalone is a bare .exe: <dir>/<binary>.exe -> <dir>/Resources/web
        const auto besideBinary = binary.getParentDirectory().getChildFile ("Resources").getChildFile ("web");
        if (besideBinary.getChildFile ("index.html").existsAsFile())
            return besideBinary;

        return inBundle;   // named in the log line below, which is the point of it
    }();

    static bool announced = false;
    if (! std::exchange (announced, true))
        juce::Logger::writeToLog ("Roli Drummer: serving the page from " + chosen.getFullPathName());

    return chosen;
}

} // namespace ssb
