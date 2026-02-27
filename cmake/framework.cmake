include(platform)

# Shared code between projects.
include(macros)
include(setup_app)
include(setup_module)

# PLATFORM is not always used.
mark_as_advanced(PLATFORM)
