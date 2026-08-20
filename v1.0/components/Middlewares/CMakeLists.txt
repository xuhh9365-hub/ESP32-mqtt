set(src_dirs
			MQTT)

set(include_dirs
			MQTT)

set(requires
			driver
			mqtt
			esp_event)

idf_component_register(SRC_DIRS ${src_dirs}
					  INCLUDE_DIRS ${include_dirs} REQUIRES ${requires})

component_compile_options(-ffast-math -O3 -Wno-error=format=-Wno-format)

