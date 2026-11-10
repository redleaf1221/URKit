# urk_add_mod(<name> [WITH_IMGUI] [DEPLOY_DIR <dir>])
#
# A mod is a normal CMake target in this repository: the scaffold in mods/<name>
# is ordinary source, not generated text. This function only wires include
# paths, the optional ImGui dependency and the deploy copy.
function(urk_add_mod name)
    cmake_parse_arguments(MOD "WITH_IMGUI" "DEPLOY_DIR" "" ${ARGN})

    set(mod_root "${URK_MODS_DIR}/${name}")
    if (NOT EXISTS "${mod_root}/mod")
        message(FATAL_ERROR "urk_add_mod(${name}): ${mod_root}/mod does not exist")
    endif()

    file(GLOB_RECURSE mod_sources CONFIGURE_DEPENDS
        "${mod_root}/mod/*.cpp"
        "${mod_root}/src/*.cpp"
    )

    set(with_imgui OFF)
    if (MOD_WITH_IMGUI)
        set(with_imgui ON)
        # Shared overlay sources. They are compiled into this mod, not linked as
        # a library, so the ImGui context, DX hooks and menu state stay per-DLL.
        # A mod that needs to change the overlay vendors a copy under its own
        # directory and sets URK_MOD_VENDORED_OVERLAY.
        if (URK_MOD_VENDORED_OVERLAY)
            file(GLOB URK_OVERLAY_SOURCES CONFIGURE_DEPENDS
                "${mod_root}/overlay/*.cpp"
                "${mod_root}/overlay/hooks/*.cpp"
            )
        else()
            file(GLOB URK_OVERLAY_SOURCES CONFIGURE_DEPENDS
                "${URK_ROOT_DIR}/src/overlay/*.cpp"
                "${URK_ROOT_DIR}/src/overlay/hooks/*.cpp"
            )
        endif()
        list(APPEND mod_sources ${URK_OVERLAY_SOURCES})
    else()
        # Drop the overlay sources; the rest of the mod never references them.
        list(FILTER mod_sources EXCLUDE REGEX "/mod/(ui|hooks)/(render_imgui_hook|dxgi_hook_discovery|dx11_state_guard|dx12_overlay_resources|dx11_viewport_swap_chain|win32_input_coordinates|win32_message_pump|win32_viewport_policy)\\.cpp$")
    endif()

    add_library(${name} SHARED ${mod_sources})
    target_compile_features(${name} PRIVATE cxx_std_20)
    target_include_directories(${name} PRIVATE
        "${mod_root}"
        "${mod_root}/mod"
        "${URK_ROOT_DIR}/src"
        "${URK_SDK_DIR}"
    )
    target_compile_definitions(${name} PRIVATE
        WIN32_LEAN_AND_MEAN
        NOMINMAX
        "URK_MOD_NAME=\"${name}\""
        "URK_WITH_IMGUI=$<BOOL:${with_imgui}>"
    )
    set_target_properties(${name} PROPERTIES PREFIX "" OUTPUT_NAME "${name}")

    if (with_imgui)
        target_link_libraries(${name} PRIVATE imgui d3d11 d3d12 dxgi opengl32)
    endif()

    if (MSVC)
        target_compile_options(${name} PRIVATE /W3 /permissive- /EHsc /utf-8)
    endif()
    urk_disable_vs_vcpkg_integration(${name})

    if (MOD_DEPLOY_DIR)
        add_custom_command(TARGET ${name} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E make_directory "${MOD_DEPLOY_DIR}"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "$<TARGET_FILE:${name}>" "${MOD_DEPLOY_DIR}/$<TARGET_FILE_NAME:${name}>"
            COMMENT "Deploying ${name}.dll to ${MOD_DEPLOY_DIR}"
        )
    endif()
endfunction()


