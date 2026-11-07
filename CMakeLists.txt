cmake_minimum_required(VERSION 3.22)

# --- Réglages ----------------------------------------------------------------
set(RYTHM_PRODUCT_NAME "Rythm")             # nom du plugin dans le DAW, et nom du fichier .vst3
set(RYTHM_COMPANY_NAME "Phragma")            # nom affiché comme éditeur dans le DAW
set(RYTHM_BUNDLE_ID    "com.phragma.rythm")
# -----------------------------------------------------------------------------
# Ne changer ni PLUGIN_MANUFACTURER_CODE ni PLUGIN_CODE : ce sont eux (et pas le nom)
# qui permettent aux DAW de retrouver le plugin dans les projets.

# macOS : binaire universel (Apple Silicon + Intel), compatible macOS 10.13+
set(CMAKE_OSX_DEPLOYMENT_TARGET "10.13" CACHE STRING "Version minimale de macOS")
if(NOT DEFINED CMAKE_OSX_ARCHITECTURES)
    set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "Architectures macOS")
endif()

# Windows : runtime C++ lié en statique -> aucun "Visual C++ Redistributable" à installer
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

project(Rythm VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

option(RYTHM_COPY_AFTER_BUILD "Copier le plugin dans le dossier VST3 du système après la compilation" OFF)
option(RYTHM_BUILD_TESTS "Compiler les tests" OFF)

# --- JUCE (téléchargé automatiquement) ---------------------------------------
# Pour utiliser une copie locale : -DFETCHCONTENT_SOURCE_DIR_JUCE=/chemin/vers/JUCE
include(FetchContent)
FetchContent_Declare(JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG        8.0.15
    GIT_SHALLOW    TRUE)
FetchContent_MakeAvailable(JUCE)

set(RYTHM_DEFINITIONS
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_PLUGINHOST_VST3=1
    RYTHM_VERSION="${PROJECT_VERSION}")

# --- Plugin ------------------------------------------------------------------
# Instrument (pour pouvoir charger un synthé dedans) qui sort aussi son MIDI vers l'hôte.
juce_add_plugin(Rythm
    PRODUCT_NAME                "${RYTHM_PRODUCT_NAME}"
    COMPANY_NAME                "${RYTHM_COMPANY_NAME}"
    BUNDLE_ID                   "${RYTHM_BUNDLE_ID}"
    PLUGIN_MANUFACTURER_CODE    Phrg
    PLUGIN_CODE                 Rytm
    FORMATS                     VST3
    VST3_CATEGORIES             Instrument Generator
    IS_SYNTH                    TRUE
    NEEDS_MIDI_INPUT            TRUE
    NEEDS_MIDI_OUTPUT           TRUE
    IS_MIDI_EFFECT              FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD     ${RYTHM_COPY_AFTER_BUILD})

target_sources(Rythm PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp)

# Police de l'interface (Chakra Petch, licence OFL : voir Assets/ChakraPetch-OFL.txt), embarquée dans le plugin
juce_add_binary_data(RythmAssets SOURCES
    Assets/ChakraPetch-SemiBold.ttf
    Assets/ChakraPetch-Bold.ttf)
set_target_properties(RythmAssets PROPERTIES POSITION_INDEPENDENT_CODE TRUE)

target_compile_definitions(Rythm PUBLIC ${RYTHM_DEFINITIONS})

target_link_libraries(Rythm
    PRIVATE
        juce::juce_audio_processors
        RythmAssets
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)

# --- Tests (optionnels) ------------------------------------------------------
if(RYTHM_BUILD_TESTS)
    enable_testing()

    # Moteur seul, sans JUCE
    add_executable(RythmEngineTests Tests/EngineTests.cpp)
    if(NOT MSVC)
        target_compile_options(RythmEngineTests PRIVATE -O2)
    endif()
    target_include_directories(RythmEngineTests PRIVATE Source)
    add_test(NAME engine COMMAND RythmEngineTests)

    # Plugin complet (paramètres, MIDI produit, instrument chargé, état, interface)
    juce_add_console_app(RythmPluginTests PRODUCT_NAME "RythmPluginTests")
    target_sources(RythmPluginTests PRIVATE
        Tests/PluginTests.cpp
        Source/PluginProcessor.cpp
        Source/PluginEditor.cpp)
    target_compile_definitions(RythmPluginTests PRIVATE ${RYTHM_DEFINITIONS} JUCE_MODAL_LOOPS_PERMITTED=1 RYTHM_TESTS=1
        JucePlugin_Name="Rythm"
        RYTHM_TEST_SYNTH_DIR="${CMAKE_BINARY_DIR}/RythmTestSynth_artefacts/$<CONFIG>/VST3")
    target_link_libraries(RythmPluginTests PRIVATE
        juce::juce_audio_processors
        RythmAssets
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)
    add_test(NAME plugin COMMAND RythmPluginTests)

    # Instrument de test chargé par les tests dans Rythm
    juce_add_plugin(RythmTestSynth
        PRODUCT_NAME                "RythmTestSynth"
        COMPANY_NAME                "PhragmaTests"
        PLUGIN_MANUFACTURER_CODE    PhrT
        PLUGIN_CODE                 RyTs
        FORMATS                     VST3
        IS_SYNTH                    TRUE
        NEEDS_MIDI_INPUT            TRUE
        COPY_PLUGIN_AFTER_BUILD     FALSE)
    target_sources(RythmTestSynth PRIVATE Tests/TestSynth.cpp)
    target_compile_definitions(RythmTestSynth PUBLIC JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_VST3_CAN_REPLACE_VST2=0)
    target_link_libraries(RythmTestSynth PRIVATE juce::juce_audio_processors PUBLIC juce::juce_recommended_config_flags)
    add_dependencies(RythmPluginTests RythmTestSynth_VST3)
endif()
