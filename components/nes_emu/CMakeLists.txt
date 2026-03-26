idf_component_register(
    SRCS "src/nes_cpu.c"
         "src/nes_ppu.c"
         "src/nes_apu.c"
         "src/nes_cart.c"
         "src/nes_emu.c"
    INCLUDE_DIRS "include"
    REQUIRES "esp_common" "heap"
)
