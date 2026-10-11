idf_component_register(SRCS "health.c"
                            "supervisor.c"
                            "supervisor_logic.c"
                       INCLUDE_DIRS "include"
                       PRIV_INCLUDE_DIRS "private_include"
                       PRIV_REQUIRES hmi)
