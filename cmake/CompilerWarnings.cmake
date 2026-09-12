# Shared compiler warning configuration for all Eclipse targets
function(eclipse_set_target_warnings target)
    if(MSVC)
        target_compile_options(${target} PRIVATE
            /W4 /permissive- /Zc:preprocessor
            /w14242 /w14254 /w14263 /w14265 /w14287 /w14296 /w14311
            /w14545 /w14546 /w14547 /w14549 /w14555 /w14640 /w14826 /w14905 /w14906 /w14928)
        if(ECLIPSE_WARNINGS_AS_ERRORS)
            target_compile_options(${target} PRIVATE /WX)
        endif()
    else()
        target_compile_options(${target} PRIVATE
            -Wall -Wextra -Wpedantic
            -Wshadow -Wnon-virtual-dtor -Wcast-align
            -Woverloaded-virtual -Wformat=2)
        if(ECLIPSE_WARNINGS_AS_ERRORS)
            target_compile_options(${target} PRIVATE -Werror)
        endif()
    endif()
endfunction()
