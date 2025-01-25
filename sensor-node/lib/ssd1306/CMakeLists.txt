set(component_srcs "ssd1306.c" "ssd1306_spi.c")

# Always use new driver for IDF 5.x
list(APPEND component_srcs "ssd1306_i2c_new.c")

idf_component_register(SRCS "${component_srcs}" PRIV_REQUIRES driver INCLUDE_DIRS ".")
