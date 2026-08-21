idf_component_register(SRCS "device_manager.c" "device_scheduler.c"
                    INCLUDE_DIRS "include" "."
                    REQUIRES config_manager BSP)
