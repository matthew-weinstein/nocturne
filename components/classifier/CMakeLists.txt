idf_component_register(SRCS "audio_features.c"
                       PRIV_INCLUDE_DIRS "private_include")

set_source_files_properties("audio_features.c" PROPERTIES COMPILE_OPTIONS "-O2")
