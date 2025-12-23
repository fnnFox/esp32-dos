SECTIONS
{
    /* Сегмент 1: только исполняемый код */
    .text : {
        *(.text .text.*)
    }
    
    /* Сегмент 2: данные только для чтения */
    .rodata : {
        *(.rodata .rodata.*)
        *(.srodata .srodata.*)
    }
    
    /* Литералы Xtensa тоже в rodata */
    .got.loc : {
        *(.got.loc)
    }
    
    /* Сегмент 3: читаемые/записываемые данные */
    .data : {
        *(.data .data.*)
        *(.sdata .sdata.*)
    }
    
    .dynamic : {
        *(.dynamic)
    }
    
    .got : {
        *(.got .got.*)
    }
    
    .bss : {
        *(.bss .bss.*)
        *(COMMON)
    }
}
