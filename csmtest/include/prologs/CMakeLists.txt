#================================================================================
#
#    csmtest/include/prologs/CMakeLists.txt
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

# executable files
file(GLOB INSTALL_FILES
	"privileged_epilog"
	"privileged_epilog_generic"
	"privileged_epilog_timeout"
	"privileged_prolog"
	"privileged_prolog_255"
	"privileged_prolog_generic"
	"privileged_prolog_timeout"
)

install(PROGRAMS ${INSTALL_FILES} COMPONENT csm-test DESTINATION csm/test/include/prologs)
