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

add_subdirectory(helper_files)

file(GLOB INSTALL_FILES
  "analytics_log_README.md"
)

install(FILES ${INSTALL_FILES} COMPONENT csm-test DESTINATION /var/log/ibm/csm/fvt_analytics/)

# Files that should be executable when shipped

file(GLOB INSTALL_PROGRAMS
  "core_blinking.sh"
)

install(PROGRAMS ${INSTALL_PROGRAMS} COMPONENT csm-test DESTINATION csm/test/buckets/analytics)

# CPACK_RPM settings for the csm-test rpm are managed in csmd/setupRPM.cmake 