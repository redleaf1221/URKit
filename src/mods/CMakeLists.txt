# Every directory under src/mods except the scaffold is a mod target.
file(GLOB URK_MOD_DIRECTORIES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/*")

foreach(urk_mod_dir IN LISTS URK_MOD_DIRECTORIES)
    if (NOT IS_DIRECTORY "${urk_mod_dir}")
        continue()
    endif()
    get_filename_component(urk_mod_name "${urk_mod_dir}" NAME)
    if (urk_mod_name STREQUAL "_template")
        continue()
    endif()

    # Per-mod switches live in the mod directory, so `urk new --no-imgui` is real
    # and does not need a repository-wide reconfigure to change.
    set(URK_MOD_WITH_IMGUI ${URK_WITH_IMGUI})
    if (EXISTS "${urk_mod_dir}/mod_target.cmake")
        include("${urk_mod_dir}/mod_target.cmake")
    endif()

    if (URK_DEPLOY_DIR)
        set(urk_mod_deploy "${URK_DEPLOY_DIR}")
    else()
        unset(urk_mod_deploy)
    endif()

    # A mod may opt out of the overlay; it may not opt in when the repository
    # default is off, because the imgui target does not exist then.
    if (URK_WITH_IMGUI AND URK_MOD_WITH_IMGUI)
        urk_add_mod(${urk_mod_name} WITH_IMGUI DEPLOY_DIR "${urk_mod_deploy}")
    else()
        urk_add_mod(${urk_mod_name} DEPLOY_DIR "${urk_mod_deploy}")
    endif()
endforeach()

