idf_component_register(
	SRCS
		"src/sdcard.c"
	INCLUDE_DIRS
		"include"
	REQUIRES
		fatfs driver
)
