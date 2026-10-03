# Script mode: make DEST look like SRC, dropping anything matching EXCLUDE, and only writing files
# whose content actually changed, so an unchanged page does not restamp (and re-sign) the bundle.
#
#   cmake -DSRC=<dist> -DDEST=<bundle>/Contents/Resources/web -DEXCLUDE=<regex> -P CopyWeb.cmake
#
# "Look like" rather than "copy into": Vite names every asset with a hash of its contents, so each
# build writes index-<new hash>.js and the old one is never referenced again. A copy that only adds
# would keep every one of them in the bundle for good, so this prunes as well.

# Don't follow symlinks out of dist/ (a linked test corpus would end up inside the plugin).
cmake_policy(SET CMP0009 NEW)

file(GLOB_RECURSE _files RELATIVE "${SRC}" "${SRC}/*")

set(_wanted "")
set(_kept 0)
set(_skipped 0)

foreach(_f IN LISTS _files)
    if(EXCLUDE AND _f MATCHES "${EXCLUDE}")
        math(EXPR _skipped "${_skipped} + 1")
        continue()
    endif()
    get_filename_component(_dir "${DEST}/${_f}" DIRECTORY)
    file(MAKE_DIRECTORY "${_dir}")
    file(COPY_FILE "${SRC}/${_f}" "${DEST}/${_f}" ONLY_IF_DIFFERENT)
    list(APPEND _wanted "${_f}")
    math(EXPR _kept "${_kept} + 1")
endforeach()

# Anything in the bundle the page no longer asks for (excluded files included).
set(_removed 0)
if(EXISTS "${DEST}")
    file(GLOB_RECURSE _there RELATIVE "${DEST}" "${DEST}/*")
    foreach(_f IN LISTS _there)
        # list(FIND) rather than IN_LIST: a `cmake -P` script inherits no policy settings.
        list(FIND _wanted "${_f}" _at)
        if(_at LESS 0)
            file(REMOVE "${DEST}/${_f}")
            math(EXPR _removed "${_removed} + 1")
        endif()
    endforeach()
endif()

message(STATUS "web: ${_kept} files copied, ${_skipped} skipped, ${_removed} stale removed")
