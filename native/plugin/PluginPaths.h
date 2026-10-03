#pragma once

#include <juce_core/juce_core.h>

namespace ssb
{

/**
    Where the built page is read from: $SSB_WEB_DIR if it is set and holds an index.html (so
    `npm run build` and reopening the editor is the whole iteration loop), otherwise Resources/web
    inside this bundle (or beside the binary, for a Windows standalone).

    Logged once per process: a blank editor is almost always a page that is not where the plugin
    looked, and that line answers it.
*/
juce::File webRoot();

} // namespace ssb
