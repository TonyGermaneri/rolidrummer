# Put a freshly sealed bundle into a plugin folder, replacing what is there.
#
# Run as `cmake -DBUNDLE=<path> -DDEST=<dir> -P InstallBundle.cmake`, from the
# same target that copies the page in and re-signs -- which is the whole point
# of it existing. JUCE's own COPY_PLUGIN_AFTER_BUILD is a POST_BUILD step on the
# plugin target, and the page is copied in by a target that depends on the
# plugin, so JUCE always copied the bundle as it stood *before* the page was
# refreshed. Every install carried the previous build's interface, silently,
# and the build tree looked correct the whole time.
if(NOT EXISTS "${BUNDLE}")
    message(FATAL_ERROR "nothing to install: no bundle at ${BUNDLE}")
endif()

get_filename_component(_name "${BUNDLE}" NAME)

# Removed rather than copied over: a bundle that is replaced file by file keeps
# whatever the last build left behind, and a stale file inside a signed bundle
# breaks the seal rather than being ignored.
file(REMOVE_RECURSE "${DEST}/${_name}")
file(MAKE_DIRECTORY "${DEST}")
# `file(COPY)` and not `copy_directory`: it keeps symlinks as symlinks and
# permissions as they were, which a bundle depends on.
file(COPY "${BUNDLE}" DESTINATION "${DEST}")

message(STATUS "installed ${_name} into ${DEST}")
