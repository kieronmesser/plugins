
# Enqueues all the Cmake variables starting with given prefix into given list
function (papillon_get_variables_with_prefix _prefix _varResult)
    get_cmake_property(_vars VARIABLES)
    string (REGEX MATCHALL "(^|;)${_prefix}[A-Za-z0-9_]*" _matchedVars "${_vars}")
    set (${_varResult} ${_matchedVars} PARENT_SCOPE)
endfunction()

# Prints all the Cmake variables starting with given prefix
function (papillon_print_variables_with_prefix _prefix)
    if(VERBOSE)
        papillon_get_variables_with_prefix(${_prefix} matchedVars)
        foreach (_var IN LISTS matchedVars)
            message("${_var}=${${_var}}")
        endforeach()
    endif(VERBOSE)
endfunction()

# Copies the binary file of given target to given directory at post-build time
function (papillon_copy_target_binary _target _destination_dir)
    add_custom_command(
        TARGET ${_target}
        POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory ${_destination_dir}
    )
    add_custom_command(
        TARGET ${_target}
        POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:${_target}> ${_destination_dir}
    )
endfunction()

