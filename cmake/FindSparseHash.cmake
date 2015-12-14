# - Try to find SparseHash
# Once done this will define
#  SPARSEHASH_FOUND - System has SparseHash
#  SPARSEHASH_INCLUDE_DIRS - The SparseHash include directories

find_path(SPARSEHASH_INCLUDE_DIR NAMES sparse_hash_map
    PATHS ${SPARSEHASH_ROOT} $ENV{SPARSEHASH_ROOT} /usr/local/ /usr/ /sw/ /opt/local /opt/csw/ /opt/ ENV CPLUS_INCLUDE_PATH
    PATH_SUFFIXES include/sparsehash include/google include
)

set(SPARSEHASH_INCLUDE_DIRS ${SPARSEHASH_INCLUDE_DIR})

# handle the QUIETLY and REQUIRED arguments and set SPARSEHASH_FOUND to TRUE if
# all listed variables are TRUE
include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(SparseHash DEFAULT_MSG SPARSEHASH_INCLUDE_DIR)

mark_as_advanced(SPARSEHASH_INCLUDE_DIR)