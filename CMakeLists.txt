cmake_minimum_required(VERSION 3.22)
project(GhostDrums VERSION 1.0.8 LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(JUCE_SOURCE_DIR "" CACHE PATH "JUCE 9.0.5 source checkout")
set(GH_LIBRARY_DIR "${CMAKE_CURRENT_SOURCE_DIR}/Content" CACHE PATH "Packed Ghost library")
option(GH_BUILD_TESTS "Build offline tests" ON)
option(GH_OPTIMISE_FOR_IPAD "Tune builds for the A10X / iPadOS target" ON)
if(NOT JUCE_SOURCE_DIR AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/ThirdParty/JUCE/CMakeLists.txt")
    set(JUCE_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/ThirdParty/JUCE")
endif()
if(JUCE_SOURCE_DIR)
    add_subdirectory("${JUCE_SOURCE_DIR}" "${CMAKE_BINARY_DIR}/juce")
else()
    include(FetchContent)
    FetchContent_Declare(JUCE GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG 9.0.5 GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(JUCE)
endif()
if(CMAKE_SYSTEM_NAME STREQUAL "iOS")
    enable_language(OBJCXX)
    set(CMAKE_OBJCXX_STANDARD 17)
    if(NOT CMAKE_GENERATOR STREQUAL "Xcode")
        message(FATAL_ERROR "Use Xcode for the AUv3/container app.")
    endif()
    set(GH_FORMATS AUv3 Standalone)
else()
    set(GH_FORMATS VST3 Standalone)
endif()

juce_add_plugin(GhostDrums
    PRODUCT_NAME "GHOST HEALER DRUMS" COMPANY_NAME "MK Audio"
    BUNDLE_ID "audio.mk.ghostdrums" PLUGIN_MANUFACTURER_CODE Mkau PLUGIN_CODE Ghds
    IS_SYNTH TRUE NEEDS_MIDI_INPUT TRUE NEEDS_MIDI_OUTPUT TRUE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE COPY_PLUGIN_AFTER_BUILD FALSE
    NEEDS_WEB_BROWSER FALSE NEEDS_CURL FALSE BACKGROUND_AUDIO_ENABLED TRUE
    AU_MAIN_TYPE kAudioUnitType_MusicDevice VST3_CATEGORIES Instrument Drum
    FORMATS ${GH_FORMATS})
target_sources(GhostDrums PRIVATE Source/Library.cpp Source/BatteryImport.cpp Source/Processor.cpp Source/Editor.cpp)
target_compile_definitions(GhostDrums PUBLIC
    JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_USE_FLAC=0 JUCE_USE_OGGVORBIS=0 JUCE_USE_OPUS=0
    JUCE_USE_MP3AUDIOFORMAT=0 JUCE_USE_WINDOWS_MEDIA_FORMAT=0)
target_link_libraries(GhostDrums PRIVATE juce::juce_audio_utils
    PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_lto_flags
    juce::juce_recommended_warning_flags)

# Routing hub: audio pass-through effect with MIDI in/out/thru and CV conversion.
juce_add_plugin(GhostDrumsMidi
    PRODUCT_NAME "GHOST HEALER DRUMS MIDI" COMPANY_NAME "MK Audio"
    BUNDLE_ID "audio.mk.ghostdrumsmidi" PLUGIN_MANUFACTURER_CODE Mkau PLUGIN_CODE Ghmd
    IS_SYNTH FALSE NEEDS_MIDI_INPUT TRUE NEEDS_MIDI_OUTPUT TRUE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE COPY_PLUGIN_AFTER_BUILD FALSE
    NEEDS_WEB_BROWSER FALSE NEEDS_CURL FALSE
    AU_MAIN_TYPE kAudioUnitType_Effect VST3_CATEGORIES Fx
    FORMATS ${GH_FORMATS})
target_sources(GhostDrumsMidi PRIVATE Source/MidiProcessor.cpp Source/MidiEditor.cpp)
target_compile_definitions(GhostDrumsMidi PUBLIC
    JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_USE_FLAC=0 JUCE_USE_OGGVORBIS=0 JUCE_USE_OPUS=0
    JUCE_USE_MP3AUDIOFORMAT=0 JUCE_USE_WINDOWS_MEDIA_FORMAT=0)
target_link_libraries(GhostDrumsMidi PRIVATE juce::juce_audio_utils
    PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_lto_flags
    juce::juce_recommended_warning_flags)
if(WIN32)
    target_sources(GhostDrums PRIVATE Source/WindowsMidiDrag.cpp Source/WindowsMidiClipboard.cpp)
    target_link_libraries(GhostDrums PRIVATE shell32 ole32)
endif()
if(CMAKE_SYSTEM_NAME STREQUAL "iOS")
    target_sources(GhostDrums PRIVATE Source/iOSMidiShare.mm Source/iOSMidiOutput.mm)
    target_link_libraries(GhostDrums PRIVATE "-framework CoreMIDI")
    set_source_files_properties(Source/iOSMidiShare.mm Source/iOSMidiOutput.mm PROPERTIES COMPILE_FLAGS "-fobjc-arc")
    set_target_properties(GhostDrums PROPERTIES ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/shared")
    foreach(t GhostDrums_AUv3 GhostDrums_Standalone GhostDrumsMidi_AUv3 GhostDrumsMidi_Standalone)
        set_target_properties(${t} PROPERTIES XCODE_ATTRIBUTE_TARGETED_DEVICE_FAMILY "1,2"
            XCODE_ATTRIBUTE_CODE_SIGN_STYLE Automatic)
        if(GH_OPTIMISE_FOR_IPAD)
            # iPadOS 17 target, arm64-only (A10X Fusion is arm64), no bitcode, strip dead code.
            set_target_properties(${t} PROPERTIES
                XCODE_ATTRIBUTE_IPHONEOS_DEPLOYMENT_TARGET "17.0"
                XCODE_ATTRIBUTE_ARCHS "arm64"
                XCODE_ATTRIBUTE_ONLY_ACTIVE_ARCH "NO"
                XCODE_ATTRIBUTE_ENABLE_BITCODE "NO"
                XCODE_ATTRIBUTE_DEAD_CODE_STRIPPING "YES"
                XCODE_ATTRIBUTE_CLANG_ENABLE_MODULES "YES"
                XCODE_ATTRIBUTE_GCC_GENERATE_DEBUGGING_SYMBOLS "NO")
        endif()
    endforeach()
endif()
if(EXISTS "${GH_LIBRARY_DIR}/catalog.json")
    if(CMAKE_SYSTEM_NAME STREQUAL "iOS")
        file(GLOB_RECURSE content_files CONFIGURE_DEPENDS "${GH_LIBRARY_DIR}/*")
        foreach(file IN LISTS content_files)
            file(RELATIVE_PATH relative "${GH_LIBRARY_DIR}" "${file}")
            get_filename_component(directory "${relative}" DIRECTORY)
            set_source_files_properties("${file}" PROPERTIES
                MACOSX_PACKAGE_LOCATION "Resources/GHLibrary/${directory}")
        endforeach()
        target_sources(GhostDrums_AUv3 PRIVATE ${content_files})
    else()
        add_custom_command(TARGET GhostDrums_VST3 POST_BUILD
            COMMAND "${CMAKE_COMMAND}" -E copy_directory "${GH_LIBRARY_DIR}"
            "$<TARGET_FILE_DIR:GhostDrums_VST3>/../Resources/GHLibrary" VERBATIM)
    endif()
else()
    message(STATUS "No library embedded yet. Build/run GHContentPack, then reconfigure.")
endif()
if(NOT CMAKE_SYSTEM_NAME STREQUAL "iOS")
    juce_add_console_app(GHContentPack PRODUCT_NAME "Ghost content packer")
    target_sources(GHContentPack PRIVATE Tools/ContentPack.cpp)
    target_link_libraries(GHContentPack PRIVATE juce::juce_core juce::juce_cryptography juce::juce_audio_basics)
    if(GH_BUILD_TESTS)
        enable_testing()
        juce_add_console_app(GhostTests PRODUCT_NAME "Ghost tests")
        target_sources(GhostTests PRIVATE Tests/GhostTests.cpp)
        target_include_directories(GhostTests PRIVATE Source)
        target_compile_definitions(GhostTests PRIVATE JUCE_MODAL_LOOPS_PERMITTED=1)
        target_link_libraries(GhostTests PRIVATE GhostDrums juce::juce_audio_utils)
        add_test(NAME GhostTests COMMAND GhostTests "${GH_LIBRARY_DIR}")
        juce_add_console_app(GhostMidiTests PRODUCT_NAME "Ghost MIDI plugin tests")
        target_sources(GhostMidiTests PRIVATE Tests/MidiPluginTests.cpp)
        target_include_directories(GhostMidiTests PRIVATE Source)
        target_link_libraries(GhostMidiTests PRIVATE GhostDrumsMidi juce::juce_audio_utils)
        add_test(NAME GhostMidiTests COMMAND GhostMidiTests)
    endif()
endif()
