#================================================================================
#
#    csmtest/CMakeLists.txt
#
#  © Copyright IBM Corporation 2015-2020. All Rights Reserved
#
#    This program is licensed under the terms of the Eclipse Public License
#    v1.0 as published by the Eclipse Foundation and available at
#    http://www.eclipse.org/legal/epl-v10.html
#
#    U.S. Government Users Restricted Rights:  Use, duplication or disclosure
#    restricted by GSA ADP Schedule Contract with IBM Corp.
#
#================================================================================

add_subdirectory(analytics)
add_subdirectory(archive)
add_subdirectory(big_data)
add_subdirectory(buckets)
add_subdirectory(include)
add_subdirectory(old_rpms)
add_subdirectory(results)
add_subdirectory(rpms)
add_subdirectory(setup)
add_subdirectory(tools)

# Set up and install the base level dir of the test package.
# include default files

# non executable files
file(GLOB INSTALL_FILES
    "config.template"
    "csm_test_example_defaults.cfg"
    "csm_test.cfg"
    "README.md"
)

install(FILES ${INSTALL_FILES} COMPONENT csm-test DESTINATION csm/test)






# drwxr-xr-x   3 root root  4096 Jan 23 10:30 rpms


