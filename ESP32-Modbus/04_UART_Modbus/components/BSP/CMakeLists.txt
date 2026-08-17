set(src_dirs
		
		RS485
		MODBUS	
			LED)

set(include_dirs
		
		RS485
		MODBUS
			LED)

set(requires
			driver
			esp-modbus)

idf_component_register(SRC_DIRS ${src_dirs}
					  INCLUDE_DIRS ${include_dirs} REQUIRES ${requires})

component_compile_options(-ffast-math -O3 -Wno-error=format=-Wno-format)





