idf_component_register(SRCS "nsi_sniffer_app.c"
                    INCLUDE_DIRS ".")
idf_build_set_property(COMPILE_DEFINITIONS "-DRMT_NSI_SNIFFER" APPEND)
