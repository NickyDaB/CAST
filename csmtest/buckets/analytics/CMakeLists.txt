#================================================================================
#
#    csmtest/buckets/analytics/CMakeLists.txt
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

# non executable files
file(GLOB INSTALL_SCRIPTS
    "example.txt"
)

install(FILES ${INSTALL_SCRIPTS} COMPONENT csm-test DESTINATION csm/test/examples)

# Files that should be executable when shipped

file(GLOB INSTALL_PROGRAMS
  "core_blinking.sh"
)

install(PROGRAMS ${INSTALL_PROGRAMS} COMPONENT csm-test DESTINATION csm/test)

file(GLOB INSTALL_DIR
	""
)

install(FILES ${INSTALL_DIR} COMPONENT csm-test DESTINATION csm/test/other)

# CPACK_RPM settings for the csm-test rpm are managed in csmd/setupRPM.cmake 