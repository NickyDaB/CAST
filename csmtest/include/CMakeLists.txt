#================================================================================
#
#    csmtest/include/CMakeLists.txt
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

add_subdirectory(hcdiag)
add_subdirectory(json)
add_subdirectory(prologs)
add_subdirectory(sql)

# executable files
file(GLOB INSTALL_SCRIPTS
	"functions.sh"
	"jsrun_cmd_test_script.sh"
)

install(PROGRAMS ${INSTALL_SCRIPTS} COMPONENT csm-test DESTINATION csm/test/include)
