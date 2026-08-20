set(src_dirs
		WIFI
		RS485
		MODBUS	
			LED)

set(include_dirs
		WIFI
		RS485
		MODBUS
			LED)

set(requires
			driver
			esp-modbus
			esp_wifi
			esp_netif
			nvs_flash
			esp_event)

idf_component_register(SRC_DIRS ${src_dirs}
					  INCLUDE_DIRS ${include_dirs} REQUIRES ${requires})

component_compile_options(-ffast-math -O3 -Wno-error=format=-Wno-format)





