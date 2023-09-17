# SPDX-License-Identifier: MIT
#
# - Try to find nwind_capi library
#
# This will define
# NWIND_CAPI_FOUND
# NWIND_CAPI_INCLUDE_DIRS
# NWIND_CAPI_LIBRARIES
#

find_path(NWIND_CAPI_INCLUDE_DIRS NAMES nwind_capi.h)
find_library(NWIND_CAPI_LIBRARIES NAMES nwind_capi)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(
    NWIND_CAPI DEFAULT_MSG
    NWIND_CAPI_LIBRARIES NWIND_CAPI_INCLUDE_DIRS
)

mark_as_advanced(NWIND_CAPI_INCLUDE_DIRS NWIND_CAPI_LIBRARIES)
