find_program(Vulkan_GLSLC_EXECUTABLE glslc
  HINTS /usr/bin /usr/local/bin $ENV{VULKAN_SDK}/bin/ $ENV{VULKAN_SDK}/bin32/ C:/VulkanSDK/1.3.283.0/bin
)

# Function to compile Vulkan shaders to SPIR-V
function(compile_shaders)
    # check if ShadersTarget is already defined
    if(TARGET ShadersTarget)
        return()
    endif()

    # Get the shader directory that is set by the user in the CMakeLists.txt
    set(shader_dir ${SHADER_DIR})
    if(NOT shader_dir)
        message(FATAL_ERROR "SHADER_DIR not set. Set SHADER_DIR to the path of your shader directory.")
    else ()
        message(STATUS "Shader directory: ${shader_dir}")
    endif()

    if(NOT Vulkan_GLSLC_EXECUTABLE)
        message(FATAL_ERROR "glslc executable not found. Set Vulkan_GLSLC_EXECUTABLE to the path of your glslc executable.")
    endif()
    message(STATUS "Found glslc executable: ${Vulkan_GLSLC_EXECUTABLE}")

    # Find all shader source files
    file(GLOB_RECURSE shader_sources
            "${shader_dir}/*.vert"
            "${shader_dir}/*.frag"
            "${shader_dir}/*.comp"
            "${shader_dir}/*.geom"
            "${shader_dir}/*.tesc"
            "${shader_dir}/*.tese"
            "${shader_dir}/*.mesh"
    )

    # Create a list to hold compiled shader outputs
    set(shader_outputs "")

    # Iterate over each shader source file
    foreach(shader_source ${shader_sources})
        # Determine output file name
        get_filename_component(shader_name ${shader_source} NAME_WE)
        get_filename_component(shader_extension ${shader_source} EXT)

        set(shader_output "${shader_dir}/${shader_name}${shader_extension}.spv")

        # Add a command to compile shader
        add_custom_command(
            OUTPUT ${shader_output}
            COMMAND ${Vulkan_GLSLC_EXECUTABLE} -o ${shader_output} 
                    --target-env=vulkan1.3
                    ${shader_source}
            DEPENDS ${shader_source}
            COMMENT "Compiling ${shader_source} to SPIR-V"
    )

        # Append compiled shader output to the list
        list(APPEND shader_outputs ${shader_output})

        message(STATUS "Adding shader: ${shader_source}")
    endforeach()

    # Add a custom target to trigger shader compilation
    add_custom_target(ShadersTarget DEPENDS ${shader_outputs})

    # Add the shaders target to default build
    add_dependencies(SlimeOdyssey ShadersTarget)
endfunction()
