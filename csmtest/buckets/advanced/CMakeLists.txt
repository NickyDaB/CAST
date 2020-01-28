#================================================================================
#
#    csmtest/buckets/advanced/CMakeLists.txt
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

# Files that should be executable when shipped

file(GLOB INSTALL_PROGRAMS
  "allocation.sh"
  "allocation_timing.py"
)

install(PROGRAMS ${INSTALL_PROGRAMS} COMPONENT csm-test DESTINATION csm/test/buckets/advanced)

# CPACK_RPM settings for the csm-test rpm are managed in csmd/setupRPM.cmake 