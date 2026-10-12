idf_component_register(SRCS "uploader.c"
                       INCLUDE_DIRS "include"
                       REQUIRES storage
                       PRIV_REQUIRES network)
