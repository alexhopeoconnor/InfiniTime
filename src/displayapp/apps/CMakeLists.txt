if(DEFINED ENABLE_USERAPPS)
    set(USERAPP_TYPES ${ENABLE_USERAPPS} CACHE STRING "List of user apps to build into the firmware")
else ()
    # ElixirTime is intentionally a focused watch, not a general-purpose
    # smartwatch.  Keep the user-facing app set small and make the Terminal
    # face the primary face.  System applications (settings, notifications,
    # firmware update, flashlight, and battery information) are registered
    # independently of this list.
    set(DEFAULT_USER_APP_TYPES "Apps::StopWatch")
    set(DEFAULT_USER_APP_TYPES "${DEFAULT_USER_APP_TYPES}, Apps::Alarm")
    set(DEFAULT_USER_APP_TYPES "${DEFAULT_USER_APP_TYPES}, Apps::Timer")
    set(DEFAULT_USER_APP_TYPES "${DEFAULT_USER_APP_TYPES}, Apps::Steps")
    set(DEFAULT_USER_APP_TYPES "${DEFAULT_USER_APP_TYPES}, Apps::HeartRate")
    set(USERAPP_TYPES "${DEFAULT_USER_APP_TYPES}" CACHE STRING "List of user apps to build into the firmware")
endif ()

if(DEFINED ENABLE_WATCHFACES)
    set(WATCHFACE_TYPES ${ENABLE_WATCHFACES} CACHE STRING "List of watch faces to build into the firmware" FORCE)
else()
    # ElixirTime intentionally ships one face. Existing installations may
    # retain another upstream value in settings.dat; DisplayApp safely falls
    # back to this first (and only) compiled face without changing that
    # compatibility data.
    set(DEFAULT_WATCHFACE_TYPES "WatchFace::Terminal")
    # FORCE is intentional: a previous build directory must not keep an old
    # face set after the product registry changed.
    set(WATCHFACE_TYPES "${DEFAULT_WATCHFACE_TYPES}" CACHE STRING "List of watch faces to build into the firmware" FORCE)
endif()

add_library(infinitime_apps INTERFACE)
target_sources(infinitime_apps INTERFACE "${CMAKE_CURRENT_BINARY_DIR}/Apps.h")
target_include_directories(infinitime_apps INTERFACE "${CMAKE_CURRENT_BINARY_DIR}/")

# Generate the list of user apps to be compiled into the firmware
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/Apps.h.in ${CMAKE_CURRENT_BINARY_DIR}/Apps.h)
