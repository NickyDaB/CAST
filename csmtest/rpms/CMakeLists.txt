#================================================================================
#
#    csmtest/rpms/CMakeLists.txt
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

# Set up and install the base level dir of the test package.
# include default files

# non executable files
file(GLOB INSTALL_FILES
    "README.md"
)

install(FILES ${INSTALL_FILES} COMPONENT csm-test DESTINATION csm/test/rpms)


