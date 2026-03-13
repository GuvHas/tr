idf_component_register(
    SRCS "app_main.cpp"
    INCLUDE_DIRS "."
    REQUIRES esp_matter nvs_flash freertos log openthread
)

# Expose the connectedhomeip source tree so app code can use CHIP SDK headers
# directly (e.g. <app/server/OnboardingCodesUtil.h>).  The managed component
# keeps its headers private to avoid polluting all consumers; we opt-in here.
idf_component_get_property(esp_matter_dir espressif__esp_matter COMPONENT_DIR)
target_include_directories(${COMPONENT_LIB} PRIVATE
    "${esp_matter_dir}/connectedhomeip/connectedhomeip/src"
)
