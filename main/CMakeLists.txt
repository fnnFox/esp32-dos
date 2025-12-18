idf_component_register(
	SRCS "main.c"
	INCLUDE_DIRS "."
	REQUIRES elf_loader uart_receiver shell
)
